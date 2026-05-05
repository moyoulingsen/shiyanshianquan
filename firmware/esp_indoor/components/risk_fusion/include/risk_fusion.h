#pragma once

#include "esp_err.h"
#include "hazard_infer.h"
#include "labguard_common.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t risk_fusion_init(void);
esp_err_t risk_fusion_evaluate(const labguard_sensor_data_t *sensor,
                               const labguard_hazard_result_t *hazard,
                               labguard_risk_state_t *out);

#ifdef __cplusplus
}
#endif

