#include "led_status.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"

static const char *TAG = "led_status";
static char s_last_color[16];

esp_err_t led_status_init(void)
{
    snprintf(s_last_color, sizeof(s_last_color), "blue");
    ESP_LOGI(TAG, "LED status initialized");
    return ESP_OK;
}

esp_err_t led_status_apply_access(const labguard_access_decision_t *decision)
{
    if (decision == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (decision->allow) {
        snprintf(s_last_color, sizeof(s_last_color), "green");
    } else if (decision->reason == LABGUARD_ACCESS_INDOOR_DANGER || decision->reason == LABGUARD_ACCESS_INDOOR_OFFLINE) {
        snprintf(s_last_color, sizeof(s_last_color), "red");
    } else if (decision->reason == LABGUARD_ACCESS_NO_PERSON) {
        snprintf(s_last_color, sizeof(s_last_color), "blue");
    } else {
        snprintf(s_last_color, sizeof(s_last_color), "yellow");
    }

    ESP_LOGI(TAG, "LED color=%s reason=%s", s_last_color, labguard_access_reason_to_string(decision->reason));
    return ESP_OK;
}

const char *led_status_get_last_color(void)
{
    return s_last_color;
}
