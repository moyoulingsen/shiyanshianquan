#pragma once

#include "esp_err.h"
#include "labguard_common.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t led_status_init(void);
esp_err_t led_status_apply_access(const labguard_access_decision_t *decision);
const char *led_status_get_last_color(void);

#ifdef __cplusplus
}
#endif
