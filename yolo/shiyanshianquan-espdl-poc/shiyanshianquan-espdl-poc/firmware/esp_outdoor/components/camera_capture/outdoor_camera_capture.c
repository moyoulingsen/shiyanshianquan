#include "outdoor_camera_capture.h"

#include "esp_log.h"

static const char *TAG = "outdoor_camera";
static bool s_ready;

esp_err_t outdoor_camera_capture_init(void)
{
    s_ready = true;
    ESP_LOGI(TAG, "outdoor camera abstraction initialized");
    return ESP_OK;
}

bool outdoor_camera_capture_is_ready(void)
{
    return s_ready;
}
