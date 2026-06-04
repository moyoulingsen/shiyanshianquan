#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "freertos/task.h"

#include "actuator_ctrl.h"
#include "driver/jpeg_encode.h"
#include "driver/sdmmc_host.h"
#include "event_log.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_ldo_regulator.h"
#include "esp_timer.h"
#include "esp_vfs_fat.h"
#include "espdl_probe.h"
#include "hazard_infer.h"
#include "indoor_camera_capture.h"
#include "labguard_common.h"
#include "labguard_net.h"
#include "mbedtls/base64.h"
#include "risk_fusion.h"
#include "sdmmc_cmd.h"
#include "sdkconfig.h"
#include "sensor_reader.h"

static const char *TAG = "labguard_indoor";
static portMUX_TYPE s_state_lock = portMUX_INITIALIZER_UNLOCKED;

static bool s_force_safe_mode;
static bool s_manual_fan_on;
static bool s_manual_pump_on;
static bool s_manual_alarm_on;
static int s_manual_fan_level_pct;
static int s_manual_pump_level_pct;

#define CAMERA_PREVIEW_WIDTH     80
#define CAMERA_PREVIEW_HEIGHT    48
#define CAMERA_PREVIEW_CHANNELS  3
#define CAMERA_PREVIEW_JPEG_QUALITY 35
#define CAMERA_PREVIEW_FPS       4
#define CAMERA_PREVIEW_INTERVAL_MS (1000 / CAMERA_PREVIEW_FPS)
#define CAMERA_PREVIEW_RGB_BYTES (CAMERA_PREVIEW_WIDTH * CAMERA_PREVIEW_HEIGHT * CAMERA_PREVIEW_CHANNELS)
#define CAMERA_PREVIEW_JPEG_BYTES (CAMERA_PREVIEW_RGB_BYTES)
#define CAMERA_PREVIEW_B64_BYTES (((CAMERA_PREVIEW_JPEG_BYTES + 2) / 3) * 4 + 1)
#define CAMERA_PREVIEW_JSON_BYTES (CAMERA_PREVIEW_B64_BYTES + 320)
#define SD_CARD_MOUNT_POINT      "/sdcard"
#define SD_LDO_CHAN_ID           4
#define SD_LDO_VOLTAGE_MV        3300
#define WORKAROUND_HOSTED_DOES_SDMMC_HOST_INIT 1

static uint8_t s_preview_rgb[CAMERA_PREVIEW_RGB_BYTES];
static unsigned char s_preview_b64[CAMERA_PREVIEW_B64_BYTES];
static uint8_t *s_preview_jpeg;
static size_t s_preview_jpeg_capacity;
static jpeg_encoder_handle_t s_jpeg_encoder;

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

static int command_level_or_default(const labguard_command_t *command, int default_level)
{
    if (command == NULL || command->level_pct < 0) {
        return default_level;
    }
    if (command->level_pct > 100) {
        return 100;
    }
    return command->level_pct;
}

static void clear_manual_overrides(void)
{
    s_manual_fan_on = false;
    s_manual_pump_on = false;
    s_manual_alarm_on = false;
    s_manual_fan_level_pct = 100;
    s_manual_pump_level_pct = 100;
}

static uint8_t expand_rgb565_component(uint16_t value, int bits)
{
    if (bits == 5) {
        return (uint8_t)((value << 3) | (value >> 2));
    }
    return (uint8_t)((value << 2) | (value >> 4));
}

static bool init_camera_preview_encoder(void)
{
    if (s_jpeg_encoder != NULL && s_preview_jpeg != NULL) {
        return true;
    }

    jpeg_encode_engine_cfg_t engine_cfg = {
        .timeout_ms = 100,
    };
    if (jpeg_new_encoder_engine(&engine_cfg, &s_jpeg_encoder) != ESP_OK) {
        ESP_LOGW(TAG, "failed to initialize JPEG encoder");
        s_jpeg_encoder = NULL;
        return false;
    }

    jpeg_encode_memory_alloc_cfg_t out_mem_cfg = {
        .buffer_direction = JPEG_ENC_ALLOC_OUTPUT_BUFFER,
    };
    s_preview_jpeg = jpeg_alloc_encoder_mem(CAMERA_PREVIEW_JPEG_BYTES, &out_mem_cfg, &s_preview_jpeg_capacity);
    if (s_preview_jpeg == NULL) {
        ESP_LOGW(TAG, "failed to allocate JPEG preview buffer");
        jpeg_del_encoder_engine(s_jpeg_encoder);
        s_jpeg_encoder = NULL;
        s_preview_jpeg_capacity = 0;
        return false;
    }

    return true;
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

static bool encode_camera_preview_jpeg(const indoor_camera_frame_t *frame, size_t *jpeg_size_out)
{
    if (frame == NULL || frame->data == NULL || jpeg_size_out == NULL) {
        return false;
    }
    if (frame->pixel_format != INDOOR_CAMERA_PIXEL_FORMAT_RGB565) {
        ESP_LOGW(TAG, "camera preview publish only supports RGB565 frames");
        return false;
    }
    if (!init_camera_preview_encoder()) {
        return false;
    }

    const uint16_t *src = (const uint16_t *)frame->data;
    const int src_width = frame->width;
    const int src_height = frame->height;

    for (int y = 0; y < CAMERA_PREVIEW_HEIGHT; ++y) {
        const int src_y = ((CAMERA_PREVIEW_HEIGHT - 1 - y) * src_height) / CAMERA_PREVIEW_HEIGHT;
        const uint16_t *src_row = src + ((size_t)src_y * src_width);
        uint8_t *dst_row = &s_preview_rgb[(size_t)y * CAMERA_PREVIEW_WIDTH * CAMERA_PREVIEW_CHANNELS];
        for (int x = 0; x < CAMERA_PREVIEW_WIDTH; ++x) {
            const int src_x = (x * src_width) / CAMERA_PREVIEW_WIDTH;
            const uint16_t pixel = src_row[src_x];
            uint8_t *dst_pixel = &dst_row[(size_t)x * CAMERA_PREVIEW_CHANNELS];
            dst_pixel[0] = expand_rgb565_component((pixel >> 11) & 0x1F, 5);
            dst_pixel[1] = expand_rgb565_component((pixel >> 5) & 0x3F, 6);
            dst_pixel[2] = expand_rgb565_component(pixel & 0x1F, 5);
        }
    }

    jpeg_encode_cfg_t encode_cfg = {
        .height = CAMERA_PREVIEW_HEIGHT,
        .width = CAMERA_PREVIEW_WIDTH,
        .src_type = JPEG_ENCODE_IN_FORMAT_RGB888,
        .sub_sample = JPEG_DOWN_SAMPLING_YUV422,
        .image_quality = CAMERA_PREVIEW_JPEG_QUALITY,
    };
    uint32_t jpeg_size = 0;
    esp_err_t ret = jpeg_encoder_process(s_jpeg_encoder,
                                         &encode_cfg,
                                         s_preview_rgb,
                                         sizeof(s_preview_rgb),
                                         s_preview_jpeg,
                                         s_preview_jpeg_capacity,
                                         &jpeg_size);
    if (ret != ESP_OK || jpeg_size == 0) {
        ESP_LOGW(TAG, "jpeg encode failed: %s", esp_err_to_name(ret));
        return false;
    }

    *jpeg_size_out = jpeg_size;
    return true;
}

static bool build_camera_preview_json(const indoor_camera_frame_t *frame, char *json, size_t json_size)
{
    if (frame == NULL || json == NULL || frame->data == NULL) {
        return false;
    }

    size_t jpeg_size = 0;
    if (!encode_camera_preview_jpeg(frame, &jpeg_size)) {
        return false;
    }

    size_t b64_len = 0;
    int rc = mbedtls_base64_encode(s_preview_b64, sizeof(s_preview_b64), &b64_len,
                                   s_preview_jpeg, jpeg_size);
    if (rc != 0) {
        ESP_LOGW(TAG, "base64 encode failed: -0x%04x", -rc);
        return false;
    }
    s_preview_b64[b64_len] = '\0';

    int json_len = snprintf(json, json_size,
                            "{\"type\":\"camera_frame\",\"format\":\"image/jpeg\",\"width\":%d,\"height\":%d,\"sequence\":%u,\"timestamp\":%lld,\"image_base64\":\"%s\"}",
                            CAMERA_PREVIEW_WIDTH,
                            CAMERA_PREVIEW_HEIGHT,
                            (unsigned int)frame->sequence,
                            (long long)now_seconds(),
                            s_preview_b64);
    return json_len > 0 && (size_t)json_len < json_size;
}

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
        clear_manual_overrides();
        portEXIT_CRITICAL(&s_state_lock);
        actuator_ctrl_set_fan(false);
        actuator_ctrl_set_pump(false);
        actuator_ctrl_set_alarm(false);
        publish_event(LABGUARD_RISK_NORMAL, "indoor_reset", "clear_forced_mode");
        break;
    case LABGUARD_CMD_SELFTEST:
        portENTER_CRITICAL(&s_state_lock);
        clear_manual_overrides();
        portEXIT_CRITICAL(&s_state_lock);
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
        clear_manual_overrides();
        portEXIT_CRITICAL(&s_state_lock);
        actuator_ctrl_set_fan(false);
        actuator_ctrl_set_pump(false);
        actuator_ctrl_set_alarm(false);
        set_all_profiles(LABGUARD_PROFILE_NORMAL);
        publish_event(LABGUARD_RISK_NORMAL, "indoor_profile_normal", "force_safe_mode");
        break;
    case LABGUARD_CMD_FORCE_WARNING:
        portENTER_CRITICAL(&s_state_lock);
        s_force_safe_mode = false;
        clear_manual_overrides();
        portEXIT_CRITICAL(&s_state_lock);
        set_all_profiles(LABGUARD_PROFILE_WARNING);
        publish_event(LABGUARD_RISK_WARNING, "indoor_profile_warning", "simulate_warning_scene");
        break;
    case LABGUARD_CMD_FORCE_ALARM:
        portENTER_CRITICAL(&s_state_lock);
        s_force_safe_mode = false;
        clear_manual_overrides();
        portEXIT_CRITICAL(&s_state_lock);
        set_all_profiles(LABGUARD_PROFILE_ALARM);
        publish_event(LABGUARD_RISK_ALARM, "indoor_profile_alarm", "simulate_alarm_scene");
        break;
    case LABGUARD_CMD_FORCE_EMERGENCY:
        portENTER_CRITICAL(&s_state_lock);
        s_force_safe_mode = false;
        clear_manual_overrides();
        portEXIT_CRITICAL(&s_state_lock);
        set_all_profiles(LABGUARD_PROFILE_EMERGENCY);
        publish_event(LABGUARD_RISK_EMERGENCY, "indoor_profile_emergency", "simulate_fire_scene");
        break;
    case LABGUARD_CMD_FAN_ON:
        portENTER_CRITICAL(&s_state_lock);
        s_manual_fan_on = true;
        s_manual_fan_level_pct = command_level_or_default(command, s_manual_fan_level_pct);
        portEXIT_CRITICAL(&s_state_lock);
        actuator_ctrl_set_fan_level(s_manual_fan_level_pct);
        actuator_ctrl_set_fan(true);
        publish_event(LABGUARD_RISK_NORMAL, "manual_fan_on", "fan_on");
        break;
    case LABGUARD_CMD_FAN_OFF:
        portENTER_CRITICAL(&s_state_lock);
        s_manual_fan_on = false;
        portEXIT_CRITICAL(&s_state_lock);
        actuator_ctrl_set_fan(false);
        publish_event(LABGUARD_RISK_NORMAL, "manual_fan_off", "fan_off");
        break;
    case LABGUARD_CMD_PUMP_ON:
        portENTER_CRITICAL(&s_state_lock);
        s_manual_pump_on = true;
        s_manual_pump_level_pct = command_level_or_default(command, s_manual_pump_level_pct);
        portEXIT_CRITICAL(&s_state_lock);
        actuator_ctrl_set_pump_level(s_manual_pump_level_pct);
        actuator_ctrl_set_pump(true);
        publish_event(LABGUARD_RISK_NORMAL, "manual_pump_on", "pump_on");
        break;
    case LABGUARD_CMD_PUMP_OFF:
        portENTER_CRITICAL(&s_state_lock);
        s_manual_pump_on = false;
        portEXIT_CRITICAL(&s_state_lock);
        actuator_ctrl_set_pump(false);
        publish_event(LABGUARD_RISK_NORMAL, "manual_pump_off", "pump_off");
        break;
    case LABGUARD_CMD_ALARM_ON:
        portENTER_CRITICAL(&s_state_lock);
        s_manual_alarm_on = true;
        portEXIT_CRITICAL(&s_state_lock);
        actuator_ctrl_set_alarm(true);
        publish_event(LABGUARD_RISK_NORMAL, "manual_alarm_on", "alarm_on");
        break;
    case LABGUARD_CMD_ALARM_OFF:
        portENTER_CRITICAL(&s_state_lock);
        s_manual_alarm_on = false;
        portEXIT_CRITICAL(&s_state_lock);
        actuator_ctrl_set_alarm(false);
        publish_event(LABGUARD_RISK_NORMAL, "manual_alarm_off", "alarm_off");
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
    bool logged_missing_frame = false;

    while (true) {
        labguard_sensor_data_t sensor = {0};
        labguard_hazard_result_t hazard = {0};
        labguard_risk_state_t risk = {0};
        indoor_camera_frame_t frame = {0};
        bool force_safe;
        bool manual_fan_on;
        bool manual_pump_on;
        bool manual_alarm_on;
        int manual_fan_level_pct;
        int manual_pump_level_pct;

        sensor_reader_read(&sensor);

        esp_err_t frame_ret = indoor_camera_capture_get_latest_frame(&frame);
        if (frame_ret == ESP_OK) {
            logged_missing_frame = false;
            hazard_infer_run(frame.data, frame.len, frame.width, frame.height, &hazard);
        } else {
            if (!logged_missing_frame) {
                ESP_LOGW(TAG, "camera frame unavailable for hazard inference yet: %s", esp_err_to_name(frame_ret));
                logged_missing_frame = true;
            }
            hazard_infer_run(NULL, 0, 0, 0, &hazard);
        }

        risk_fusion_evaluate(&sensor, &hazard, &risk);

        portENTER_CRITICAL(&s_state_lock);
        force_safe = s_force_safe_mode;
        manual_fan_on = s_manual_fan_on;
        manual_pump_on = s_manual_pump_on;
        manual_alarm_on = s_manual_alarm_on;
        manual_fan_level_pct = s_manual_fan_level_pct;
        manual_pump_level_pct = s_manual_pump_level_pct;
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
            risk.fan_level_pct = 0;
            risk.pump_level_pct = 0;
        }

        if (manual_fan_on) {
            risk.action_fan = true;
            risk.fan_level_pct = manual_fan_level_pct;
        } else if (risk.action_fan && risk.fan_level_pct <= 0) {
            risk.fan_level_pct = 100;
        } else if (!risk.action_fan) {
            risk.fan_level_pct = 0;
        }

        if (manual_pump_on) {
            risk.action_pump = true;
            risk.pump_level_pct = manual_pump_level_pct;
        } else if (risk.action_pump && risk.pump_level_pct <= 0) {
            risk.pump_level_pct = 100;
        } else if (!risk.action_pump) {
            risk.pump_level_pct = 0;
        }

        if (manual_alarm_on) {
            risk.action_alarm = true;
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

static void camera_publish_task(void *arg)
{
    (void)arg;

    static char preview_json[CAMERA_PREVIEW_JSON_BYTES];
    bool logged_missing_frame = false;

    while (true) {
        indoor_camera_frame_t frame = {0};
        esp_err_t frame_ret = indoor_camera_capture_get_latest_frame(&frame);
        if (frame_ret == ESP_OK) {
            logged_missing_frame = false;
            if (build_camera_preview_json(&frame, preview_json, sizeof(preview_json))) {
                labguard_net_publish(LABGUARD_TOPIC_INDOOR_CAMERA, preview_json, 0, false);
            } else {
                ESP_LOGW(TAG, "skip camera preview publish: preview encode failed");
            }
        } else if (!logged_missing_frame) {
            ESP_LOGW(TAG, "camera preview unavailable for dashboard yet: %s", esp_err_to_name(frame_ret));
            logged_missing_frame = true;
        }

        vTaskDelay(pdMS_TO_TICKS(CAMERA_PREVIEW_INTERVAL_MS));
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
    clear_manual_overrides();
    portEXIT_CRITICAL(&s_state_lock);

    actuator_ctrl_set_fan_level(100);
    actuator_ctrl_set_pump_level(100);

    event_log_init(NULL);
    init_sd_card();

    labguard_net_config_t net_config = {
        .wifi_ssid = CONFIG_LABGUARD_WIFI_SSID,
        .wifi_password = CONFIG_LABGUARD_WIFI_PASSWORD,
        .mqtt_uri = CONFIG_LABGUARD_MQTT_URI,
        .message_cb = net_message_cb,
        .user_ctx = NULL,
    };
    labguard_net_init(&net_config);
    labguard_net_start();
    labguard_net_subscribe(LABGUARD_TOPIC_CMD_RESET, 1);
    labguard_net_subscribe(LABGUARD_TOPIC_CMD_TEST, 1);

    // sensor_reader_init creates the shared I2C bus on GPIO7/8 that the
    // SC2336 SCCB also lives on, so it must run before the camera pipeline.
    sensor_reader_init();
    indoor_camera_capture_init();
    hazard_infer_init();
#if CONFIG_LABGUARD_ESPDL_PROBE_RUN_ON_BOOT
    espdl_probe_run_once();
#endif
    risk_fusion_init();
    actuator_ctrl_init();

    publish_status();
    publish_event(LABGUARD_RISK_NORMAL, "indoor_boot", "camera_sensor_actuator_ready");

    xTaskCreate(indoor_task, "indoor_task", 8192, NULL, 5, NULL);
    xTaskCreate(camera_publish_task, "camera_publish", 8192, NULL, 4, NULL);
    xTaskCreate(heartbeat_task, "indoor_heartbeat", 4096, NULL, 4, NULL);
}
