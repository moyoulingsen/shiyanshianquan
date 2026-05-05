#include "indoor_camera_capture.h"

#include "esp_log.h"

static const char *TAG = "indoor_camera";
static bool s_ready;

esp_err_t indoor_camera_capture_init(void)
{
    s_ready = true;
    ESP_LOGI(TAG, "indoor camera abstraction initialized");
    return ESP_OK;
}

bool indoor_camera_capture_is_ready(void)
{
    return s_ready;
}
