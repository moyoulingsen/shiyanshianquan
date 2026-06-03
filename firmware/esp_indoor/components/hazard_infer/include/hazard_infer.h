#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "labguard_common.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    LABGUARD_HAZARD_BACKEND_MOCK = 0,
    LABGUARD_HAZARD_BACKEND_MODEL_PLACEHOLDER,
    LABGUARD_HAZARD_BACKEND_ONBOARD_PENDING,
    LABGUARD_HAZARD_BACKEND_ESPDL_SDCARD,
} labguard_hazard_backend_t;

typedef struct {
    bool smoke;
    bool flame;
    float score_smoke;
    float score_flame;
    uint16_t detection_count;
    const char *model;
} labguard_hazard_result_t;

typedef struct {
    uint16_t input_width;
    uint16_t input_height;
    size_t workspace_bytes;
    bool model_file_present;
    bool runtime_available;
} hazard_infer_runtime_info_t;

esp_err_t hazard_infer_init(void);
esp_err_t hazard_infer_run(const void *frame,
                           size_t frame_len,
                           uint16_t frame_width,
                           uint16_t frame_height,
                           labguard_hazard_result_t *out);
void hazard_infer_set_profile(labguard_profile_t profile);
labguard_profile_t hazard_infer_get_profile(void);
labguard_hazard_backend_t hazard_infer_get_backend(void);
bool hazard_infer_model_is_ready(void);
const hazard_infer_runtime_info_t *hazard_infer_get_runtime_info(void);

#ifdef __cplusplus
}
#endif
