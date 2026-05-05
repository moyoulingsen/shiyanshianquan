#include "actuator_ctrl.h"

#include <string.h>

#include "esp_log.h"

static const char *TAG = "actuator_ctrl";
static labguard_risk_state_t s_last_risk;

esp_err_t actuator_ctrl_init(void)
{
    memset(&s_last_risk, 0, sizeof(s_last_risk));
    s_last_risk.risk_text = "normal";
    ESP_LOGI(TAG, "actuator controller initialized");
    return ESP_OK;
}

esp_err_t actuator_ctrl_apply_risk(const labguard_risk_state_t *risk)
{
    if (risk == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    s_last_risk = *risk;
    ESP_LOGI(TAG,
             "risk=%s alarm=%d fan=%d pump=%d temp=%.1f",
             labguard_risk_level_to_string(risk->risk_level),
             risk->action_alarm,
             risk->action_fan,
             risk->action_pump,
             risk->temperature_c);
    return ESP_OK;
}

const labguard_risk_state_t *actuator_ctrl_get_last_risk(void)
{
    return &s_last_risk;
}
