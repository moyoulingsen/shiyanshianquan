#include "sensor_reader.h"

#include <stdbool.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

static const char *TAG = "sensor_reader";

#ifdef CONFIG_LABGUARD_MQ2_ACTIVE_LOW
#define MQ2_ACTIVE_LOW 1
#else
#define MQ2_ACTIVE_LOW 0
#endif

#define SENSOR_I2C_TIMEOUT_MS 100
#define MQ2_ADC_MAX_MV 3300

#define SHT3X_ADDR_DEFAULT CONFIG_LABGUARD_SHT3X_ADDR
#define SHT3X_CMD_SOFT_RESET 0x30A2
#define SHT3X_CMD_SINGLE_HIGH_NO_CLOCK_STRETCH 0x2400

#define ENS160_ADDR_DEFAULT CONFIG_LABGUARD_ENS160_ADDR
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
    int mq2_raw_adc;
    int mq2_raw_mv;
    bool mq2_alarm;
    bool mq2_analog_valid;
} physical_sensor_sample_t;

static uint32_t s_cycle;
static i2c_master_bus_handle_t s_i2c_bus;
static i2c_master_dev_handle_t s_sht_dev;
static i2c_master_dev_handle_t s_ens_dev;
static bool s_i2c_ready;
static bool s_sht_ready;
static bool s_ens_ready;
static bool s_mq2_ready;
static bool s_mq2_adc_ready;
#if CONFIG_LABGUARD_MQ2_ANALOG_ENABLE
static adc_oneshot_unit_handle_t s_mq2_adc_unit;
#endif
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

#if CONFIG_LABGUARD_MQ2_ANALOG_ENABLE
static adc_channel_t mq2_gpio_to_adc_channel(int gpio)
{
    switch (gpio) {
    case 11:
        return ADC_CHANNEL_0;
    case 12:
        return ADC_CHANNEL_1;
    case 13:
        return ADC_CHANNEL_2;
    case 14:
        return ADC_CHANNEL_3;
    case 15:
        return ADC_CHANNEL_4;
    case 16:
        return ADC_CHANNEL_5;
    case 17:
        return ADC_CHANNEL_6;
    default:
        return ADC_CHANNEL_MAX;
    }
}

static esp_err_t read_mq2_analog(int *raw_adc, int *raw_mv)
{
    if (!s_mq2_adc_ready || raw_adc == NULL || raw_mv == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    adc_channel_t channel = mq2_gpio_to_adc_channel(CONFIG_LABGUARD_MQ2_AO_GPIO);
    if (channel == ADC_CHANNEL_MAX) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    int total = 0;
    for (int i = 0; i < CONFIG_LABGUARD_MQ2_ADC_SAMPLES; i++) {
        int sample = 0;
        esp_err_t ret = adc_oneshot_read(s_mq2_adc_unit, channel, &sample);
        if (ret != ESP_OK) {
            return ret;
        }
        total += sample;
    }

    *raw_adc = total / CONFIG_LABGUARD_MQ2_ADC_SAMPLES;
    *raw_mv = (*raw_adc * MQ2_ADC_MAX_MV) / 4095;
    return ESP_OK;
}
#endif

static void fill_default(labguard_sensor_data_t *out)
{
    out->temperature_c = 25.8f;
    out->humidity_rh = 54.0f;
    out->voc_index = 50;
    out->temperature_raw_c = out->temperature_c;
    out->humidity_raw_rh = out->humidity_rh;
    out->voc_raw_index = out->voc_index;
    out->mq2_raw_adc = 0;
    out->mq2_raw_mv = 0;
    out->mq2_alarm = false;
    out->mq2_analog_valid = false;
    out->sensor_ok = false;
    out->filtered = false;
    out->timestamp = esp_timer_get_time() / 1000000;
}

static bool read_mq2_alarm(void)
{
#if CONFIG_LABGUARD_MQ2_ENABLE
    if (!s_mq2_ready) {
        return false;
    }

    int level = gpio_get_level(CONFIG_LABGUARD_MQ2_DO_GPIO);
    return MQ2_ACTIVE_LOW ? (level == 0) : (level != 0);
#else
    return false;
#endif
}

static bool derive_mq2_alarm(int mq2_raw_mv, bool digital_alarm)
{
#if CONFIG_LABGUARD_MQ2_ANALOG_ENABLE
    if (mq2_raw_mv > 0) {
        return mq2_raw_mv >= CONFIG_LABGUARD_MQ2_ALARM_THRESHOLD_MV;
    }
#endif
    return digital_alarm;
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

    sample->mq2_alarm = read_mq2_alarm();
#if CONFIG_LABGUARD_MQ2_ANALOG_ENABLE
    sample->mq2_raw_adc = 0;
    sample->mq2_raw_mv = 0;
    sample->mq2_analog_valid = false;
    if (read_mq2_analog(&sample->mq2_raw_adc, &sample->mq2_raw_mv) == ESP_OK) {
        sample->mq2_analog_valid = true;
    }
#endif
    sample->mq2_alarm = derive_mq2_alarm(sample->mq2_raw_mv, sample->mq2_alarm);

    if (sht_ok || ens_ok) {
        s_last_sample = *sample;
        s_last_sample_valid = true;
    }

    return (sht_ok && ens_ok) ? ESP_OK : ESP_FAIL;
}

esp_err_t sensor_reader_init(void)
{
    s_cycle = 0;
    s_i2c_ready = false;
    s_sht_ready = false;
    s_ens_ready = false;
    s_mq2_ready = false;
    s_mq2_adc_ready = false;
    s_last_sample_valid = false;
    reset_filters();

#ifdef CONFIG_LABGUARD_SENSOR_I2C_ENABLE
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
    ESP_LOGI(TAG,
             "SHT3x/ENS160 I2C initialized port=%d SDA=GPIO%d SCL=GPIO%d",
             CONFIG_LABGUARD_SENSOR_I2C_PORT,
             CONFIG_LABGUARD_SENSOR_I2C_SDA_GPIO,
             CONFIG_LABGUARD_SENSOR_I2C_SCL_GPIO);

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
#else
    ESP_LOGW(TAG, "SHT3x/ENS160 I2C disabled");
#endif

#if CONFIG_LABGUARD_MQ2_ENABLE
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << CONFIG_LABGUARD_MQ2_DO_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = CONFIG_LABGUARD_MQ2_INTERNAL_PULLUP ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&cfg), TAG, "MQ-2 GPIO config failed");
    s_mq2_ready = true;
    ESP_LOGI(TAG,
             "MQ-2 DO initialized GPIO%d active_%s",
             CONFIG_LABGUARD_MQ2_DO_GPIO,
             MQ2_ACTIVE_LOW ? "low" : "high");
#endif

#if CONFIG_LABGUARD_MQ2_ANALOG_ENABLE
    adc_oneshot_unit_init_cfg_t adc_cfg = {
        .unit_id = ADC_UNIT_1,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    ESP_RETURN_ON_ERROR(adc_oneshot_new_unit(&adc_cfg, &s_mq2_adc_unit), TAG, "MQ-2 ADC unit init failed");

    adc_channel_t mq2_channel = mq2_gpio_to_adc_channel(CONFIG_LABGUARD_MQ2_AO_GPIO);
    if (mq2_channel == ADC_CHANNEL_MAX) {
        ESP_LOGE(TAG, "MQ-2 AO GPIO%d is not mapped to a supported ADC channel", CONFIG_LABGUARD_MQ2_AO_GPIO);
        return ESP_ERR_NOT_SUPPORTED;
    }

    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    ESP_RETURN_ON_ERROR(adc_oneshot_config_channel(s_mq2_adc_unit, mq2_channel, &chan_cfg),
                        TAG,
                        "MQ-2 ADC channel config failed");
    s_mq2_adc_ready = true;
    ESP_LOGI(TAG,
             "MQ-2 AO initialized GPIO%d channel=%d threshold=%dmV samples=%d",
             CONFIG_LABGUARD_MQ2_AO_GPIO,
             mq2_channel,
             CONFIG_LABGUARD_MQ2_ALARM_THRESHOLD_MV,
             CONFIG_LABGUARD_MQ2_ADC_SAMPLES);
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
    out->mq2_raw_adc = sample.mq2_raw_adc;
    out->mq2_raw_mv = sample.mq2_raw_mv;
    out->mq2_alarm = sample.mq2_alarm;
    out->mq2_analog_valid = sample.mq2_analog_valid;
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
