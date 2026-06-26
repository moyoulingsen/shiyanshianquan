#include "ppe_infer.h"

#include <stdint.h>

#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "ppe_infer";
static labguard_profile_t s_profile = LABGUARD_PROFILE_AUTO;
static uint32_t s_cycle;

esp_err_t ppe_infer_init(void)
{
    s_profile = LABGUARD_PROFILE_AUTO;
    s_cycle = 0;
    ESP_LOGI(TAG, "PPE inference simulator initialized");
    return ESP_OK;
}

void ppe_infer_set_profile(labguard_profile_t profile)
{
    s_profile = profile;
}

labguard_profile_t ppe_infer_get_profile(void)
{
    return s_profile;
}

esp_err_t ppe_infer_run(const void *frame, size_t frame_len, labguard_ppe_result_t *out)
{
    (void)frame;
    (void)frame_len;

    if (out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    s_cycle++;
    out->person_present = true;
    out->labcoat = true;
    out->goggles = true;
    out->score_labcoat = 0.95f;
    out->score_goggles = 0.96f;
    out->model = "ppe_sim_v1";
    out->timestamp = esp_timer_get_time() / 1000000;

    switch (s_profile) {
    case LABGUARD_PROFILE_NORMAL:
        break;
    case LABGUARD_PROFILE_WARNING:
        out->goggles = false;
        out->score_goggles = 0.18f;
        break;
    case LABGUARD_PROFILE_ALARM:
        out->labcoat = false;
        out->score_labcoat = 0.22f;
        break;
    case LABGUARD_PROFILE_EMERGENCY:
        out->labcoat = false;
        out->goggles = false;
        out->score_labcoat = 0.11f;
        out->score_goggles = 0.13f;
        break;
    case LABGUARD_PROFILE_AUTO:
    default:
        switch (s_cycle % 12U) {
        case 0:
            out->person_present = false;
            out->score_labcoat = 0.0f;
            out->score_goggles = 0.0f;
            break;
        case 4:
        case 5:
            out->goggles = false;
            out->score_goggles = 0.27f;
            break;
        case 8:
            out->labcoat = false;
            out->score_labcoat = 0.31f;
            break;
        default:
            break;
        }
        break;
    }

    return ESP_OK;
}
