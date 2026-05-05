#pragma once

#include <stddef.h>

#include "esp_err.h"
#include "labguard_common.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t ppe_infer_init(void);
esp_err_t ppe_infer_run(const void *frame, size_t frame_len, labguard_ppe_result_t *out);
void ppe_infer_set_profile(labguard_profile_t profile);
labguard_profile_t ppe_infer_get_profile(void);

#ifdef __cplusplus
}
#endif
