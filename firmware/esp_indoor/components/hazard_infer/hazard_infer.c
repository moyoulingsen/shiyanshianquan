#include "hazard_infer.h"

#include <stdint.h>

#include "esp_log.h"

static const char *TAG = "hazard_infer";
static labguard_profile_t s_profile = LABGUARD_PROFILE_AUTO;
static uint32_t s_cycle;

esp_err_t hazard_infer_init(void)
{
    s_profile = LABGUARD_PROFILE_AUTO;
    s_cycle = 0;
    ESP_LOGI(TAG, "hazard inference simulator initialized");
    return ESP_OK;
}

void hazard_infer_set_profile(labguard_profile_t profile)
{
    s_profile = profile;
}

labguard_profile_t hazard_infer_get_profile(void)
{
    return s_profile;
}

esp_err_t hazard_infer_run(const void *frame, size_t frame_len, labguard_hazard_result_t *out)
{
    (void)frame;
    (void)frame_len;

    if (out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    s_cycle++;
    out->smoke = false;
    out->flame = false;
    out->score_smoke = 0.03f;
    out->score_flame = 0.02f;
    out->model = "hazard_sim_v1";

    switch (s_profile) {
    case LABGUARD_PROFILE_NORMAL:
        break;
    case LABGUARD_PROFILE_WARNING:
        out->smoke = true;
        out->score_smoke = 0.56f;
        break;
    case LABGUARD_PROFILE_ALARM:
        out->smoke = true;
        out->score_smoke = 0.86f;
        break;
    case LABGUARD_PROFILE_EMERGENCY:
        out->smoke = true;
        out->flame = true;
        out->score_smoke = 0.88f;
        out->score_flame = 0.91f;
        break;
    case LABGUARD_PROFILE_AUTO:
    default:
        switch (s_cycle % 14U) {
        case 5:
        case 6:
            out->smoke = true;
            out->score_smoke = 0.58f;
            break;
        case 10:
        case 11:
            out->smoke = true;
            out->score_smoke = 0.82f;
            break;
        case 12:
            out->smoke = true;
            out->flame = true;
            out->score_smoke = 0.90f;
            out->score_flame = 0.93f;
            break;
        default:
            break;
        }
        break;
    }

    return ESP_OK;
}
