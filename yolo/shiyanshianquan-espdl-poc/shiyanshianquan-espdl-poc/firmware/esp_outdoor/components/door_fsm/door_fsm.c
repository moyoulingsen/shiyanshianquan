#include "door_fsm.h"

#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "door_fsm";
static door_fsm_state_t s_state = DOOR_FSM_IDLE;

esp_err_t door_fsm_init(void)
{
    s_state = DOOR_FSM_IDLE;
    ESP_LOGI(TAG, "door FSM initialized");
    return ESP_OK;
}

esp_err_t door_fsm_evaluate(const labguard_ppe_result_t *ppe,
                            bool indoor_online,
                            labguard_risk_level_t indoor_risk_level,
                            labguard_access_decision_t *out)
{
    if (ppe == NULL || out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    out->allow = false;
    out->door_lock = "locked";
    out->indoor_risk_level = indoor_risk_level;
    out->timestamp = esp_timer_get_time() / 1000000;

    if (!indoor_online) {
        s_state = DOOR_FSM_LOCKED_BY_INDOOR;
        out->reason = LABGUARD_ACCESS_INDOOR_OFFLINE;
        return ESP_OK;
    }

    if (indoor_risk_level >= LABGUARD_RISK_ALARM) {
        s_state = DOOR_FSM_LOCKED_BY_INDOOR;
        out->reason = LABGUARD_ACCESS_INDOOR_DANGER;
        return ESP_OK;
    }

    if (indoor_risk_level == LABGUARD_RISK_WARNING) {
        s_state = DOOR_FSM_ACCESS_DENIED;
        out->reason = LABGUARD_ACCESS_INDOOR_WARNING;
        return ESP_OK;
    }

    if (!ppe->person_present) {
        s_state = DOOR_FSM_IDLE;
        out->reason = LABGUARD_ACCESS_NO_PERSON;
        return ESP_OK;
    }

    if (!ppe->labcoat && !ppe->goggles) {
        s_state = DOOR_FSM_ACCESS_DENIED;
        out->reason = LABGUARD_ACCESS_MISSING_PPE;
        return ESP_OK;
    }

    if (!ppe->labcoat) {
        s_state = DOOR_FSM_ACCESS_DENIED;
        out->reason = LABGUARD_ACCESS_MISSING_LABCOAT;
        return ESP_OK;
    }

    if (!ppe->goggles) {
        s_state = DOOR_FSM_ACCESS_DENIED;
        out->reason = LABGUARD_ACCESS_MISSING_GOGGLES;
        return ESP_OK;
    }

    s_state = DOOR_FSM_ACCESS_GRANTED;
    out->allow = true;
    out->reason = LABGUARD_ACCESS_PASS;
    out->door_lock = "unlocked";
    return ESP_OK;
}

door_fsm_state_t door_fsm_get_state(void)
{
    return s_state;
}

