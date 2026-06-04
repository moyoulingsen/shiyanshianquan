#include "risk_fusion.h"

#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "risk_fusion";

esp_err_t risk_fusion_init(void)
{
    ESP_LOGI(TAG, "risk fusion initialized");
    return ESP_OK;
}

esp_err_t risk_fusion_evaluate(const labguard_sensor_data_t *sensor,
                               const labguard_hazard_result_t *hazard,
                               labguard_risk_state_t *out)
{
    if (sensor == NULL || hazard == NULL || out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    labguard_risk_level_t level = LABGUARD_RISK_NORMAL;
    const char *risk_text = "normal";

    bool temp_high = sensor->temperature_c >= 38.0f;
    bool voc_high = sensor->voc_index >= 180;

    if (hazard->flame && hazard->score_flame >= 0.70f) {
        level = LABGUARD_RISK_EMERGENCY;
        risk_text = "fire_event";
    } else if ((hazard->smoke && sensor->mq2_alarm) || (sensor->mq2_alarm && temp_high)) {
        level = LABGUARD_RISK_ALARM;
        risk_text = "toxic_gas_event";
    } else if ((hazard->smoke && hazard->score_smoke >= 0.50f) || temp_high || voc_high || sensor->mq2_alarm) {
        level = LABGUARD_RISK_WARNING;
        risk_text = "warning";
    }

    out->risk_level = level;
    out->risk_text = risk_text;
    out->smoke = hazard->smoke;
    out->flame = hazard->flame;
    out->gas_alarm = sensor->mq2_alarm;
    out->temperature_c = sensor->temperature_c;
    out->action_alarm = level >= LABGUARD_RISK_ALARM;
    out->action_fan = level >= LABGUARD_RISK_ALARM;
    out->action_pump = level >= LABGUARD_RISK_EMERGENCY;
    out->model = hazard->model;
    out->timestamp = esp_timer_get_time() / 1000000;
    return ESP_OK;
}

