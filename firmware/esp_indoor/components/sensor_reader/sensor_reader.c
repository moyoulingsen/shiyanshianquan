#include "sensor_reader.h"

#include <stdint.h>

#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "sensor_reader";
static labguard_profile_t s_profile = LABGUARD_PROFILE_AUTO;
static uint32_t s_cycle;

esp_err_t sensor_reader_init(void)
{
    s_profile = LABGUARD_PROFILE_AUTO;
    s_cycle = 0;
    ESP_LOGI(TAG, "sensor reader simulator initialized");
    return ESP_OK;
}

void sensor_reader_set_profile(labguard_profile_t profile)
{
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
    out->temperature_c = 25.8f;
    out->humidity_rh = 54.0f;
    out->voc_index = 78;
    out->mq2_alarm = false;
    out->sensor_ok = true;
    out->timestamp = esp_timer_get_time() / 1000000;

    switch (s_profile) {
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
        switch (s_cycle % 14U) {
        case 5:
        case 6:
            out->temperature_c = 35.2f;
            out->voc_index = 162;
            break;
        case 10:
        case 11:
            out->temperature_c = 43.0f;
            out->humidity_rh = 66.0f;
            out->voc_index = 240;
            out->mq2_alarm = true;
            break;
        case 12:
            out->temperature_c = 57.0f;
            out->humidity_rh = 70.0f;
            out->voc_index = 305;
            out->mq2_alarm = true;
            break;
        default:
            break;
        }
        break;
    }

    return ESP_OK;
}
