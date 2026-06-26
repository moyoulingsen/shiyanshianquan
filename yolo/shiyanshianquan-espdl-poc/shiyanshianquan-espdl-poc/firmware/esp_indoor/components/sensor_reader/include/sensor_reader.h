#pragma once

#include "esp_err.h"
#include "driver/i2c_master.h"
#include "labguard_common.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t sensor_reader_init(void);
esp_err_t sensor_reader_read(labguard_sensor_data_t *out);
void sensor_reader_set_profile(labguard_profile_t profile);
labguard_profile_t sensor_reader_get_profile(void);

// Returns the shared I2C master bus initialized by sensor_reader_init().
// Used so the camera SCCB can attach to the same physical bus (GPIO7/8 on
// the ESP32-P4-Function HMI subboard). Returns NULL if sensor_reader_init
// has not been called or the bus failed to come up.
i2c_master_bus_handle_t sensor_reader_get_i2c_bus(void);

#ifdef __cplusplus
}
#endif
