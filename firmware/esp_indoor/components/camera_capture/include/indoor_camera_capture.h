#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    INDOOR_CAMERA_PIXEL_FORMAT_UNKNOWN = 0,
    INDOOR_CAMERA_PIXEL_FORMAT_RGB565,
} indoor_camera_pixel_format_t;

typedef struct {
    const void *data;
    size_t len;
    uint16_t width;
    uint16_t height;
    indoor_camera_pixel_format_t pixel_format;
    uint32_t sequence;
} indoor_camera_frame_t;

esp_err_t indoor_camera_capture_init(void);
bool indoor_camera_capture_is_ready(void);
esp_err_t indoor_camera_capture_get_latest_frame(indoor_camera_frame_t *out_frame);

#ifdef __cplusplus
}
#endif
