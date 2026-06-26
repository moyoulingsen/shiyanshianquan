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

#ifndef CONFIG_LABGUARD_INDOOR_SENSOR_I2C_AUTO_DETECT_PINS
#define CONFIG_LABGUARD_INDOOR_SENSOR_I2C_AUTO_DETECT_PINS 1
#endif

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

static labguard_profile_t s_profile = LABGUARD_PROFILE_AUTO;
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
        .scl_speed_hz = CONFIG_LABGUARD_INDOOR_SENSOR_I2C_FREQ_HZ,
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

static void fill_safe_default(labguard_sensor_data_t *out, bool sensor_ok)
{
    out->temperature_c = 25.8f;
    out->humidity_rh = 54.0f;
    out->voc_index = 50;
    out->temperature_raw_c = out->temperature_c;
    out->humidity_raw_rh = out->humidity_rh;
    out->voc_raw_index = out->voc_index;
    out->mq2_alarm = false;
    out->sensor_ok = sensor_ok;
    out->filtered = false;
    out->timestamp = esp_timer_get_time() / 1000000;
}

static void apply_profile_override(labguard_profile_t profile, labguard_sensor_data_t *out)
{
    switch (profile) {
    case LABGUARD_PROFILE_NORMAL:
        break;
    case LABGUARD_PROFILE_WARNING:
        out->temperature_c = 36.8f;
        out->humidity_rh = 61.0f;
        out->voc_index = 165;
        break;
    case LABGUARD_PROFILE_ALARM:
        out->temperature_c = 42.5f;
        out->humidity_rh = 67.0f;
        out->voc_index = 228;
        out->mq2_alarm = true;
        break;
    case LABGUARD_PROFILE_EMERGENCY:
        out->temperature_c = 58.0f;
        out->humidity_rh = 72.0f;
        out->voc_index = 320;
        out->mq2_alarm = true;
        break;
    case LABGUARD_PROFILE_AUTO:
    default:
        break;
    }
}

static void scan_i2c_bus(void)
{
    if (!CONFIG_LABGUARD_INDOOR_SENSOR_I2C_SCAN || !s_i2c_ready) {
        return;
    }

    ESP_LOGI(TAG,
             "scanning indoor sensor I2C bus: port=%d SDA=GPIO%d SCL=GPIO%d",
             CONFIG_LABGUARD_INDOOR_SENSOR_I2C_PORT,
             s_i2c_sda_gpio,
             s_i2c_scl_gpio);

    for (uint8_t addr = 0x03; addr <= 0x77; addr++) {
        if (i2c_master_probe(s_i2c_bus, addr, SENSOR_I2C_BOOT_SCAN_TIMEOUT_MS) == ESP_OK) {
            ESP_LOGI(TAG, "I2C device found at 0x%02x", addr);
        }
    }
}

static bool bus_has_expected_sensor(void)
{
    const uint8_t expected_addrs[] = {
        SHT3X_ADDR_DEFAULT,
        SHT3X_ADDR_ALT,
        ENS160_ADDR_DEFAULT,
        ENS160_ADDR_ALT_1,
        ENS160_ADDR_ALT_0,
    };

    for (size_t i = 0; i < sizeof(expected_addrs); i++) {
        uint8_t addr = expected_addrs[i];
        if (addr == 0) {
            continue;
        }

        bool duplicate = false;
        for (size_t j = 0; j < i; j++) {
            duplicate = duplicate || addr == expected_addrs[j];
        }
        if (duplicate) {
            continue;
        }

        if (i2c_master_probe(s_i2c_bus, addr, SENSOR_I2C_BOOT_SCAN_TIMEOUT_MS) == ESP_OK) {
            return true;
        }
    }

    return false;
}

static esp_err_t create_i2c_bus(int sda_gpio, int scl_gpio)
{
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = CONFIG_LABGUARD_INDOOR_SENSOR_I2C_PORT,
        .sda_io_num = sda_gpio,
        .scl_io_num = scl_gpio,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = CONFIG_LABGUARD_INDOOR_SENSOR_I2C_INTERNAL_PULLUPS,
    };

    esp_err_t ret = i2c_new_master_bus(&bus_cfg, &s_i2c_bus);
    if (ret == ESP_OK) {
        s_i2c_ready = true;
        s_i2c_sda_gpio = sda_gpio;
        s_i2c_scl_gpio = scl_gpio;
    } else {
        ESP_LOGW(TAG, "failed to initialize I2C bus SDA=GPIO%d SCL=GPIO%d: %s", sda_gpio, scl_gpio, esp_err_to_name(ret));
    }
    return ret;
}

static esp_err_t init_i2c_bus(void)
{
    const sensor_i2c_pin_pair_t candidates[] = {
        {
            .sda_gpio = CONFIG_LABGUARD_INDOOR_SENSOR_I2C_SDA_GPIO,
            .scl_gpio = CONFIG_LABGUARD_INDOOR_SENSOR_I2C_SCL_GPIO,
            .name = "configured",
        },
        {.sda_gpio = 23, .scl_gpio = 22, .name = "p4_function_fly_line"},
        {.sda_gpio = 7, .scl_gpio = 8, .name = "p4_function_fib"},
        {.sda_gpio = 31, .scl_gpio = 34, .name = "p4_function_sample"},
    };

    esp_err_t last_ret = ESP_ERR_NOT_FOUND;

    for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++) {
        const sensor_i2c_pin_pair_t *candidate = &candidates[i];
        bool duplicate = false;

        if (!CONFIG_LABGUARD_INDOOR_SENSOR_I2C_AUTO_DETECT_PINS && i > 0) {
            break;
        }

        for (size_t j = 0; j < i; j++) {
            duplicate = duplicate ||
                        (candidate->sda_gpio == candidates[j].sda_gpio &&
                         candidate->scl_gpio == candidates[j].scl_gpio);
        }
        if (duplicate) {
            continue;
        }

        last_ret = create_i2c_bus(candidate->sda_gpio, candidate->scl_gpio);
        if (last_ret != ESP_OK) {
            continue;
        }

        bool found = bus_has_expected_sensor();
        ESP_LOGI(TAG,
                 "I2C candidate %s SDA=GPIO%d SCL=GPIO%d expected_sensor=%d",
                 candidate->name,
                 candidate->sda_gpio,
                 candidate->scl_gpio,
                 found);

        if (found || !CONFIG_LABGUARD_INDOOR_SENSOR_I2C_AUTO_DETECT_PINS) {
            scan_i2c_bus();
            return ESP_OK;
        }

        i2c_del_master_bus(s_i2c_bus);
        s_i2c_bus = NULL;
        s_i2c_ready = false;
    }

    if (last_ret == ESP_OK) {
        ESP_LOGW(TAG, "no SHT3x/ENS160 detected on common I2C pin pairs; using configured pins for diagnostics");
        ESP_RETURN_ON_ERROR(create_i2c_bus(CONFIG_LABGUARD_INDOOR_SENSOR_I2C_SDA_GPIO,
                                           CONFIG_LABGUARD_INDOOR_SENSOR_I2C_SCL_GPIO),
                            TAG,
                            "configured I2C bus fallback failed");
        scan_i2c_bus();
        return ESP_OK;
    }

    ESP_LOGE(TAG, "failed to initialize any indoor sensor I2C pin pair");
    return last_ret;
}

static esp_err_t init_sht3x(void)
{
    uint8_t candidates[] = {SHT3X_ADDR_DEFAULT, SHT3X_ADDR_ALT};
    uint8_t selected = 0;

    for (size_t i = 0; i < sizeof(candidates); i++) {
        uint8_t addr = candidates[i];
        if (addr == 0 || (i > 0 && addr == candidates[0])) {
            continue;
        }
        if (i2c_master_probe(s_i2c_bus, addr, SENSOR_I2C_TIMEOUT_MS) == ESP_OK) {
            selected = addr;
            break;
        }
    }

    if (selected == 0) {
        ESP_LOGW(TAG, "SHT3x not found at 0x%02x or 0x%02x", SHT3X_ADDR_DEFAULT, SHT3X_ADDR_ALT);
        return ESP_ERR_NOT_FOUND;
    }

    esp_err_t ret = i2c_add_device(selected, &s_sht_dev);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "failed to add SHT3x I2C device 0x%02x: %s", selected, esp_err_to_name(ret));
        return ret;
    }

    s_sht_addr = selected;
    sht3x_write_cmd(SHT3X_CMD_SOFT_RESET);
    vTaskDelay(pdMS_TO_TICKS(10));
    s_sht_ready = true;
    ESP_LOGI(TAG, "SHT3x ready at 0x%02x", s_sht_addr);
    return ESP_OK;
}

static esp_err_t init_mq2(void)
{
#if CONFIG_LABGUARD_MQ2_ENABLE
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << CONFIG_LABGUARD_MQ2_DO_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = CONFIG_LABGUARD_MQ2_INTERNAL_PULLUP ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t ret = gpio_config(&cfg);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "MQ-2 GPIO%d init failed: %s", CONFIG_LABGUARD_MQ2_DO_GPIO, esp_err_to_name(ret));
        return ret;
    }
    s_mq2_ready = true;
    ESP_LOGI(TAG, "MQ-2 DO ready on GPIO%d (active_%s)",
             CONFIG_LABGUARD_MQ2_DO_GPIO,
             CONFIG_LABGUARD_MQ2_ACTIVE_LOW ? "low" : "high");
    return ESP_OK;
#else
    return ESP_OK;
#endif
}

static bool mq2_read_alarm(void)
{
#if CONFIG_LABGUARD_MQ2_ENABLE
    if (!s_mq2_ready) {
        return false;
    }
    int level = gpio_get_level(CONFIG_LABGUARD_MQ2_DO_GPIO);
    return CONFIG_LABGUARD_MQ2_ACTIVE_LOW ? (level == 0) : (level != 0);
#else
    return false;
#endif
}

static esp_err_t init_ens160(void)
{
    uint8_t candidates[] = {ENS160_ADDR_DEFAULT, ENS160_ADDR_ALT_1, ENS160_ADDR_ALT_0};
    uint8_t selected = 0;
    i2c_master_dev_handle_t dev = NULL;

    for (size_t i = 0; i < sizeof(candidates); i++) {
        uint8_t addr = candidates[i];
        if (addr == 0) {
            continue;
        }
        bool duplicate = false;
        for (size_t j = 0; j < i; j++) {
            duplicate = duplicate || addr == candidates[j];
        }
        if (duplicate) {
            continue;
        }

        if (i2c_master_probe(s_i2c_bus, addr, SENSOR_I2C_TIMEOUT_MS) != ESP_OK) {
            continue;
        }

        if (i2c_add_device(addr, &dev) != ESP_OK) {
            continue;
        }

        uint8_t part_id_buf[2] = {0};
        esp_err_t ret = i2c_read_reg(dev, ENS160_REG_PART_ID, part_id_buf, sizeof(part_id_buf));
        uint16_t part_id = u16_le(part_id_buf);
        if (ret == ESP_OK && part_id == ENS160_PART_ID) {
            selected = addr;
            break;
        }

        ESP_LOGW(TAG,
                 "I2C 0x%02x ACKed but ENS160 PART_ID read as 0x%04x (%s)",
                 addr,
                 part_id,
                 esp_err_to_name(ret));
        i2c_master_bus_rm_device(dev);
        dev = NULL;
    }

    if (selected == 0 || dev == NULL) {
        ESP_LOGW(TAG, "ENS160 not found at 0x%02x or 0x%02x", ENS160_ADDR_ALT_1, ENS160_ADDR_ALT_0);
        return ESP_ERR_NOT_FOUND;
    }

    s_ens_addr = selected;
    s_ens_dev = dev;
    ESP_RETURN_ON_ERROR(i2c_write_u8(s_ens_dev, ENS160_REG_OPMODE, ENS160_OPMODE_STANDARD), TAG, "ENS160 set standard mode failed");
    vTaskDelay(pdMS_TO_TICKS(20));
    ESP_RETURN_ON_ERROR(i2c_write_u8(s_ens_dev, ENS160_REG_CONFIG, 0x00), TAG, "ENS160 config write failed");
    ESP_RETURN_ON_ERROR(ens160_write_temp_hum(25.0f, 50.0f), TAG, "ENS160 compensation write failed");
    s_ens_ready = true;
    ESP_LOGI(TAG, "ENS160 ready at 0x%02x", s_ens_addr);
    return ESP_OK;
}

static esp_err_t read_physical_sample(physical_sensor_sample_t *sample)
{
    bool sht_ok = false;
    bool ens_ok = false;

    if (s_sht_ready) {
        esp_err_t ret = sht3x_read(&sample->temperature_c, &sample->humidity_rh);
        if (ret == ESP_OK) {
            sht_ok = true;
        } else if ((s_cycle % 30U) == 1U) {
            ESP_LOGW(TAG, "SHT3x read failed: %s", esp_err_to_name(ret));
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
        } else if ((s_cycle % 30U) == 1U) {
            ESP_LOGW(TAG, "ENS160 read failed: %s", esp_err_to_name(ret));
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

    sample->mq2_alarm = mq2_read_alarm();

    return (sht_ok && ens_ok) ? ESP_OK : ESP_FAIL;
}

esp_err_t sensor_reader_init(void)
{
    s_profile = LABGUARD_PROFILE_AUTO;
    s_cycle = 0;
    s_i2c_ready = false;
    s_sht_ready = false;
    s_ens_ready = false;
    s_mq2_ready = false;
    s_last_sample_valid = false;
    reset_filters();

    esp_err_t ret = init_i2c_bus();
    if (ret == ESP_OK) {
        init_sht3x();
        init_ens160();
    }
    init_mq2();

    ESP_LOGI(TAG,
             "sensor reader initialized: SHT3x=%s ENS160=%s MQ2=%s",
             s_sht_ready ? "ready" : "missing",
             s_ens_ready ? "ready" : "missing",
             s_mq2_ready ? "ready" : "disabled");
    return ESP_OK;
}

void sensor_reader_set_profile(labguard_profile_t profile)
{
    if (s_profile != profile) {
        reset_filters();
    }
    s_profile = profile;
}

labguard_profile_t sensor_reader_get_profile(void)
{
    return s_profile;
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

    fill_safe_default(out, false);

    if (s_profile == LABGUARD_PROFILE_AUTO) {
        physical_sensor_sample_t sample = {0};
        esp_err_t ret = read_physical_sample(&sample);
        out->temperature_c = sample.temperature_c;
        out->humidity_rh = sample.humidity_rh;
        out->voc_index = ens160_to_voc_index(sample.ens_aqi, sample.ens_tvoc_ppb);
        out->mq2_alarm = sample.mq2_alarm;
        out->sensor_ok = ret == ESP_OK;

        if ((s_cycle % 5U) == 1U) {
            ESP_LOGI(TAG,
                     "sensor temp=%.1fC hum=%.1f%% voc_index=%d mq2=%d ENS160(addr=0x%02x,status=%u,aqi=%u,tvoc=%uppb,eco2=%uppm) ok=%d",
                     out->temperature_c,
                     out->humidity_rh,
                     out->voc_index,
                     out->mq2_alarm,
                     s_ens_addr,
                     sample.ens_status,
                     sample.ens_aqi,
                     sample.ens_tvoc_ppb,
                     sample.ens_eco2_ppm,
                     out->sensor_ok);
        }
    } else {
        out->sensor_ok = true;
        apply_profile_override(s_profile, out);
    }

    out->temperature_raw_c = out->temperature_c;
    out->humidity_raw_rh = out->humidity_rh;
    out->voc_raw_index = out->voc_index;

    out->temperature_c = kalman_update(&s_temperature_filter, out->temperature_c);
    out->humidity_rh = kalman_update(&s_humidity_filter, out->humidity_rh);
    out->voc_index = (int)(kalman_update(&s_voc_filter, (float)out->voc_index) + 0.5f);
    out->filtered = true;

    return ESP_OK;
}
