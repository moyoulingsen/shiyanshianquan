#include <cstdint>
#include <new>

#include "dl_model_base.hpp"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "sdkconfig.h"

static const char *TAG = "espdl_probe";

#if CONFIG_LABGUARD_ESPDL_PROBE_LOAD_RODATA
extern const uint8_t model_espdl[] asm("_binary_model_espdl_start");
#endif

static void run_probe_model(dl::Model *model, int64_t load_start_us)
{
    if (model == nullptr) {
        ESP_LOGE(TAG, "failed to allocate dl::Model");
        return;
    }

    ESP_LOGI(TAG, "dl::Model loaded in %lld us", (long long)(esp_timer_get_time() - load_start_us));

    const int64_t test_start_us = esp_timer_get_time();
    const esp_err_t test_ret = model->test();
    const int64_t test_elapsed_us = esp_timer_get_time() - test_start_us;
    if (test_ret == ESP_OK) {
        ESP_LOGI(TAG, "model->test() passed in %lld us", (long long)test_elapsed_us);
    } else {
        ESP_LOGE(TAG, "model->test() failed: %s, elapsed=%lld us",
                 esp_err_to_name(test_ret),
                 (long long)test_elapsed_us);
    }

    ESP_LOGI(TAG, "model->profile_memory()");
    model->profile_memory();

    ESP_LOGI(TAG, "model->profile_module(sort_by_latency=%d)",
             CONFIG_LABGUARD_ESPDL_PROBE_SORT_PROFILE_BY_LATENCY);
    model->profile_module(CONFIG_LABGUARD_ESPDL_PROBE_SORT_PROFILE_BY_LATENCY);

    delete model;
}

extern "C" void espdl_probe_run_espdl(void)
{
#if CONFIG_LABGUARD_ESPDL_PROBE_LOAD_RODATA
    ESP_LOGI(TAG, "loading .espdl model from rodata");
    const int64_t load_start_us = esp_timer_get_time();
    dl::Model *model = new (std::nothrow) dl::Model((const char *)model_espdl,
                                                     fbs::MODEL_LOCATION_IN_FLASH_RODATA);
    run_probe_model(model, load_start_us);
#elif CONFIG_LABGUARD_ESPDL_PROBE_LOAD_SDCARD
    const char *model_path = CONFIG_LABGUARD_ESPDL_PROBE_SDCARD_PATH;
    if (model_path == nullptr || model_path[0] == '\0') {
        ESP_LOGE(TAG, "SD card model path is empty");
        return;
    }

    ESP_LOGI(TAG, "loading .espdl model from SD card path: %s", model_path);
    const int64_t load_start_us = esp_timer_get_time();
    dl::Model *model = new (std::nothrow) dl::Model(model_path,
                                                     fbs::MODEL_LOCATION_IN_SDCARD);
    run_probe_model(model, load_start_us);
#else
    ESP_LOGW(TAG, "no ESP-DL probe load mode selected");
#endif
}
