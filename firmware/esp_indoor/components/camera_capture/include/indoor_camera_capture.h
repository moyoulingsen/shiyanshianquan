#pragma once

#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t indoor_camera_capture_init(void);
bool indoor_camera_capture_is_ready(void);

#ifdef __cplusplus
}
#endif
