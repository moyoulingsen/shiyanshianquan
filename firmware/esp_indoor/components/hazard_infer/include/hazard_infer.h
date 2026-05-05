#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"
#include "labguard_common.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool smoke;
    bool flame;
    float score_smoke;
    float score_flame;
    const char *model;
} labguard_hazard_result_t;

esp_err_t hazard_infer_init(void);
esp_err_t hazard_infer_run(const void *frame, size_t frame_len, labguard_hazard_result_t *out);
void hazard_infer_set_profile(labguard_profile_t profile);
labguard_profile_t hazard_infer_get_profile(void);

#ifdef __cplusplus
}
#endif
