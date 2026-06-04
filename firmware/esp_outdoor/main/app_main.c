#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "freertos/task.h"

#include "audio_prompt.h"
#include "door_fsm.h"
#include "driver/sdmmc_host.h"
#include "event_log.h"
#include "esp_ldo_regulator.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_vfs_fat.h"
#include "espdl_probe.h"
#include "labguard_common.h"
#include "labguard_net.h"
#include "led_status.h"
#include "outdoor_camera_capture.h"
#include "ppe_infer.h"
#include "sdmmc_cmd.h"
#include "sdkconfig.h"
#include "ui_lvgl.h"

static const char *TAG = "labguard_outdoor";
static portMUX_TYPE s_state_lock = portMUX_INITIALIZER_UNLOCKED;

static labguard_risk_level_t s_latest_indoor_risk = LABGUARD_RISK_NORMAL;
static int64_t s_latest_indoor_status_ts;
static bool s_admin_locked;
static audio_prompt_t s_last_prompt = AUDIO_PROMPT_ACCESS_DENIED;

#define SD_CARD_MOUNT_POINT      "/sdcard"
#define SD_LDO_CHAN_ID           4
#define SD_LDO_VOLTAGE_MV        3300
#define WORKAROUND_HOSTED_DOES_SDMMC_HOST_INIT 1

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

static bool indoor_online_snapshot(void)
{
    int64_t last_seen;
    portENTER_CRITICAL(&s_state_lock);
    last_seen = s_latest_indoor_status_ts;
    portEXIT_CRITICAL(&s_state_lock);
    return last_seen > 0 && (now_seconds() - last_seen) <= 5;
}

static void publish_status(void)
{
    labguard_status_t status = {
        .node = LABGUARD_NODE_OUTDOOR,
        .online = true,
        .uptime_s = now_seconds(),
        .wifi_rssi = labguard_net_get_rssi(),
        .version = LABGUARD_VERSION,
        .timestamp = now_seconds(),
    };
    publish_json(LABGUARD_TOPIC_OUTDOOR_STATUS, labguard_status_to_json(&status));
}

static void publish_event(labguard_risk_level_t level, const char *event, const char *actions)
{
    labguard_event_t message = {
        .node = LABGUARD_NODE_OUTDOOR,
        .level = level,
        .source = "door_control",
        .event = event,
        .actions = actions,
        .timestamp = now_seconds(),
    };
    event_log_append_event(&message);
    publish_json(LABGUARD_TOPIC_EVENT, labguard_event_to_json(&message));
}

#if WORKAROUND_HOSTED_DOES_SDMMC_HOST_INIT
static esp_err_t sdmmc_host_init_dummy(void)
{
    return ESP_OK;
}

static esp_err_t sdmmc_host_deinit_dummy(void)
{
    return ESP_OK;
}
#endif

static void init_sd_card(void)
{
    static esp_ldo_channel_handle_t s_sd_ldo_handle = NULL;

    if (s_sd_ldo_handle == NULL) {
        const esp_ldo_channel_config_t ldo_cfg = {
            .chan_id = SD_LDO_CHAN_ID,
            .voltage_mv = SD_LDO_VOLTAGE_MV,
        };
        esp_err_t ldo_ret = esp_ldo_acquire_channel(&ldo_cfg, &s_sd_ldo_handle);
        if (ldo_ret != ESP_OK) {
            ESP_LOGW(TAG, "microSD LDO enable failed: %s", esp_err_to_name(ldo_ret));
        }
    }

    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 64 * 1024,
    };
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.slot = SDMMC_HOST_SLOT_0;
    host.max_freq_khz = SDMMC_FREQ_HIGHSPEED;
#if WORKAROUND_HOSTED_DOES_SDMMC_HOST_INIT
    host.init = sdmmc_host_init_dummy;
    host.deinit = sdmmc_host_deinit_dummy;
#endif

    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
    slot_config.width = 4;
    slot_config.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;
    slot_config.cmd = 0;
    slot_config.clk = 0;
    slot_config.d0 = 0;
    slot_config.d1 = 0;
    slot_config.d2 = 0;
    slot_config.d3 = 0;
    slot_config.d4 = 0;
    slot_config.d5 = 0;
    slot_config.d6 = 0;
    slot_config.d7 = 0;
    sdmmc_card_t *card = NULL;

    ESP_LOGI(TAG, "mounting onboard microSD at %s", SD_CARD_MOUNT_POINT);
    esp_err_t ret = esp_vfs_fat_sdmmc_mount(SD_CARD_MOUNT_POINT, &host, &slot_config, &mount_config, &card);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "microSD mount failed: %s", esp_err_to_name(ret));
        return;
    }

    sdmmc_card_print_info(stdout, card);
    ESP_LOGI(TAG, "microSD mounted at %s", SD_CARD_MOUNT_POINT);
}

static audio_prompt_t prompt_for_reason(labguard_access_reason_t reason)
{
    switch (reason) {
    case LABGUARD_ACCESS_PASS:
        return AUDIO_PROMPT_ACCESS_GRANTED;
    case LABGUARD_ACCESS_MISSING_GOGGLES:
        return AUDIO_PROMPT_MISSING_GOGGLES;
    case LABGUARD_ACCESS_MISSING_LABCOAT:
    case LABGUARD_ACCESS_MISSING_PPE:
        return AUDIO_PROMPT_MISSING_LABCOAT;
    case LABGUARD_ACCESS_INDOOR_DANGER:
    case LABGUARD_ACCESS_INDOOR_WARNING:
    case LABGUARD_ACCESS_INDOOR_OFFLINE:
        return AUDIO_PROMPT_INDOOR_DANGER;
    case LABGUARD_ACCESS_NO_PERSON:
        return s_last_prompt;
    case LABGUARD_ACCESS_CAMERA_ERROR:
    case LABGUARD_ACCESS_MODEL_ERROR:
    case LABGUARD_ACCESS_ADMIN_LOCKED:
    default:
        return AUDIO_PROMPT_ACCESS_DENIED;
    }
}

static void apply_command(const labguard_command_t *command)
{
    if (command == NULL || !labguard_command_targets_node(command, LABGUARD_NODE_OUTDOOR)) {
        return;
    }

    switch (command->type) {
    case LABGUARD_CMD_RESET:
        ppe_infer_set_profile(LABGUARD_PROFILE_AUTO);
        portENTER_CRITICAL(&s_state_lock);
        s_admin_locked = false;
        portEXIT_CRITICAL(&s_state_lock);
        publish_event(LABGUARD_RISK_NORMAL, "outdoor_reset", "clear_admin_lock");
        break;
    case LABGUARD_CMD_SELFTEST:
        ppe_infer_set_profile(LABGUARD_PROFILE_WARNING);
        publish_event(LABGUARD_RISK_WARNING, "outdoor_selftest", "simulate_missing_goggles");
        break;
    case LABGUARD_CMD_ADMIN_LOCK:
        portENTER_CRITICAL(&s_state_lock);
        s_admin_locked = true;
        portEXIT_CRITICAL(&s_state_lock);
        publish_event(LABGUARD_RISK_WARNING, "admin_lock_enabled", "door_locked");
        break;
    case LABGUARD_CMD_ADMIN_UNLOCK:
        portENTER_CRITICAL(&s_state_lock);
        s_admin_locked = false;
        portEXIT_CRITICAL(&s_state_lock);
        publish_event(LABGUARD_RISK_NORMAL, "admin_lock_disabled", "door_auto_mode");
        break;
    case LABGUARD_CMD_FORCE_NORMAL:
        ppe_infer_set_profile(LABGUARD_PROFILE_NORMAL);
        publish_event(LABGUARD_RISK_NORMAL, "ppe_profile_normal", "simulate_clean_pass");
        break;
    case LABGUARD_CMD_FORCE_WARNING:
        ppe_infer_set_profile(LABGUARD_PROFILE_WARNING);
        publish_event(LABGUARD_RISK_WARNING, "ppe_profile_warning", "simulate_missing_goggles");
        break;
    case LABGUARD_CMD_FORCE_ALARM:
        ppe_infer_set_profile(LABGUARD_PROFILE_ALARM);
        publish_event(LABGUARD_RISK_WARNING, "ppe_profile_alarm", "simulate_missing_labcoat");
        break;
    case LABGUARD_CMD_FORCE_EMERGENCY:
        ppe_infer_set_profile(LABGUARD_PROFILE_EMERGENCY);
        publish_event(LABGUARD_RISK_ALARM, "ppe_profile_emergency", "simulate_missing_all_ppe");
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

    if (strcmp(topic, LABGUARD_TOPIC_INDOOR_RISK) == 0) {
        labguard_risk_state_t risk = {0};
        if (labguard_risk_state_from_json(buffer, &risk)) {
            portENTER_CRITICAL(&s_state_lock);
            s_latest_indoor_risk = risk.risk_level;
            s_latest_indoor_status_ts = now_seconds();
            portEXIT_CRITICAL(&s_state_lock);
            ui_lvgl_update_indoor_risk(&risk, true);
        }
        return;
    }

    if (strcmp(topic, LABGUARD_TOPIC_INDOOR_SENSOR) == 0) {
        labguard_sensor_data_t sensor = {0};
        if (labguard_sensor_data_from_json(buffer, &sensor)) {
            portENTER_CRITICAL(&s_state_lock);
            s_latest_indoor_status_ts = now_seconds();
            portEXIT_CRITICAL(&s_state_lock);
            ui_lvgl_update_indoor_sensor(&sensor);
        }
        return;
    }

    if (strcmp(topic, LABGUARD_TOPIC_INDOOR_STATUS) == 0) {
        labguard_status_t status = {0};
        if (labguard_status_from_json(buffer, &status) && status.online) {
            portENTER_CRITICAL(&s_state_lock);
            s_latest_indoor_status_ts = now_seconds();
            portEXIT_CRITICAL(&s_state_lock);
            ui_lvgl_set_indoor_online(true);
        }
        return;
    }

    if (strcmp(topic, LABGUARD_TOPIC_EVENT) == 0) {
        labguard_event_t event = {0};
        if (labguard_event_from_json(buffer, &event) && event.node != LABGUARD_NODE_OUTDOOR) {
            event_log_append_event(&event);
        }
        return;
    }

    if (strcmp(topic, LABGUARD_TOPIC_CMD_RESET) == 0 || strcmp(topic, LABGUARD_TOPIC_CMD_TEST) == 0) {
        labguard_command_t command = {0};
        if (labguard_command_from_json(buffer, &command)) {
            apply_command(&command);
        }
    }
}

static void outdoor_task(void *arg)
{
    (void)arg;

    labguard_access_reason_t last_reason = LABGUARD_ACCESS_NO_PERSON;
    bool last_allow = false;

    while (true) {
        labguard_ppe_result_t ppe = {0};
        labguard_access_decision_t decision = {0};
        labguard_risk_level_t indoor_risk;
        bool indoor_online;
        bool admin_locked;

        ppe_infer_run(NULL, 0, &ppe);

        portENTER_CRITICAL(&s_state_lock);
        indoor_risk = s_latest_indoor_risk;
        admin_locked = s_admin_locked;
        portEXIT_CRITICAL(&s_state_lock);

        indoor_online = indoor_online_snapshot();
        door_fsm_evaluate(&ppe, indoor_online, indoor_risk, &decision);

        if (admin_locked) {
            decision.allow = false;
            decision.reason = LABGUARD_ACCESS_ADMIN_LOCKED;
            decision.door_lock = "locked";
        }

        led_status_apply_access(&decision);
        ui_lvgl_set_indoor_online(indoor_online);
        ui_lvgl_set_admin_locked(admin_locked);
        ui_lvgl_update_access(&ppe, &decision, indoor_risk);

        audio_prompt_t prompt = prompt_for_reason(decision.reason);
        if (decision.reason != LABGUARD_ACCESS_NO_PERSON &&
            (decision.reason != last_reason || decision.allow != last_allow)) {
            audio_prompt_play(prompt);
            s_last_prompt = prompt;
        }

        publish_json(LABGUARD_TOPIC_OUTDOOR_PPE, labguard_ppe_result_to_json(&ppe));
        publish_json(LABGUARD_TOPIC_OUTDOOR_ACCESS, labguard_access_decision_to_json(&decision));

        if (decision.reason != last_reason || decision.allow != last_allow) {
            publish_event(decision.allow ? LABGUARD_RISK_NORMAL : decision.indoor_risk_level,
                          labguard_access_reason_to_string(decision.reason),
                          decision.allow ? "door_unlock_3s" : "door_locked");
            last_reason = decision.reason;
            last_allow = decision.allow;
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
    ESP_LOGI(TAG, "LabGuard outdoor node starting, version=%s", LABGUARD_VERSION);

    portENTER_CRITICAL(&s_state_lock);
    s_latest_indoor_risk = LABGUARD_RISK_NORMAL;
    s_latest_indoor_status_ts = now_seconds();
    s_admin_locked = false;
    portEXIT_CRITICAL(&s_state_lock);

    event_log_init(NULL);
    init_sd_card();
    ui_lvgl_init();

    labguard_net_config_t net_config = {
        .wifi_ssid = CONFIG_LABGUARD_WIFI_SSID,
        .wifi_password = CONFIG_LABGUARD_WIFI_PASSWORD,
        .mqtt_uri = CONFIG_LABGUARD_MQTT_URI,
        .message_cb = net_message_cb,
        .user_ctx = NULL,
    };
    labguard_net_init(&net_config);
    labguard_net_start();
    labguard_net_subscribe(LABGUARD_TOPIC_INDOOR_RISK, 1);
    labguard_net_subscribe(LABGUARD_TOPIC_INDOOR_SENSOR, 1);
    labguard_net_subscribe(LABGUARD_TOPIC_INDOOR_STATUS, 1);
    labguard_net_subscribe(LABGUARD_TOPIC_EVENT, 1);
    labguard_net_subscribe(LABGUARD_TOPIC_CMD_RESET, 1);
    labguard_net_subscribe(LABGUARD_TOPIC_CMD_TEST, 1);

    outdoor_camera_capture_init();
    ppe_infer_init();
#if CONFIG_LABGUARD_ESPDL_PROBE_RUN_ON_BOOT
    espdl_probe_run_once();
#endif
    door_fsm_init();
    audio_prompt_init();
    led_status_init();

    publish_status();
    publish_event(LABGUARD_RISK_NORMAL, "outdoor_boot", "ui_camera_audio_ready");

    xTaskCreate(outdoor_task, "outdoor_task", 8192, NULL, 5, NULL);
    xTaskCreate(heartbeat_task, "outdoor_heartbeat", 4096, NULL, 4, NULL);
}
