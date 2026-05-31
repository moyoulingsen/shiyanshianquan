#include "sensor_reader.h"

#include <stdbool.h>
#include <stdint.h>

#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "sensor_reader";
static labguard_profile_t s_profile = LABGUARD_PROFILE_AUTO;
static uint32_t s_cycle;

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

static float pattern_noise(uint32_t cycle, float step)
{
    int pattern = (int)((cycle * 37U + 11U) % 9U) - 4;
    return (float)pattern * step;
}

static int pattern_noise_int(uint32_t cycle, int step)
{
    int pattern = (int)((cycle * 19U + 5U) % 9U) - 4;
    return pattern * step;
}

esp_err_t sensor_reader_init(void)
{
    s_profile = LABGUARD_PROFILE_AUTO;
    s_cycle = 0;
    reset_filters();
    ESP_LOGI(TAG, "sensor reader simulator with Kalman smoothing initialized");
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

esp_err_t sensor_reader_read(labguard_sensor_data_t *out)
{
    if (out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    s_cycle++;
    float temperature_c = 25.8f;
    float humidity_rh = 54.0f;
    int voc_index = 78;
    bool mq2_alarm = false;
    out->sensor_ok = true;
    out->timestamp = esp_timer_get_time() / 1000000;

    switch (s_profile) {
    case LABGUARD_PROFILE_NORMAL:
        break;
    case LABGUARD_PROFILE_WARNING:
        temperature_c = 36.8f;
        humidity_rh = 61.0f;
        voc_index = 165;
        break;
    case LABGUARD_PROFILE_ALARM:
        temperature_c = 42.5f;
        humidity_rh = 67.0f;
        voc_index = 228;
        mq2_alarm = true;
        break;
    case LABGUARD_PROFILE_EMERGENCY:
        temperature_c = 58.0f;
        humidity_rh = 72.0f;
        voc_index = 320;
        mq2_alarm = true;
        break;
    case LABGUARD_PROFILE_AUTO:
    default:
        switch (s_cycle % 14U) {
        case 5:
        case 6:
            temperature_c = 35.2f;
            voc_index = 162;
            break;
        case 10:
        case 11:
            temperature_c = 43.0f;
            humidity_rh = 66.0f;
            voc_index = 240;
            mq2_alarm = true;
            break;
        case 12:
            temperature_c = 57.0f;
            humidity_rh = 70.0f;
            voc_index = 305;
            mq2_alarm = true;
            break;
        default:
            break;
        }
        break;
    }

    temperature_c += pattern_noise(s_cycle, 0.18f);
    humidity_rh += pattern_noise(s_cycle + 3U, 0.35f);
    voc_index += pattern_noise_int(s_cycle + 7U, 4);
    if (voc_index < 0) {
        voc_index = 0;
    }

    out->temperature_raw_c = temperature_c;
    out->humidity_raw_rh = humidity_rh;
    out->voc_raw_index = voc_index;
    out->temperature_c = kalman_update(&s_temperature_filter, temperature_c);
    out->humidity_rh = kalman_update(&s_humidity_filter, humidity_rh);
    out->voc_index = (int)(kalman_update(&s_voc_filter, (float)voc_index) + 0.5f);
    out->mq2_alarm = mq2_alarm;
    out->filtered = true;

    return ESP_OK;
}
