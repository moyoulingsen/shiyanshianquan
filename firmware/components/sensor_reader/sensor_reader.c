#include "sensor_reader.h"

#include <stdbool.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

static const char *TAG = "sensor_reader";

#define SENSOR_I2C_TIMEOUT_MS 100
#define SENSOR_I2C_BOOT_SCAN_TIMEOUT_MS 20

#define SHT3X_ADDR_DEFAULT CONFIG_LABGUARD_SHT3X_ADDR
#define SHT3X_ADDR_ALT 0x45
#define SHT3X_CMD_SOFT_RESET 0x30A2
#define SHT3X_CMD_SINGLE_HIGH_NO_CLOCK_STRETCH 0x2400

#define ENS160_ADDR_DEFAULT CONFIG_LABGUARD_ENS160_ADDR
#define ENS160_ADDR_ALT_0 0x52
#define ENS160_ADDR_ALT_1 0x53
#define ENS160_PART_ID 0x0160
#define ENS160_REG_PART_ID 0x00
#define ENS160_REG_OPMODE 0x10
#define ENS160_REG_CONFIG 0x11
#define ENS160_REG_TEMP_IN 0x13
#define ENS160_REG_STATUS 0x20
#define ENS160_REG_AQI 0x21
#define ENS160_REG_TVOC 0x22
#define ENS160_REG_ECO2 0x24
#define ENS160_OPMODE_STANDARD 0x02

typedef struct {
    float temperature_c;
    float humidity_rh;
    uint8_t ens_status;
    uint8_t ens_aqi;
    uint16_t ens_tvoc_ppb;
    uint16_t ens_eco2_ppm;
    bool mq2_alarm;
} physical_sensor_sample_t;

typedef struct {
    int sda_gpio;
    int scl_gpio;
    const char *name;
} sensor_i2c_pin_pair_t;

static uint32_t s_cycle;
static i2c_master_bus_handle_t s_i2c_bus;
static i2c_master_dev_handle_t s_sht_dev;
static i2c_master_dev_handle_t s_ens_dev;
static bool s_i2c_ready;
static bool s_sht_ready;
static bool s_ens_ready;
static bool s_mq2_ready;
static uint8_t s_sht_addr;
static uint8_t s_ens_addr;
static int s_i2c_sda_gpio;
static int s_i2c_scl_gpio;
static physical_sensor_sample_t s_last_sample;
static bool s_last_sample_valid;

typedef struct {
    bool initialized;
    float estimate;
    float covariance;
    float process_noise;
    float measurement_noise;
} kalman_filter_t;

static kalman_filter_t s_temperature_filter;
static kalman_filter_t s_humidity_filter;
static kalman_filter_t s_voc_filter;

static kalman_filter_t make_filter(float process_noise, float measurement_noise)
{
    kalman_filter_t filter = {
        .initialized = false,
        .estimate = 0.0f,
        .covariance = 1.0f,
        .process_noise = process_noise,
        .measurement_noise = measurement_noise,
    };
    return filter;
}

static void reset_filters(void)
{
    s_temperature_filter = make_filter(0.08f, 0.75f);
    s_humidity_filter = make_filter(0.12f, 1.20f);
    s_voc_filter = make_filter(4.0f, 24.0f);
}

static float kalman_update(kalman_filter_t *filter, float measurement)
{
    if (!filter->initialized) {
        filter->estimate = measurement;
        filter->covariance = filter->measurement_noise;
        filter->initialized = true;
        return filter->estimate;
    }

    filter->covariance += filter->process_noise;
    float gain = filter->covariance / (filter->covariance + filter->measurement_noise);
    filter->estimate += gain * (measurement - filter->estimate);
    filter->covariance = (1.0f - gain) * filter->covariance;
    return filter->estimate;
}

static uint8_t sht3x_crc8(const uint8_t *data)
{
    uint8_t crc = 0xFF;

    for (int i = 0; i < 2; i++) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; bit++) {
            crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x31) : (uint8_t)(crc << 1);
        }
    }

    return crc;
}

static uint16_t u16_le(const uint8_t *data)
{
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

static void put_u16_le(uint8_t *data, uint16_t value)
{
    data[0] = value & 0xFF;
    data[1] = (value >> 8) & 0xFF;
}

static uint16_t clamp_u16_from_float(float value, float min, float max)
{
    if (value < min) {
        value = min;
    } else if (value > max) {
        value = max;
    }
    return (uint16_t)(value + 0.5f);
}

static esp_err_t i2c_add_device(uint8_t addr, i2c_master_dev_handle_t *handle)
{
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = addr,
        .scl_speed_hz = CONFIG_LABGUARD_SENSOR_I2C_FREQ_HZ,
    };
    return i2c_master_bus_add_device(s_i2c_bus, &dev_cfg, handle);
}

static esp_err_t i2c_write_reg(i2c_master_dev_handle_t dev, uint8_t reg, const uint8_t *data, size_t len)
{
    if (len > 7) {
        return ESP_ERR_INVALID_SIZE;
    }

    uint8_t tx[8] = {reg};
    for (size_t i = 0; i < len; i++) {
        tx[i + 1] = data[i];
    }
    return i2c_master_transmit(dev, tx, len + 1, SENSOR_I2C_TIMEOUT_MS);
}

static esp_err_t i2c_read_reg(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t *data, size_t len)
{
    return i2c_master_transmit_receive(dev, &reg, 1, data, len, SENSOR_I2C_TIMEOUT_MS);
}

static esp_err_t i2c_write_u8(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t value)
{
    return i2c_write_reg(dev, reg, &value, 1);
}

static esp_err_t sht3x_write_cmd(uint16_t cmd)
{
    uint8_t tx[2] = {(uint8_t)(cmd >> 8), (uint8_t)(cmd & 0xFF)};
    return i2c_master_transmit(s_sht_dev, tx, sizeof(tx), SENSOR_I2C_TIMEOUT_MS);
}

static esp_err_t sht3x_read(float *temperature_c, float *humidity_rh)
{
    uint8_t rx[6] = {0};
    esp_err_t ret = sht3x_write_cmd(SHT3X_CMD_SINGLE_HIGH_NO_CLOCK_STRETCH);
    if (ret != ESP_OK) {
        return ret;
    }

    vTaskDelay(pdMS_TO_TICKS(20));

    ret = i2c_master_receive(s_sht_dev, rx, sizeof(rx), SENSOR_I2C_TIMEOUT_MS);
    if (ret != ESP_OK) {
        return ret;
    }

    if (sht3x_crc8(&rx[0]) != rx[2] || sht3x_crc8(&rx[3]) != rx[5]) {
        return ESP_ERR_INVALID_CRC;
    }

    uint16_t raw_temp = ((uint16_t)rx[0] << 8) | rx[1];
    uint16_t raw_hum = ((uint16_t)rx[3] << 8) | rx[4];
    float temp = -45.0f + 175.0f * (float)raw_temp / 65535.0f;
    float hum = 100.0f * (float)raw_hum / 65535.0f;

    if (temp < -40.0f || temp > 125.0f || hum < 0.0f || hum > 100.0f) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    *temperature_c = temp;
    *humidity_rh = hum;
    return ESP_OK;
}

static esp_err_t ens160_write_temp_hum(float temperature_c, float humidity_rh)
{
    uint8_t compensation[4] = {0};
    uint16_t temp_k64 = clamp_u16_from_float((temperature_c + 273.15f) * 64.0f, 0.0f, 65535.0f);
    uint16_t hum_512 = clamp_u16_from_float(humidity_rh * 512.0f, 0.0f, 65535.0f);

    put_u16_le(&compensation[0], temp_k64);
    put_u16_le(&compensation[2], hum_512);
    return i2c_write_reg(s_ens_dev, ENS160_REG_TEMP_IN, compensation, sizeof(compensation));
}

static esp_err_t ens160_read(physical_sensor_sample_t *sample)
{
    uint8_t status = 0;
    uint8_t aqi = 0;
    uint8_t tvoc[2] = {0};
    uint8_t eco2[2] = {0};
    esp_err_t ret = i2c_read_reg(s_ens_dev, ENS160_REG_STATUS, &status, 1);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = i2c_read_reg(s_ens_dev, ENS160_REG_AQI, &aqi, 1);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = i2c_read_reg(s_ens_dev, ENS160_REG_TVOC, tvoc, sizeof(tvoc));
    if (ret != ESP_OK) {
        return ret;
    }

    ret = i2c_read_reg(s_ens_dev, ENS160_REG_ECO2, eco2, sizeof(eco2));
    if (ret != ESP_OK) {
        return ret;
    }

    sample->ens_status = (status >> 2) & 0x03;
    sample->ens_aqi = aqi;
    sample->ens_tvoc_ppb = u16_le(tvoc);
    sample->ens_eco2_ppm = u16_le(eco2);
    return ESP_OK;
}

static int ens160_to_voc_index(uint8_t aqi, uint16_t tvoc_ppb)
{
    int aqi_index;

    switch (aqi) {
    case 1:
        aqi_index = 50;
        break;
    case 2:
        aqi_index = 100;
        break;
    case 3:
        aqi_index = 180;
        break;
    case 4:
        aqi_index = 260;
        break;
    case 5:
        aqi_index = 340;
        break;
    default:
        aqi_index = 0;
        break;
    }

    int tvoc_index = tvoc_ppb / 4;
    if (tvoc_index > 500) {
        tvoc_index = 500;
    }

    return tvoc_index > aqi_index ? tvoc_index : aqi_index;
}

static void fill_default(labguard_sensor_data_t *out)
{
    out->temperature_c = 25.8f;
    out->humidity_rh = 54.0f;
    out->voc_index = 50;
    out->temperature_raw_c = out->temperature_c;
    out->humidity_raw_rh = out->humidity_rh;
    out->voc_raw_index = out->voc_index;
    out->mq2_alarm = false;
    out->sensor_ok = false;
    out->filtered = false;
    out->timestamp = esp_timer_get_time() / 1000000;
}

static esp_err_t read_physical_sample(physical_sensor_sample_t *sample)
{
    bool sht_ok = false;
    bool ens_ok = false;

    if (s_sht_ready) {
        esp_err_t ret = sht3x_read(&sample->temperature_c, &sample->humidity_rh);
        if (ret == ESP_OK) {
            sht_ok = true;
        }
    }

    if (!sht_ok) {
        if (s_last_sample_valid) {
            sample->temperature_c = s_last_sample.temperature_c;
            sample->humidity_rh = s_last_sample.humidity_rh;
        } else {
            sample->temperature_c = 25.8f;
            sample->humidity_rh = 54.0f;
        }
    }

    if (s_ens_ready) {
        if (sht_ok) {
            ens160_write_temp_hum(sample->temperature_c, sample->humidity_rh);
        }

        esp_err_t ret = ens160_read(sample);
        if (ret == ESP_OK) {
            ens_ok = true;
        }
    }

    if (!ens_ok) {
        if (s_last_sample_valid) {
            sample->ens_status = s_last_sample.ens_status;
            sample->ens_aqi = s_last_sample.ens_aqi;
            sample->ens_tvoc_ppb = s_last_sample.ens_tvoc_ppb;
            sample->ens_eco2_ppm = s_last_sample.ens_eco2_ppm;
        } else {
            sample->ens_status = 3;
            sample->ens_aqi = 1;
            sample->ens_tvoc_ppb = 0;
            sample->ens_eco2_ppm = 400;
        }
    }

    if (sht_ok || ens_ok) {
        s_last_sample = *sample;
        s_last_sample_valid = true;
    }

    sample->mq2_alarm = false;
    return (sht_ok && ens_ok) ? ESP_OK : ESP_FAIL;
}

esp_err_t sensor_reader_init(void)
{
    s_cycle = 0;
    s_i2c_ready = false;
    s_sht_ready = false;
    s_ens_ready = false;
    s_mq2_ready = false;
    s_last_sample_valid = false;
    reset_filters();

    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = CONFIG_LABGUARD_SENSOR_I2C_PORT,
        .sda_io_num = CONFIG_LABGUARD_SENSOR_I2C_SDA_GPIO,
        .scl_io_num = CONFIG_LABGUARD_SENSOR_I2C_SCL_GPIO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = CONFIG_LABGUARD_SENSOR_I2C_INTERNAL_PULLUPS,
    };
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus_cfg, &s_i2c_bus), TAG, "I2C bus init failed");
    s_i2c_ready = true;
    s_sht_ready = true;
    s_ens_ready = true;

    i2c_device_config_t sht_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = SHT3X_ADDR_DEFAULT,
        .scl_speed_hz = CONFIG_LABGUARD_SENSOR_I2C_FREQ_HZ,
    };
    i2c_device_config_t ens_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = ENS160_ADDR_DEFAULT,
        .scl_speed_hz = CONFIG_LABGUARD_SENSOR_I2C_FREQ_HZ,
    };
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(s_i2c_bus, &sht_cfg, &s_sht_dev), TAG, "SHT3x add failed");
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(s_i2c_bus, &ens_cfg, &s_ens_dev), TAG, "ENS160 add failed");

    uint8_t reset_cmd[2] = {(uint8_t)(SHT3X_CMD_SOFT_RESET >> 8), (uint8_t)(SHT3X_CMD_SOFT_RESET & 0xFF)};
    i2c_master_transmit(s_sht_dev, reset_cmd, sizeof(reset_cmd), SENSOR_I2C_TIMEOUT_MS);
    i2c_write_u8(s_ens_dev, ENS160_REG_OPMODE, ENS160_OPMODE_STANDARD);
    i2c_write_u8(s_ens_dev, ENS160_REG_CONFIG, 0x00);
    ens160_write_temp_hum(25.0f, 50.0f);

#if CONFIG_LABGUARD_MQ2_ENABLE
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << CONFIG_LABGUARD_MQ2_DO_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = CONFIG_LABGUARD_MQ2_INTERNAL_PULLUP ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&cfg);
    s_mq2_ready = true;
#endif

    return ESP_OK;
}

i2c_master_bus_handle_t sensor_reader_get_i2c_bus(void)
{
    return s_i2c_ready ? s_i2c_bus : NULL;
}

esp_err_t sensor_reader_read(labguard_sensor_data_t *out)
{
    if (out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    s_cycle++;
    fill_default(out);

    physical_sensor_sample_t sample = {0};
    esp_err_t ret = read_physical_sample(&sample);
    out->temperature_c = sample.temperature_c;
    out->humidity_rh = sample.humidity_rh;
    out->voc_index = ens160_to_voc_index(sample.ens_aqi, sample.ens_tvoc_ppb);
    out->mq2_alarm = sample.mq2_alarm;
    out->sensor_ok = ret == ESP_OK;

    out->temperature_raw_c = out->temperature_c;
    out->humidity_raw_rh = out->humidity_rh;
    out->voc_raw_index = out->voc_index;
    out->temperature_c = kalman_update(&s_temperature_filter, out->temperature_c);
    out->humidity_rh = kalman_update(&s_humidity_filter, out->humidity_rh);
    out->voc_index = (int)(kalman_update(&s_voc_filter, (float)out->voc_index) + 0.5f);
    out->filtered = true;
    return ESP_OK;
}
