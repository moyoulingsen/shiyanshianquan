#pragma once

#include "esp_err.h"
#include "labguard_common.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t sensor_reader_init(void);
esp_err_t sensor_reader_read(labguard_sensor_data_t *out);
void sensor_reader_set_profile(labguard_profile_t profile);
labguard_profile_t sensor_reader_get_profile(void);

#ifdef __cplusplus
}
#endif
