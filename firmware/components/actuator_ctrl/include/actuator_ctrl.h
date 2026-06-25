#pragma once

#include <stdbool.h>

#include "esp_err.h"
#include "labguard_common.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t actuator_ctrl_init(void);
esp_err_t actuator_ctrl_apply_risk(const labguard_risk_state_t *risk);
esp_err_t actuator_ctrl_set_fan(bool on);
esp_err_t actuator_ctrl_set_fan_level(int level_pct);
esp_err_t actuator_ctrl_set_pump(bool on);
esp_err_t actuator_ctrl_set_pump_level(int level_pct);
esp_err_t actuator_ctrl_set_light(bool on);
bool actuator_ctrl_get_light(void);
const labguard_risk_state_t *actuator_ctrl_get_last_risk(void);

#ifdef __cplusplus
}
#endif
