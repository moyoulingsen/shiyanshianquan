#include "espdl_probe.h"

#include "esp_log.h"
#include "sdkconfig.h"

static const char *TAG = "espdl_probe";

#ifndef LABGUARD_ESPDL_PROBE_HAVE_RUNTIME
#define LABGUARD_ESPDL_PROBE_HAVE_RUNTIME 0
#endif

#if LABGUARD_ESPDL_PROBE_HAVE_RUNTIME
void espdl_probe_run_espdl(void);
#endif

void espdl_probe_run_once(void)
{
#if CONFIG_LABGUARD_ESPDL_PROBE_ENABLE
    ESP_LOGI(TAG, "isolated ESP-DL probe requested");
#if LABGUARD_ESPDL_PROBE_HAVE_RUNTIME
    espdl_probe_run_espdl();
#else
    ESP_LOGW(TAG, "ESP-DL runtime path is not available for the selected probe configuration");
#endif
#else
    ESP_LOGD(TAG, "isolated ESP-DL probe disabled");
#endif
}
