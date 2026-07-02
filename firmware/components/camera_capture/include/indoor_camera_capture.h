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

typedef enum {
    INDOOR_CAMERA_STATE_IDLE = 0,
    INDOOR_CAMERA_STATE_INIT_MIPI_LDO,
    INDOOR_CAMERA_STATE_INIT_DSI_PANEL,
    INDOOR_CAMERA_STATE_INIT_SCCB,
    INDOOR_CAMERA_STATE_DETECT_SENSOR,
    INDOOR_CAMERA_STATE_INIT_CSI_ISP,
    INDOOR_CAMERA_STATE_RESET_PANEL,
    INDOOR_CAMERA_STATE_START_CSI,
    INDOOR_CAMERA_STATE_INIT_PANEL,
    INDOOR_CAMERA_STATE_ENABLE_BACKLIGHT,
    INDOOR_CAMERA_STATE_START_TASK,
    INDOOR_CAMERA_STATE_WAITING_FIRST_FRAME,
    INDOOR_CAMERA_STATE_STREAMING,
    INDOOR_CAMERA_STATE_INIT_FAILED,
} indoor_camera_state_t;

typedef struct {
    indoor_camera_state_t state;
    esp_err_t last_error;
    bool ready;
    bool first_frame_received;
    uint32_t latest_sequence;
} indoor_camera_status_t;

esp_err_t indoor_camera_capture_init(void);
bool indoor_camera_capture_is_ready(void);
esp_err_t indoor_camera_capture_get_latest_frame(indoor_camera_frame_t *out_frame);
void indoor_camera_capture_get_status(indoor_camera_status_t *out_status);
indoor_camera_state_t indoor_camera_capture_get_state(void);
esp_err_t indoor_camera_capture_get_last_error(void);
bool indoor_camera_capture_has_received_first_frame(void);
const char *indoor_camera_capture_state_to_string(indoor_camera_state_t state);

#ifdef __cplusplus
}
#endif
