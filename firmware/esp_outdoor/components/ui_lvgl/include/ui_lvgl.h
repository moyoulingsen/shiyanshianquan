#pragma once

#include "esp_err.h"
#include "labguard_common.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t ui_lvgl_init(void);
esp_err_t ui_lvgl_update_access(const labguard_ppe_result_t *ppe,
                                const labguard_access_decision_t *decision,
                                labguard_risk_level_t indoor_risk_level);
const char *ui_lvgl_get_last_summary(void);

#ifdef __cplusplus
}
#endif
