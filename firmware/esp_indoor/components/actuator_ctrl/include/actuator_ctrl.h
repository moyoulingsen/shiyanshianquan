#pragma once

#include "esp_err.h"
#include "labguard_common.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t actuator_ctrl_init(void);
esp_err_t actuator_ctrl_apply_risk(const labguard_risk_state_t *risk);
const labguard_risk_state_t *actuator_ctrl_get_last_risk(void);

#ifdef __cplusplus
}
#endif
