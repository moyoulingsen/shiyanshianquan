#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "freertos/task.h"

#include "actuator_ctrl.h"
#include "event_log.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "hazard_infer.h"
#include "indoor_camera_capture.h"
#include "labguard_common.h"
#include "labguard_net.h"
#include "risk_fusion.h"
#include "sensor_reader.h"

static const char *TAG = "labguard_indoor";
static portMUX_TYPE s_state_lock = portMUX_INITIALIZER_UNLOCKED;

static bool s_force_safe_mode;

static int64_t now_seconds(void)
{
    return esp_timer_get_time() / 1000000;
}

static void publish_json(const char *topic, char *json)
{
    if (json == NULL) {
        ESP_LOGW(TAG, "skip publish to %s: json allocation failed", topic);
        return;
    }

    labguard_net_publish(topic, json, 1, false);
    free(json);
}

static void publish_status(void)
{
    labguard_status_t status = {
        .node = LABGUARD_NODE_INDOOR,
        .online = true,
        .uptime_s = now_seconds(),
        .wifi_rssi = labguard_net_get_rssi(),
        .version = LABGUARD_VERSION,
        .timestamp = now_seconds(),
    };
    publish_json(LABGUARD_TOPIC_INDOOR_STATUS, labguard_status_to_json(&status));
}

static void publish_event(labguard_risk_level_t level, const char *event, const char *actions)
{
    labguard_event_t message = {
        .node = LABGUARD_NODE_INDOOR,
        .level = level,
        .source = "risk_engine",
        .event = event,
        .actions = actions,
        .timestamp = now_seconds(),
    };
    event_log_append_event(&message);
    publish_json(LABGUARD_TOPIC_EVENT, labguard_event_to_json(&message));
}

static void set_all_profiles(labguard_profile_t profile)
{
    sensor_reader_set_profile(profile);
    hazard_infer_set_profile(profile);
}

static void apply_command(const labguard_command_t *command)
{
    if (command == NULL || !labguard_command_targets_node(command, LABGUARD_NODE_INDOOR)) {
        return;
    }

    switch (command->type) {
    case LABGUARD_CMD_RESET:
        set_all_profiles(LABGUARD_PROFILE_AUTO);
        portENTER_CRITICAL(&s_state_lock);
        s_force_safe_mode = false;
        portEXIT_CRITICAL(&s_state_lock);
        publish_event(LABGUARD_RISK_NORMAL, "indoor_reset", "clear_forced_mode");
        break;
    case LABGUARD_CMD_SELFTEST:
        set_all_profiles(LABGUARD_PROFILE_ALARM);
        publish_event(LABGUARD_RISK_ALARM, "indoor_selftest", "simulate_alarm_scene");
        break;
    case LABGUARD_CMD_ADMIN_LOCK:
        portENTER_CRITICAL(&s_state_lock);
        s_force_safe_mode = false;
        portEXIT_CRITICAL(&s_state_lock);
        publish_event(LABGUARD_RISK_WARNING, "admin_lock_notice", "outdoor_should_lock");
        break;
    case LABGUARD_CMD_ADMIN_UNLOCK:
        set_all_profiles(LABGUARD_PROFILE_AUTO);
        publish_event(LABGUARD_RISK_NORMAL, "admin_unlock_notice", "return_auto_mode");
        break;
    case LABGUARD_CMD_FORCE_NORMAL:
        portENTER_CRITICAL(&s_state_lock);
        s_force_safe_mode = true;
        portEXIT_CRITICAL(&s_state_lock);
        set_all_profiles(LABGUARD_PROFILE_NORMAL);
        publish_event(LABGUARD_RISK_NORMAL, "indoor_profile_normal", "force_safe_mode");
        break;
    case LABGUARD_CMD_FORCE_WARNING:
        portENTER_CRITICAL(&s_state_lock);
        s_force_safe_mode = false;
        portEXIT_CRITICAL(&s_state_lock);
        set_all_profiles(LABGUARD_PROFILE_WARNING);
        publish_event(LABGUARD_RISK_WARNING, "indoor_profile_warning", "simulate_warning_scene");
        break;
    case LABGUARD_CMD_FORCE_ALARM:
        portENTER_CRITICAL(&s_state_lock);
        s_force_safe_mode = false;
        portEXIT_CRITICAL(&s_state_lock);
        set_all_profiles(LABGUARD_PROFILE_ALARM);
        publish_event(LABGUARD_RISK_ALARM, "indoor_profile_alarm", "simulate_alarm_scene");
        break;
    case LABGUARD_CMD_FORCE_EMERGENCY:
        portENTER_CRITICAL(&s_state_lock);
        s_force_safe_mode = false;
        portEXIT_CRITICAL(&s_state_lock);
        set_all_profiles(LABGUARD_PROFILE_EMERGENCY);
        publish_event(LABGUARD_RISK_EMERGENCY, "indoor_profile_emergency", "simulate_fire_scene");
        break;
    case LABGUARD_CMD_NONE:
    default:
        break;
    }
}

static void net_message_cb(const char *topic, const char *payload, int payload_len, void *user_ctx)
{
    (void)user_ctx;

    char buffer[384];
    int copy_len = payload_len;
    if (copy_len < 0) {
        copy_len = 0;
    }
    if (copy_len >= (int)sizeof(buffer)) {
        copy_len = sizeof(buffer) - 1;
    }
    memcpy(buffer, payload, (size_t)copy_len);
    buffer[copy_len] = '\0';

    if (strcmp(topic, LABGUARD_TOPIC_CMD_RESET) == 0 || strcmp(topic, LABGUARD_TOPIC_CMD_TEST) == 0) {
        labguard_command_t command = {0};
        if (labguard_command_from_json(buffer, &command)) {
            apply_command(&command);
        }
    }
}

static void indoor_task(void *arg)
{
    (void)arg;

    labguard_risk_level_t last_level = LABGUARD_RISK_NORMAL;

    while (true) {
        labguard_sensor_data_t sensor = {0};
        labguard_hazard_result_t hazard = {0};
        labguard_risk_state_t risk = {0};
        bool force_safe;

        sensor_reader_read(&sensor);
        hazard_infer_run(NULL, 0, &hazard);
        risk_fusion_evaluate(&sensor, &hazard, &risk);

        portENTER_CRITICAL(&s_state_lock);
        force_safe = s_force_safe_mode;
        portEXIT_CRITICAL(&s_state_lock);

        if (force_safe) {
            risk.risk_level = LABGUARD_RISK_NORMAL;
            risk.risk_text = "forced_safe";
            risk.smoke = false;
            risk.flame = false;
            risk.gas_alarm = false;
            risk.action_alarm = false;
            risk.action_fan = false;
            risk.action_pump = false;
        }

        actuator_ctrl_apply_risk(&risk);

        publish_json(LABGUARD_TOPIC_INDOOR_SENSOR, labguard_sensor_data_to_json(&sensor));
        publish_json(LABGUARD_TOPIC_INDOOR_RISK, labguard_risk_state_to_json(&risk));

        if (risk.risk_level != last_level) {
            publish_event(risk.risk_level, risk.risk_text, risk.action_pump ? "alarm_fan_pump" :
                           (risk.action_fan ? "alarm_fan" : (risk.action_alarm ? "alarm_only" : "idle")));
            last_level = risk.risk_level;
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

static void heartbeat_task(void *arg)
{
    (void)arg;

    while (true) {
        publish_status();
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "LabGuard indoor node starting, version=%s", LABGUARD_VERSION);

    portENTER_CRITICAL(&s_state_lock);
    s_force_safe_mode = false;
    portEXIT_CRITICAL(&s_state_lock);

    event_log_init(NULL);

    labguard_net_config_t net_config = {
        .wifi_ssid = "",
        .wifi_password = "",
        .mqtt_uri = "",
        .message_cb = net_message_cb,
        .user_ctx = NULL,
    };
    labguard_net_init(&net_config);
    labguard_net_start();
    labguard_net_subscribe(LABGUARD_TOPIC_CMD_RESET, 1);
    labguard_net_subscribe(LABGUARD_TOPIC_CMD_TEST, 1);

    indoor_camera_capture_init();
    sensor_reader_init();
    hazard_infer_init();
    risk_fusion_init();
    actuator_ctrl_init();

    publish_status();
    publish_event(LABGUARD_RISK_NORMAL, "indoor_boot", "camera_sensor_actuator_ready");

    xTaskCreate(indoor_task, "indoor_task", 8192, NULL, 5, NULL);
    xTaskCreate(heartbeat_task, "indoor_heartbeat", 4096, NULL, 4, NULL);
}
