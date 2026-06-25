#pragma once

#include "esp_err.h"
#include "driver/i2c_master.h"
#include "labguard_common.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t sensor_reader_init(void);
esp_err_t sensor_reader_read(labguard_sensor_data_t *out);

// Returns the I2C master bus initialized by sensor_reader_init().
// This bus is reserved for SHT3x/ENS160 sensors. The HMI subboard SC2336
// camera uses its own SCCB bus on the Espressif reference pins.
i2c_master_bus_handle_t sensor_reader_get_i2c_bus(void);

#ifdef __cplusplus
}
#endif
