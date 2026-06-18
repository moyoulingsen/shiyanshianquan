#include "labguard_common.h"

#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "esp_timer.h"

static int64_t now_seconds(void)
{
    return esp_timer_get_time() / 1000000;
}

static void add_timestamp(cJSON *root, int64_t timestamp)
{
    cJSON_AddNumberToObject(root, "timestamp", timestamp > 0 ? timestamp : now_seconds());
}

static char *print_unformatted(cJSON *root)
{
    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return json;
}

static const char *json_get_string_or_default(const cJSON *root, const char *name, const char *fallback)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(root, name);
    if (cJSON_IsString(item) && item->valuestring != NULL) {
        return item->valuestring;
    }
    return fallback;
}

static bool json_get_bool_or_default(const cJSON *root, const char *name, bool fallback)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(root, name);
    if (cJSON_IsBool(item)) {
        return cJSON_IsTrue(item);
    }
    return fallback;
}

static int json_get_int_or_default(const cJSON *root, const char *name, int fallback)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(root, name);
    if (cJSON_IsNumber(item)) {
        return item->valueint;
    }
    return fallback;
}

static int64_t json_get_i64_or_default(const cJSON *root, const char *name, int64_t fallback)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(root, name);
    if (cJSON_IsNumber(item)) {
        return (int64_t)item->valuedouble;
    }
    return fallback;
}

static double json_get_double_or_default(const cJSON *root, const char *name, double fallback)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(root, name);
    if (cJSON_IsNumber(item)) {
        return item->valuedouble;
    }
    return fallback;
}

const char *labguard_node_to_string(labguard_node_t node)
{
    switch (node) {
    case LABGUARD_NODE_DEVICE:
        return "device";
    case LABGUARD_NODE_DASHBOARD:
        return "dashboard";
    case LABGUARD_NODE_UNKNOWN:
    default:
        return "unknown";
    }
}

labguard_node_t labguard_node_from_string(const char *node)
{
    if (node == NULL) {
        return LABGUARD_NODE_UNKNOWN;
    }

    if (strcmp(node, "device") == 0) {
        return LABGUARD_NODE_DEVICE;
    }
    if (strcmp(node, "dashboard") == 0) {
        return LABGUARD_NODE_DASHBOARD;
    }
    return LABGUARD_NODE_UNKNOWN;
}

const char *labguard_risk_level_to_string(labguard_risk_level_t level)
{
    switch (level) {
    case LABGUARD_RISK_NORMAL:
        return "normal";
    case LABGUARD_RISK_WARNING:
        return "warning";
    case LABGUARD_RISK_ALARM:
        return "alarm";
    case LABGUARD_RISK_EMERGENCY:
        return "emergency";
    default:
        return "unknown";
    }
}

labguard_risk_level_t labguard_risk_level_from_string(const char *level)
{
    if (level == NULL) {
        return LABGUARD_RISK_NORMAL;
    }

    if (strcmp(level, "normal") == 0) {
        return LABGUARD_RISK_NORMAL;
    }
    if (strcmp(level, "warning") == 0) {
        return LABGUARD_RISK_WARNING;
    }
    if (strcmp(level, "alarm") == 0) {
        return LABGUARD_RISK_ALARM;
    }
    if (strcmp(level, "emergency") == 0) {
        return LABGUARD_RISK_EMERGENCY;
    }
    return LABGUARD_RISK_NORMAL;
}

const char *labguard_access_reason_to_string(labguard_access_reason_t reason)
{
    switch (reason) {
    case LABGUARD_ACCESS_PASS:
        return "pass";
    case LABGUARD_ACCESS_CAMERA_ERROR:
        return "camera_error";
    case LABGUARD_ACCESS_MODEL_ERROR:
        return "model_error";
    default:
        return "unknown";
    }
}

const char *labguard_command_type_to_string(labguard_command_type_t type)
{
    switch (type) {
    case LABGUARD_CMD_RESET:
        return "reset";
    case LABGUARD_CMD_FAN_ON:
        return "fan_on";
    case LABGUARD_CMD_FAN_OFF:
        return "fan_off";
    case LABGUARD_CMD_PUMP_ON:
        return "pump_on";
    case LABGUARD_CMD_PUMP_OFF:
        return "pump_off";
    case LABGUARD_CMD_ALARM_ON:
        return "alarm_on";
    case LABGUARD_CMD_ALARM_OFF:
        return "alarm_off";
    case LABGUARD_CMD_NONE:
    default:
        return "none";
    }
}

labguard_command_type_t labguard_command_type_from_string(const char *type)
{
    if (type == NULL) {
        return LABGUARD_CMD_NONE;
    }

    if (strcmp(type, "reset") == 0) {
        return LABGUARD_CMD_RESET;
    }
    if (strcmp(type, "fan_on") == 0) {
        return LABGUARD_CMD_FAN_ON;
    }
    if (strcmp(type, "fan_off") == 0) {
        return LABGUARD_CMD_FAN_OFF;
    }
    if (strcmp(type, "pump_on") == 0) {
        return LABGUARD_CMD_PUMP_ON;
    }
    if (strcmp(type, "pump_off") == 0) {
        return LABGUARD_CMD_PUMP_OFF;
    }
    if (strcmp(type, "alarm_on") == 0) {
        return LABGUARD_CMD_ALARM_ON;
    }
    if (strcmp(type, "alarm_off") == 0) {
        return LABGUARD_CMD_ALARM_OFF;
    }
    return LABGUARD_CMD_NONE;
}

bool labguard_command_targets_node(const labguard_command_t *command, labguard_node_t node)
{
    if (command == NULL) {
        return false;
    }
    return command->target_node == LABGUARD_NODE_UNKNOWN || command->target_node == node;
}

char *labguard_status_to_json(const labguard_status_t *status)
{
    cJSON *root = cJSON_CreateObject();
    if (root == NULL || status == NULL) {
        cJSON_Delete(root);
        return NULL;
    }
    cJSON_AddStringToObject(root, "type", "status");
    cJSON_AddStringToObject(root, "node", labguard_node_to_string(status->node));
    cJSON_AddBoolToObject(root, "online", status->online);
    cJSON_AddNumberToObject(root, "uptime_s", status->uptime_s);
    cJSON_AddNumberToObject(root, "wifi_rssi", status->wifi_rssi);
    cJSON_AddStringToObject(root, "version", status->version ? status->version : "");
    add_timestamp(root, status->timestamp);
    return print_unformatted(root);
}

char *labguard_sensor_data_to_json(const labguard_sensor_data_t *data)
{
    cJSON *root = cJSON_CreateObject();
    if (root == NULL || data == NULL) {
        cJSON_Delete(root);
        return NULL;
    }
    cJSON_AddStringToObject(root, "type", "sensor");
    cJSON_AddNumberToObject(root, "temperature_c", data->temperature_c);
    cJSON_AddNumberToObject(root, "humidity_rh", data->humidity_rh);
    cJSON_AddNumberToObject(root, "voc_index", data->voc_index);
    cJSON_AddNumberToObject(root, "temperature_raw_c", data->temperature_raw_c);
    cJSON_AddNumberToObject(root, "humidity_raw_rh", data->humidity_raw_rh);
    cJSON_AddNumberToObject(root, "voc_raw_index", data->voc_raw_index);
    cJSON_AddBoolToObject(root, "mq2_alarm", data->mq2_alarm);
    cJSON_AddBoolToObject(root, "sensor_ok", data->sensor_ok);
    cJSON_AddBoolToObject(root, "filtered", data->filtered);
    add_timestamp(root, data->timestamp);
    return print_unformatted(root);
}

char *labguard_risk_state_to_json(const labguard_risk_state_t *state)
{
    cJSON *root = cJSON_CreateObject();
    if (root == NULL || state == NULL) {
        cJSON_Delete(root);
        return NULL;
    }
    cJSON_AddStringToObject(root, "type", "risk");
    cJSON_AddStringToObject(root, "risk_level", labguard_risk_level_to_string(state->risk_level));
    cJSON_AddStringToObject(root, "risk_text", state->risk_text ? state->risk_text : "");
    cJSON_AddBoolToObject(root, "smoke", state->smoke);
    cJSON_AddBoolToObject(root, "flame", state->flame);
    cJSON_AddBoolToObject(root, "gas_alarm", state->gas_alarm);
    cJSON_AddNumberToObject(root, "temperature_c", state->temperature_c);
    cJSON_AddBoolToObject(root, "action_alarm", state->action_alarm);
    cJSON_AddBoolToObject(root, "action_fan", state->action_fan);
    cJSON_AddBoolToObject(root, "action_pump", state->action_pump);
    cJSON_AddNumberToObject(root, "fan_level_pct", state->fan_level_pct);
    cJSON_AddNumberToObject(root, "pump_level_pct", state->pump_level_pct);
    cJSON_AddStringToObject(root, "model", state->model ? state->model : "");
    add_timestamp(root, state->timestamp);
    return print_unformatted(root);
}

char *labguard_command_to_json(const labguard_command_t *command)
{
    cJSON *root = cJSON_CreateObject();
    if (root == NULL || command == NULL) {
        cJSON_Delete(root);
        return NULL;
    }
    cJSON_AddStringToObject(root, "type", "command");
    cJSON_AddStringToObject(root, "command", labguard_command_type_to_string(command->type));
    cJSON_AddStringToObject(root, "target_node", labguard_node_to_string(command->target_node));
    cJSON_AddNumberToObject(root, "level_pct", command->level_pct);
    add_timestamp(root, command->timestamp);
    return print_unformatted(root);
}

char *labguard_event_to_json(const labguard_event_t *event)
{
    cJSON *root = cJSON_CreateObject();
    if (root == NULL || event == NULL) {
        cJSON_Delete(root);
        return NULL;
    }
    cJSON_AddStringToObject(root, "type", "event");
    cJSON_AddStringToObject(root, "node", labguard_node_to_string(event->node));
    cJSON_AddStringToObject(root, "risk_level", labguard_risk_level_to_string(event->level));
    cJSON_AddStringToObject(root, "source", event->source ? event->source : "");
    cJSON_AddStringToObject(root, "event", event->event ? event->event : "");
    cJSON_AddStringToObject(root, "actions", event->actions ? event->actions : "");
    add_timestamp(root, event->timestamp);
    return print_unformatted(root);
}

bool labguard_status_from_json(const char *json, labguard_status_t *status)
{
    (void)json;
    (void)status;
    return false;
}

bool labguard_sensor_data_from_json(const char *json, labguard_sensor_data_t *data)
{
    (void)json;
    (void)data;
    return false;
}

bool labguard_risk_state_from_json(const char *json, labguard_risk_state_t *state)
{
    (void)json;
    (void)state;
    return false;
}

bool labguard_event_from_json(const char *json, labguard_event_t *event)
{
    (void)json;
    (void)event;
    return false;
}

bool labguard_command_from_json(const char *json, labguard_command_t *command)
{
    if (json == NULL || command == NULL) {
        return false;
    }

    cJSON *root = cJSON_Parse(json);
    if (root == NULL) {
        return false;
    }

    command->type = labguard_command_type_from_string(json_get_string_or_default(root, "command", "none"));
    command->target_node = labguard_node_from_string(json_get_string_or_default(root, "target_node", "device"));
    command->level_pct = json_get_int_or_default(root, "level_pct", -1);
    command->timestamp = json_get_i64_or_default(root, "timestamp", now_seconds());

    cJSON_Delete(root);
    return true;
}
