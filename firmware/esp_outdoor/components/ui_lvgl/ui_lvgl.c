#include "ui_lvgl.h"

#include <stdio.h>

#include "esp_log.h"

static const char *TAG = "ui_lvgl";
static char s_last_summary[256];

esp_err_t ui_lvgl_init(void)
{
    snprintf(s_last_summary, sizeof(s_last_summary), "UI ready");
    ESP_LOGI(TAG, "LVGL summary UI initialized");
    return ESP_OK;
}

esp_err_t ui_lvgl_update_access(const labguard_ppe_result_t *ppe,
                                const labguard_access_decision_t *decision,
                                labguard_risk_level_t indoor_risk_level)
{
    if (ppe == NULL || decision == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    snprintf(s_last_summary,
             sizeof(s_last_summary),
             "person=%d coat=%d goggles=%d allow=%d reason=%s indoor=%s",
             ppe->person_present,
             ppe->labcoat,
             ppe->goggles,
             decision->allow,
             labguard_access_reason_to_string(decision->reason),
             labguard_risk_level_to_string(indoor_risk_level));

    ESP_LOGI(TAG, "%s", s_last_summary);
    return ESP_OK;
}

const char *ui_lvgl_get_last_summary(void)
{
    return s_last_summary;
}
