#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LABGUARD_VERSION "0.3.0"

#define LABGUARD_TOPIC_OUTDOOR_STATUS "labguard/outdoor/status"
#define LABGUARD_TOPIC_OUTDOOR_PPE    "labguard/outdoor/ppe"
#define LABGUARD_TOPIC_OUTDOOR_ACCESS "labguard/outdoor/access"
#define LABGUARD_TOPIC_INDOOR_STATUS  "labguard/indoor/status"
#define LABGUARD_TOPIC_INDOOR_SENSOR  "labguard/indoor/sensor"
#define LABGUARD_TOPIC_INDOOR_RISK    "labguard/indoor/risk"
#define LABGUARD_TOPIC_EVENT          "labguard/event"
#define LABGUARD_TOPIC_CMD_RESET      "labguard/cmd/reset"
#define LABGUARD_TOPIC_CMD_TEST       "labguard/cmd/test"
#define LABGUARD_TOPIC_CMD_CONFIG     "labguard/cmd/config"

typedef enum {
    LABGUARD_NODE_UNKNOWN = -1,
    LABGUARD_NODE_OUTDOOR = 0,
    LABGUARD_NODE_INDOOR,
    LABGUARD_NODE_DASHBOARD,
} labguard_node_t;

typedef enum {
    LABGUARD_RISK_NORMAL = 0,
    LABGUARD_RISK_WARNING = 1,
    LABGUARD_RISK_ALARM = 2,
    LABGUARD_RISK_EMERGENCY = 3,
} labguard_risk_level_t;

typedef enum {
    LABGUARD_ACCESS_PASS = 0,
    LABGUARD_ACCESS_NO_PERSON,
    LABGUARD_ACCESS_MISSING_LABCOAT,
    LABGUARD_ACCESS_MISSING_GOGGLES,
    LABGUARD_ACCESS_MISSING_PPE,
    LABGUARD_ACCESS_INDOOR_WARNING,
    LABGUARD_ACCESS_INDOOR_DANGER,
    LABGUARD_ACCESS_INDOOR_OFFLINE,
    LABGUARD_ACCESS_CAMERA_ERROR,
    LABGUARD_ACCESS_MODEL_ERROR,
    LABGUARD_ACCESS_ADMIN_LOCKED,
} labguard_access_reason_t;

typedef enum {
    LABGUARD_CMD_NONE = 0,
    LABGUARD_CMD_RESET,
    LABGUARD_CMD_SELFTEST,
    LABGUARD_CMD_ADMIN_LOCK,
    LABGUARD_CMD_ADMIN_UNLOCK,
    LABGUARD_CMD_FORCE_NORMAL,
    LABGUARD_CMD_FORCE_WARNING,
    LABGUARD_CMD_FORCE_ALARM,
    LABGUARD_CMD_FORCE_EMERGENCY,
} labguard_command_type_t;

typedef enum {
    LABGUARD_PROFILE_AUTO = 0,
    LABGUARD_PROFILE_NORMAL,
    LABGUARD_PROFILE_WARNING,
    LABGUARD_PROFILE_ALARM,
    LABGUARD_PROFILE_EMERGENCY,
} labguard_profile_t;

typedef struct {
    bool person_present;
    bool labcoat;
    bool goggles;
    float score_labcoat;
    float score_goggles;
    const char *model;
    int64_t timestamp;
} labguard_ppe_result_t;

typedef struct {
    float temperature_c;
    float humidity_rh;
    int voc_index;
    float temperature_raw_c;
    float humidity_raw_rh;
    int voc_raw_index;
    bool mq2_alarm;
    bool sensor_ok;
    bool filtered;
    int64_t timestamp;
} labguard_sensor_data_t;

typedef struct {
    labguard_risk_level_t risk_level;
    const char *risk_text;
    bool smoke;
    bool flame;
    bool gas_alarm;
    float temperature_c;
    bool action_alarm;
    bool action_fan;
    bool action_pump;
    const char *model;
    int64_t timestamp;
} labguard_risk_state_t;

typedef struct {
    bool allow;
    labguard_access_reason_t reason;
    const char *door_lock;
    labguard_risk_level_t indoor_risk_level;
    int64_t timestamp;
} labguard_access_decision_t;

typedef struct {
    labguard_node_t node;
    bool online;
    int64_t uptime_s;
    int wifi_rssi;
    const char *version;
    int64_t timestamp;
} labguard_status_t;

typedef struct {
    labguard_command_type_t type;
    labguard_node_t target_node;
    int64_t timestamp;
} labguard_command_t;

typedef struct {
    labguard_node_t node;
    labguard_risk_level_t level;
    const char *source;
    const char *event;
    const char *actions;
    int64_t timestamp;
} labguard_event_t;

const char *labguard_node_to_string(labguard_node_t node);
labguard_node_t labguard_node_from_string(const char *node);
const char *labguard_risk_level_to_string(labguard_risk_level_t level);
labguard_risk_level_t labguard_risk_level_from_string(const char *level);
const char *labguard_access_reason_to_string(labguard_access_reason_t reason);
const char *labguard_command_type_to_string(labguard_command_type_t type);
labguard_command_type_t labguard_command_type_from_string(const char *type);
const char *labguard_profile_to_string(labguard_profile_t profile);
labguard_profile_t labguard_profile_from_string(const char *profile);
labguard_risk_level_t labguard_profile_to_risk_level(labguard_profile_t profile);
bool labguard_command_targets_node(const labguard_command_t *command, labguard_node_t node);

char *labguard_status_to_json(const labguard_status_t *status);
char *labguard_ppe_result_to_json(const labguard_ppe_result_t *result);
char *labguard_sensor_data_to_json(const labguard_sensor_data_t *data);
char *labguard_risk_state_to_json(const labguard_risk_state_t *state);
char *labguard_access_decision_to_json(const labguard_access_decision_t *decision);
char *labguard_command_to_json(const labguard_command_t *command);
char *labguard_event_to_json(const labguard_event_t *event);

bool labguard_status_from_json(const char *json, labguard_status_t *status);
bool labguard_sensor_data_from_json(const char *json, labguard_sensor_data_t *data);
bool labguard_risk_state_from_json(const char *json, labguard_risk_state_t *state);
bool labguard_event_from_json(const char *json, labguard_event_t *event);
bool labguard_command_from_json(const char *json, labguard_command_t *command);

#ifdef __cplusplus
}
#endif
