#include "ui_lvgl.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "event_log.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "ui_lvgl";
static char s_last_summary[256];
static char s_dashboard[1024];

typedef struct {
    bool has_access;
    bool has_sensor;
    bool has_risk;
    bool indoor_online;
    bool admin_locked;
    labguard_ppe_result_t ppe;
    labguard_access_decision_t decision;
    labguard_sensor_data_t sensor;
    labguard_risk_state_t risk;
    labguard_risk_level_t indoor_risk_level;
    int64_t last_refresh_s;
} ui_dashboard_state_t;

static ui_dashboard_state_t s_state;

static int64_t now_seconds(void)
{
    return esp_timer_get_time() / 1000000;
}

static const char *yes_no(bool value)
{
    return value ? "yes" : "no";
}

static const char *ok_ng(bool value)
{
    return value ? "OK" : "NG";
}

static const char *access_text(void)
{
    if (!s_state.has_access) {
        return "waiting";
    }
    return s_state.decision.allow ? "PASS" : "BLOCK";
}

static const char *event_line(size_t index)
{
    const char *line = event_log_get_recent(index);
    if (line == NULL || line[0] == '\0') {
        return "-";
    }
    return line;
}

static float sensor_raw_temperature(void)
{
    if (!s_state.has_sensor) {
        return 0.0f;
    }
    return s_state.sensor.filtered ? s_state.sensor.temperature_raw_c : s_state.sensor.temperature_c;
}

static float sensor_raw_humidity(void)
{
    if (!s_state.has_sensor) {
        return 0.0f;
    }
    return s_state.sensor.filtered ? s_state.sensor.humidity_raw_rh : s_state.sensor.humidity_rh;
}

static int sensor_raw_voc(void)
{
    if (!s_state.has_sensor) {
        return 0;
    }
    return s_state.sensor.filtered ? s_state.sensor.voc_raw_index : s_state.sensor.voc_index;
}

static void render_dashboard(void)
{
    const labguard_ppe_result_t *ppe = &s_state.ppe;
    const labguard_access_decision_t *decision = &s_state.decision;
    const labguard_sensor_data_t *sensor = &s_state.sensor;
    const labguard_risk_state_t *risk = &s_state.risk;
    const char *reason = s_state.has_access ? labguard_access_reason_to_string(decision->reason) : "waiting";
    const char *door = (s_state.has_access && decision->door_lock != NULL) ? decision->door_lock : "-";
    labguard_risk_level_t level = s_state.has_risk ? risk->risk_level : s_state.indoor_risk_level;

    snprintf(s_dashboard,
             sizeof(s_dashboard),
             "LabGuard TwinP4 Safety Console\n"
             "ACCESS  %s  reason=%s  door=%s  admin_lock=%s\n"
             "PPE     person=%s  labcoat=%s  goggles=%s  score=%.2f/%.2f\n"
             "INDOOR  online=%s  risk=%s  smoke=%s  flame=%s  gas=%s\n"
             "SENSOR  T=%.1fC(raw %.1f)  RH=%.1f%%(raw %.1f)  VOC=%d(raw %d)  MQ2=%s\n"
             "ACTIONS alarm=%s  fan=%s  pump=%s  refreshed=%llds\n"
             "EVENT1  %s\n"
             "EVENT2  %s\n"
             "EVENT3  %s",
             access_text(),
             reason,
             door,
             yes_no(s_state.admin_locked),
             s_state.has_access ? yes_no(ppe->person_present) : "-",
             s_state.has_access ? ok_ng(ppe->labcoat) : "-",
             s_state.has_access ? ok_ng(ppe->goggles) : "-",
             s_state.has_access ? ppe->score_labcoat : 0.0f,
             s_state.has_access ? ppe->score_goggles : 0.0f,
             yes_no(s_state.indoor_online),
             labguard_risk_level_to_string(level),
             s_state.has_risk ? yes_no(risk->smoke) : "-",
             s_state.has_risk ? yes_no(risk->flame) : "-",
             s_state.has_risk ? yes_no(risk->gas_alarm) : "-",
             s_state.has_sensor ? sensor->temperature_c : 0.0f,
             sensor_raw_temperature(),
             s_state.has_sensor ? sensor->humidity_rh : 0.0f,
             sensor_raw_humidity(),
             s_state.has_sensor ? sensor->voc_index : 0,
             sensor_raw_voc(),
             s_state.has_sensor ? yes_no(sensor->mq2_alarm) : "-",
             s_state.has_risk ? yes_no(risk->action_alarm) : "-",
             s_state.has_risk ? yes_no(risk->action_fan) : "-",
             s_state.has_risk ? yes_no(risk->action_pump) : "-",
             s_state.last_refresh_s,
             event_line(0),
             event_line(1),
             event_line(2));
}

esp_err_t ui_lvgl_init(void)
{
    memset(&s_state, 0, sizeof(s_state));
    s_state.indoor_online = false;
    s_state.indoor_risk_level = LABGUARD_RISK_NORMAL;
    s_state.last_refresh_s = now_seconds();
    snprintf(s_last_summary, sizeof(s_last_summary), "UI ready");
    render_dashboard();
    ESP_LOGI(TAG, "LVGL dashboard state initialized");
    return ESP_OK;
}

esp_err_t ui_lvgl_update_indoor_sensor(const labguard_sensor_data_t *sensor)
{
    if (sensor == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    s_state.sensor = *sensor;
    s_state.has_sensor = true;
    s_state.last_refresh_s = now_seconds();
    render_dashboard();
    return ESP_OK;
}

esp_err_t ui_lvgl_update_indoor_risk(const labguard_risk_state_t *risk, bool online)
{
    if (risk == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    s_state.risk = *risk;
    s_state.has_risk = true;
    s_state.indoor_online = online;
    s_state.indoor_risk_level = risk->risk_level;
    s_state.last_refresh_s = now_seconds();
    render_dashboard();
    return ESP_OK;
}

esp_err_t ui_lvgl_set_indoor_online(bool online)
{
    s_state.indoor_online = online;
    s_state.last_refresh_s = now_seconds();
    render_dashboard();
    return ESP_OK;
}

esp_err_t ui_lvgl_set_admin_locked(bool locked)
{
    s_state.admin_locked = locked;
    s_state.last_refresh_s = now_seconds();
    render_dashboard();
    return ESP_OK;
}

esp_err_t ui_lvgl_update_access(const labguard_ppe_result_t *ppe,
                                const labguard_access_decision_t *decision,
                                labguard_risk_level_t indoor_risk_level)
{
    if (ppe == NULL || decision == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    s_state.ppe = *ppe;
    s_state.decision = *decision;
    s_state.has_access = true;
    s_state.indoor_risk_level = indoor_risk_level;
    s_state.last_refresh_s = now_seconds();
    render_dashboard();

    snprintf(s_last_summary,
             sizeof(s_last_summary),
             "access=%s person=%d coat=%d goggles=%d reason=%s indoor=%s temp=%.1f/raw=%.1f voc=%d/raw=%d",
             access_text(),
             ppe->person_present,
             ppe->labcoat,
             ppe->goggles,
             labguard_access_reason_to_string(decision->reason),
             labguard_risk_level_to_string(indoor_risk_level),
             s_state.has_sensor ? s_state.sensor.temperature_c : 0.0f,
             sensor_raw_temperature(),
             s_state.has_sensor ? s_state.sensor.voc_index : 0,
             sensor_raw_voc());

    ESP_LOGI(TAG, "%s", s_last_summary);
    return ESP_OK;
}

const char *ui_lvgl_get_last_summary(void)
{
    return s_last_summary;
}

const char *ui_lvgl_get_dashboard(void)
{
    return s_dashboard;
}
