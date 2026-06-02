#pragma once

#include <stddef.h>

#include "labguard_common.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t event_log_init(const char *mount_path);
esp_err_t event_log_append(const char *source, int level, const char *event, const char *actions);
esp_err_t event_log_append_event(const labguard_event_t *event);
const char *event_log_get_latest(void);
const char *event_log_get_recent(size_t index);
size_t event_log_get_recent_count(void);

#ifdef __cplusplus
}
#endif
