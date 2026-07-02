#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LABGUARD_VERSION "0.4.0"

#define LABGUARD_TOPIC_DEVICE_STATUS "labguard/device/status"
#define LABGUARD_TOPIC_DEVICE_SENSOR "labguard/device/sensor"
#define LABGUARD_TOPIC_DEVICE_RISK   "labguard/device/risk"
#define LABGUARD_TOPIC_DEVICE_CAMERA "labguard/device/camera"
#define LABGUARD_TOPIC_EVENT         "labguard/event"
#define LABGUARD_TOPIC_CMD_RESET     "labguard/cmd/reset"
#define LABGUARD_TOPIC_CMD_TEST      "labguard/cmd/test"
#define LABGUARD_TOPIC_CMD_CONFIG    "labguard/cmd/config"

typedef enum {
    LABGUARD_NODE_UNKNOWN = -1,
    LABGUARD_NODE_DEVICE = 0,
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
    LABGUARD_ACCESS_CAMERA_ERROR,
    LABGUARD_ACCESS_MODEL_ERROR,
} labguard_access_reason_t;

typedef enum {
    LABGUARD_CMD_NONE = 0,
    LABGUARD_CMD_RESET,
    LABGUARD_CMD_FAN_ON,
    LABGUARD_CMD_FAN_OFF,
    LABGUARD_CMD_PUMP_ON,
    LABGUARD_CMD_PUMP_OFF,
    LABGUARD_CMD_ALARM_ON,
    LABGUARD_CMD_ALARM_OFF,
    LABGUARD_CMD_AUDIO_ON,
    LABGUARD_CMD_AUDIO_OFF,
    LABGUARD_CMD_LIGHT_ON,
    LABGUARD_CMD_LIGHT_OFF,
    LABGUARD_CMD_MASTER_ON,
    LABGUARD_CMD_MASTER_OFF,
} labguard_command_type_t;

typedef struct {
    float temperature_c;
    float humidity_rh;
    int voc_index;
    float temperature_raw_c;
    float humidity_raw_rh;
    int voc_raw_index;
    int mq2_raw_adc;
    int mq2_raw_mv;
    bool mq2_alarm;
    bool mq2_analog_valid;
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
    bool auto_alarm;
    bool auto_fan;
    bool auto_pump;
    bool manual_fan_override;
    bool manual_pump_override;
    bool manual_fan;
    bool manual_pump;
    int fan_level_pct;
    int pump_level_pct;
    int manual_fan_level_pct;
    int manual_pump_level_pct;
    const char *model;
    int64_t timestamp;
} labguard_risk_state_t;

typedef struct {
    labguard_node_t node;
    bool online;
    int64_t uptime_s;
    int wifi_rssi;
    const char *version;
    bool audio_looping;
    bool light_on;
    int64_t timestamp;
} labguard_status_t;

typedef struct {
    labguard_command_type_t type;
    labguard_node_t target_node;
    int level_pct;
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

typedef struct {
    bool smoke;
    bool flame;
    float score_smoke;
    float score_flame;
    uint16_t detection_count;
    const char *model;
} labguard_hazard_result_t;

const char *labguard_node_to_string(labguard_node_t node);
labguard_node_t labguard_node_from_string(const char *node);
const char *labguard_risk_level_to_string(labguard_risk_level_t level);
labguard_risk_level_t labguard_risk_level_from_string(const char *level);
const char *labguard_access_reason_to_string(labguard_access_reason_t reason);
const char *labguard_command_type_to_string(labguard_command_type_t type);
labguard_command_type_t labguard_command_type_from_string(const char *type);
bool labguard_command_targets_node(const labguard_command_t *command, labguard_node_t node);

char *labguard_status_to_json(const labguard_status_t *status);
char *labguard_sensor_data_to_json(const labguard_sensor_data_t *data);
char *labguard_risk_state_to_json(const labguard_risk_state_t *state);
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
