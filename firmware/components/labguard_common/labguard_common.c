#include "labguard_common.h"

#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "esp_timer.h"

static char s_parsed_event_source[48];
static char s_parsed_event_name[96];
static char s_parsed_event_actions[96];

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
    case LABGUARD_NODE_OUTDOOR:
        return "outdoor";
    case LABGUARD_NODE_INDOOR:
        return "indoor";
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

    if (strcmp(node, "outdoor") == 0) {
        return LABGUARD_NODE_OUTDOOR;
    }
    if (strcmp(node, "indoor") == 0) {
        return LABGUARD_NODE_INDOOR;
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
    case LABGUARD_ACCESS_NO_PERSON:
        return "no_person";
    case LABGUARD_ACCESS_MISSING_LABCOAT:
        return "missing_labcoat";
    case LABGUARD_ACCESS_MISSING_GOGGLES:
        return "missing_goggles";
    case LABGUARD_ACCESS_MISSING_PPE:
        return "missing_ppe";
    case LABGUARD_ACCESS_INDOOR_WARNING:
        return "indoor_warning";
    case LABGUARD_ACCESS_INDOOR_DANGER:
        return "indoor_danger";
    case LABGUARD_ACCESS_INDOOR_OFFLINE:
        return "indoor_offline";
    case LABGUARD_ACCESS_CAMERA_ERROR:
        return "camera_error";
    case LABGUARD_ACCESS_MODEL_ERROR:
        return "model_error";
    case LABGUARD_ACCESS_ADMIN_LOCKED:
        return "admin_locked";
    default:
        return "unknown";
    }
}

const char *labguard_command_type_to_string(labguard_command_type_t type)
{
    switch (type) {
    case LABGUARD_CMD_RESET:
        return "reset";
    case LABGUARD_CMD_SELFTEST:
        return "selftest";
    case LABGUARD_CMD_ADMIN_LOCK:
        return "admin_lock";
    case LABGUARD_CMD_ADMIN_UNLOCK:
        return "admin_unlock";
    case LABGUARD_CMD_FORCE_NORMAL:
        return "force_normal";
    case LABGUARD_CMD_FORCE_WARNING:
        return "force_warning";
    case LABGUARD_CMD_FORCE_ALARM:
        return "force_alarm";
    case LABGUARD_CMD_FORCE_EMERGENCY:
        return "force_emergency";
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
    if (strcmp(type, "selftest") == 0) {
        return LABGUARD_CMD_SELFTEST;
    }
    if (strcmp(type, "admin_lock") == 0) {
        return LABGUARD_CMD_ADMIN_LOCK;
    }
    if (strcmp(type, "admin_unlock") == 0) {
        return LABGUARD_CMD_ADMIN_UNLOCK;
    }
    if (strcmp(type, "force_normal") == 0) {
        return LABGUARD_CMD_FORCE_NORMAL;
    }
    if (strcmp(type, "force_warning") == 0) {
        return LABGUARD_CMD_FORCE_WARNING;
    }
    if (strcmp(type, "force_alarm") == 0) {
        return LABGUARD_CMD_FORCE_ALARM;
    }
    if (strcmp(type, "force_emergency") == 0) {
        return LABGUARD_CMD_FORCE_EMERGENCY;
    }
    return LABGUARD_CMD_NONE;
}

const char *labguard_profile_to_string(labguard_profile_t profile)
{
    switch (profile) {
    case LABGUARD_PROFILE_NORMAL:
        return "normal";
    case LABGUARD_PROFILE_WARNING:
        return "warning";
    case LABGUARD_PROFILE_ALARM:
        return "alarm";
    case LABGUARD_PROFILE_EMERGENCY:
        return "emergency";
    case LABGUARD_PROFILE_AUTO:
    default:
        return "auto";
    }
}

labguard_profile_t labguard_profile_from_string(const char *profile)
{
    if (profile == NULL) {
        return LABGUARD_PROFILE_AUTO;
    }

    if (strcmp(profile, "normal") == 0) {
        return LABGUARD_PROFILE_NORMAL;
    }
    if (strcmp(profile, "warning") == 0) {
        return LABGUARD_PROFILE_WARNING;
    }
    if (strcmp(profile, "alarm") == 0) {
        return LABGUARD_PROFILE_ALARM;
    }
    if (strcmp(profile, "emergency") == 0) {
        return LABGUARD_PROFILE_EMERGENCY;
    }
    return LABGUARD_PROFILE_AUTO;
}

labguard_risk_level_t labguard_profile_to_risk_level(labguard_profile_t profile)
{
    switch (profile) {
    case LABGUARD_PROFILE_WARNING:
        return LABGUARD_RISK_WARNING;
    case LABGUARD_PROFILE_ALARM:
        return LABGUARD_RISK_ALARM;
    case LABGUARD_PROFILE_EMERGENCY:
        return LABGUARD_RISK_EMERGENCY;
    case LABGUARD_PROFILE_NORMAL:
    case LABGUARD_PROFILE_AUTO:
    default:
        return LABGUARD_RISK_NORMAL;
    }
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
    cJSON_AddStringToObject(root, "node", labguard_node_to_string(status->node));
    cJSON_AddStringToObject(root, "type", "status");
    cJSON_AddBoolToObject(root, "online", status->online);
    cJSON_AddNumberToObject(root, "uptime_s", status->uptime_s);
    cJSON_AddNumberToObject(root, "wifi_rssi", status->wifi_rssi);
    cJSON_AddStringToObject(root, "version", status->version ? status->version : LABGUARD_VERSION);
    add_timestamp(root, status->timestamp);
    return print_unformatted(root);
}

char *labguard_ppe_result_to_json(const labguard_ppe_result_t *result)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "node", "outdoor");
    cJSON_AddStringToObject(root, "type", "ppe_result");
    cJSON_AddBoolToObject(root, "person_present", result->person_present);
    cJSON_AddBoolToObject(root, "labcoat", result->labcoat);
    cJSON_AddBoolToObject(root, "goggles", result->goggles);
    cJSON_AddNumberToObject(root, "score_labcoat", result->score_labcoat);
    cJSON_AddNumberToObject(root, "score_goggles", result->score_goggles);
    cJSON_AddStringToObject(root, "model", result->model ? result->model : "ppe_sim_v1");
    add_timestamp(root, result->timestamp);
    return print_unformatted(root);
}

char *labguard_sensor_data_to_json(const labguard_sensor_data_t *data)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "node", "indoor");
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
    cJSON_AddStringToObject(root, "node", "indoor");
    cJSON_AddStringToObject(root, "type", "risk_state");
    cJSON_AddNumberToObject(root, "risk_level", state->risk_level);
    cJSON_AddStringToObject(root, "risk_text", state->risk_text ? state->risk_text : labguard_risk_level_to_string(state->risk_level));
    cJSON_AddBoolToObject(root, "smoke", state->smoke);
    cJSON_AddBoolToObject(root, "flame", state->flame);
    cJSON_AddBoolToObject(root, "gas_alarm", state->gas_alarm);
    cJSON_AddNumberToObject(root, "temperature_c", state->temperature_c);

    cJSON *actions = cJSON_AddArrayToObject(root, "actions");
    if (state->action_alarm) {
        cJSON_AddItemToArray(actions, cJSON_CreateString("alarm_on"));
    }
    if (state->action_fan) {
        cJSON_AddItemToArray(actions, cJSON_CreateString("fan_on"));
    }
    if (state->action_pump) {
        cJSON_AddItemToArray(actions, cJSON_CreateString("pump_on"));
    }

    cJSON_AddStringToObject(root, "model", state->model ? state->model : "hazard_sim_v1");
    add_timestamp(root, state->timestamp);
    return print_unformatted(root);
}

char *labguard_access_decision_to_json(const labguard_access_decision_t *decision)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "node", "outdoor");
    cJSON_AddStringToObject(root, "type", "access_decision");
    cJSON_AddBoolToObject(root, "allow", decision->allow);
    cJSON_AddStringToObject(root, "reason", labguard_access_reason_to_string(decision->reason));
    cJSON_AddStringToObject(root, "door_lock", decision->door_lock ? decision->door_lock : (decision->allow ? "unlocked" : "locked"));
    cJSON_AddNumberToObject(root, "indoor_risk_level", decision->indoor_risk_level);
    add_timestamp(root, decision->timestamp);
    return print_unformatted(root);
}

char *labguard_command_to_json(const labguard_command_t *command)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "node", "dashboard");
    cJSON_AddStringToObject(root, "type", "command");
    cJSON_AddStringToObject(root, "command", labguard_command_type_to_string(command->type));
    cJSON_AddStringToObject(root, "target_node", labguard_node_to_string(command->target_node));
    add_timestamp(root, command->timestamp);
    return print_unformatted(root);
}

char *labguard_event_to_json(const labguard_event_t *event)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "node", labguard_node_to_string(event->node));
    cJSON_AddStringToObject(root, "type", "event");
    cJSON_AddNumberToObject(root, "risk_level", event->level);
    cJSON_AddStringToObject(root, "risk_text", labguard_risk_level_to_string(event->level));
    cJSON_AddStringToObject(root, "source", event->source ? event->source : labguard_node_to_string(event->node));
    cJSON_AddStringToObject(root, "event", event->event ? event->event : "unknown");
    cJSON_AddStringToObject(root, "actions", event->actions ? event->actions : "");
    add_timestamp(root, event->timestamp);
    return print_unformatted(root);
}

bool labguard_status_from_json(const char *json, labguard_status_t *status)
{
    if (json == NULL || status == NULL) {
        return false;
    }

    cJSON *root = cJSON_Parse(json);
    if (root == NULL) {
        return false;
    }

    status->node = labguard_node_from_string(json_get_string_or_default(root, "node", "unknown"));
    status->online = json_get_bool_or_default(root, "online", false);
    status->uptime_s = json_get_i64_or_default(root, "uptime_s", 0);
    status->wifi_rssi = json_get_int_or_default(root, "wifi_rssi", 0);
    status->version = "remote";
    status->timestamp = json_get_i64_or_default(root, "timestamp", 0);

    cJSON_Delete(root);
    return true;
}

bool labguard_sensor_data_from_json(const char *json, labguard_sensor_data_t *data)
{
    if (json == NULL || data == NULL) {
        return false;
    }

    cJSON *root = cJSON_Parse(json);
    if (root == NULL) {
        return false;
    }

    data->temperature_c = (float)json_get_double_or_default(root, "temperature_c", 0.0);
    data->humidity_rh = (float)json_get_double_or_default(root, "humidity_rh", 0.0);
    data->voc_index = json_get_int_or_default(root, "voc_index", 0);
    data->temperature_raw_c = (float)json_get_double_or_default(root, "temperature_raw_c", data->temperature_c);
    data->humidity_raw_rh = (float)json_get_double_or_default(root, "humidity_raw_rh", data->humidity_rh);
    data->voc_raw_index = json_get_int_or_default(root, "voc_raw_index", data->voc_index);
    data->mq2_alarm = json_get_bool_or_default(root, "mq2_alarm", false);
    data->sensor_ok = json_get_bool_or_default(root, "sensor_ok", false);
    data->filtered = json_get_bool_or_default(root, "filtered", false);
    data->timestamp = json_get_i64_or_default(root, "timestamp", 0);

    cJSON_Delete(root);
    return true;
}

bool labguard_risk_state_from_json(const char *json, labguard_risk_state_t *state)
{
    if (json == NULL || state == NULL) {
        return false;
    }

    cJSON *root = cJSON_Parse(json);
    if (root == NULL) {
        return false;
    }

    const cJSON *risk_level = cJSON_GetObjectItemCaseSensitive(root, "risk_level");
    if (cJSON_IsNumber(risk_level)) {
        state->risk_level = (labguard_risk_level_t)risk_level->valueint;
    } else {
        state->risk_level = labguard_risk_level_from_string(json_get_string_or_default(root, "risk_text", "normal"));
    }

    state->risk_text = labguard_risk_level_to_string(state->risk_level);
    state->smoke = json_get_bool_or_default(root, "smoke", false);
    state->flame = json_get_bool_or_default(root, "flame", false);
    state->gas_alarm = json_get_bool_or_default(root, "gas_alarm", false);
    state->temperature_c = (float)json_get_double_or_default(root, "temperature_c", 0.0);
    state->action_alarm = false;
    state->action_fan = false;
    state->action_pump = false;

    const cJSON *actions = cJSON_GetObjectItemCaseSensitive(root, "actions");
    if (cJSON_IsArray(actions)) {
        cJSON *action = NULL;
        cJSON_ArrayForEach(action, actions) {
            if (!cJSON_IsString(action) || action->valuestring == NULL) {
                continue;
            }
            if (strcmp(action->valuestring, "alarm_on") == 0) {
                state->action_alarm = true;
            } else if (strcmp(action->valuestring, "fan_on") == 0) {
                state->action_fan = true;
            } else if (strcmp(action->valuestring, "pump_on") == 0) {
                state->action_pump = true;
            }
        }
    }

    state->model = "hazard_remote";
    state->timestamp = json_get_i64_or_default(root, "timestamp", 0);

    cJSON_Delete(root);
    return true;
}

bool labguard_event_from_json(const char *json, labguard_event_t *event)
{
    if (json == NULL || event == NULL) {
        return false;
    }

    cJSON *root = cJSON_Parse(json);
    if (root == NULL) {
        return false;
    }

    const cJSON *risk_level = cJSON_GetObjectItemCaseSensitive(root, "risk_level");
    if (cJSON_IsNumber(risk_level)) {
        event->level = (labguard_risk_level_t)risk_level->valueint;
    } else {
        event->level = labguard_risk_level_from_string(json_get_string_or_default(root, "risk_text", "normal"));
    }

    snprintf(s_parsed_event_source,
             sizeof(s_parsed_event_source),
             "%s",
             json_get_string_or_default(root, "source", "remote"));
    snprintf(s_parsed_event_name,
             sizeof(s_parsed_event_name),
             "%s",
             json_get_string_or_default(root, "event", "unknown"));
    snprintf(s_parsed_event_actions,
             sizeof(s_parsed_event_actions),
             "%s",
             json_get_string_or_default(root, "actions", ""));

    event->node = labguard_node_from_string(json_get_string_or_default(root, "node", "unknown"));
    event->source = s_parsed_event_source;
    event->event = s_parsed_event_name;
    event->actions = s_parsed_event_actions;
    event->timestamp = json_get_i64_or_default(root, "timestamp", 0);

    cJSON_Delete(root);
    return true;
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
    command->target_node = labguard_node_from_string(json_get_string_or_default(root, "target_node", "unknown"));
    command->timestamp = json_get_i64_or_default(root, "timestamp", 0);

    cJSON_Delete(root);
    return command->type != LABGUARD_CMD_NONE;
}
