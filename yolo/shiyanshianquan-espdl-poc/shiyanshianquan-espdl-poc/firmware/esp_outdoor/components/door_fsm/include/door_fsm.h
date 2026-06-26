#pragma once

#include <stdbool.h>

#include "esp_err.h"
#include "labguard_common.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    DOOR_FSM_IDLE = 0,
    DOOR_FSM_PERSON_DETECTED,
    DOOR_FSM_CHECK_INDOOR_STATE,
    DOOR_FSM_PPE_INFER,
    DOOR_FSM_ACCESS_GRANTED,
    DOOR_FSM_ACCESS_DENIED,
    DOOR_FSM_LOCKED_BY_INDOOR,
} door_fsm_state_t;

esp_err_t door_fsm_init(void);
esp_err_t door_fsm_evaluate(const labguard_ppe_result_t *ppe,
                            bool indoor_online,
                            labguard_risk_level_t indoor_risk_level,
                            labguard_access_decision_t *out);
door_fsm_state_t door_fsm_get_state(void);

#ifdef __cplusplus
}
#endif

