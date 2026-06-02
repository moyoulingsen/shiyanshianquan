#include "event_log.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"

#define EVENT_LOG_RECENT_MAX 5

static const char *TAG = "event_log";
static const char *s_mount_path;
static char s_latest_line[256];
static char s_recent_lines[EVENT_LOG_RECENT_MAX][256];
static size_t s_recent_count;
static size_t s_recent_next;

static void update_latest(const char *line)
{
    if (line == NULL) {
        s_latest_line[0] = '\0';
        return;
    }

    snprintf(s_latest_line, sizeof(s_latest_line), "%s", line);
    snprintf(s_recent_lines[s_recent_next], sizeof(s_recent_lines[s_recent_next]), "%s", line);
    s_recent_next = (s_recent_next + 1U) % EVENT_LOG_RECENT_MAX;
    if (s_recent_count < EVENT_LOG_RECENT_MAX) {
        s_recent_count++;
    }
}

esp_err_t event_log_init(const char *mount_path)
{
    s_mount_path = mount_path;
    s_latest_line[0] = '\0';
    memset(s_recent_lines, 0, sizeof(s_recent_lines));
    s_recent_count = 0;
    s_recent_next = 0;
    ESP_LOGI(TAG, "event log initialized at %s", s_mount_path ? s_mount_path : "(memory)");
    return ESP_OK;
}

esp_err_t event_log_append(const char *source, int level, const char *event, const char *actions)
{
    labguard_event_t log_event = {
        .node = LABGUARD_NODE_UNKNOWN,
        .level = (labguard_risk_level_t)level,
        .source = source,
        .event = event,
        .actions = actions,
        .timestamp = esp_timer_get_time() / 1000000,
    };
    return event_log_append_event(&log_event);
}

esp_err_t event_log_append_event(const labguard_event_t *event)
{
    if (event == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    char line[256];
    snprintf(line,
             sizeof(line),
             "time=%lld node=%s source=%s level=%s event=%s actions=%s",
             event->timestamp > 0 ? event->timestamp : esp_timer_get_time() / 1000000,
             labguard_node_to_string(event->node),
             event->source ? event->source : labguard_node_to_string(event->node),
             labguard_risk_level_to_string(event->level),
             event->event ? event->event : "unknown",
             event->actions ? event->actions : "");

    update_latest(line);
    ESP_LOGI(TAG, "%s", line);
    return ESP_OK;
}

const char *event_log_get_latest(void)
{
    return s_latest_line;
}

const char *event_log_get_recent(size_t index)
{
    if (index >= s_recent_count) {
        return "";
    }

    size_t newest = (s_recent_next + EVENT_LOG_RECENT_MAX - 1U) % EVENT_LOG_RECENT_MAX;
    size_t slot = (newest + EVENT_LOG_RECENT_MAX - index) % EVENT_LOG_RECENT_MAX;
    return s_recent_lines[slot];
}

size_t event_log_get_recent_count(void)
{
    return s_recent_count;
}
