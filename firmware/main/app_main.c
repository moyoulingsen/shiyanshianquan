#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "actuator_ctrl.h"
#include "audio_prompt.h"
#include "driver/jpeg_encode.h"
#include "driver/sdmmc_host.h"
#include "driver/usb_serial_jtag.h"
#include "event_log.h"
#include "esp_idf_version.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_vfs_fat.h"
#include "espdl_probe.h"
#include "indoor_camera_capture.h"
#include "labguard_common.h"
#include "labguard_net.h"
#include "mbedtls/base64.h"
#include "risk_fusion.h"
#include "sd_pwr_ctrl_by_on_chip_ldo.h"
#include "sdmmc_cmd.h"
#include "sdkconfig.h"
#include "sensor_reader.h"

static const char *TAG = "labguard_device";

#define CAMERA_PREVIEW_WIDTH 80
#define CAMERA_PREVIEW_HEIGHT 48
#define CAMERA_PREVIEW_CHANNELS 3
#define CAMERA_PREVIEW_JPEG_QUALITY 35
#define CAMERA_PREVIEW_FPS 4
#define CAMERA_PREVIEW_INTERVAL_MS (1000 / CAMERA_PREVIEW_FPS)
#define CAMERA_PREVIEW_RGB_BYTES (CAMERA_PREVIEW_WIDTH * CAMERA_PREVIEW_HEIGHT * CAMERA_PREVIEW_CHANNELS)
#define CAMERA_PREVIEW_JPEG_BYTES (CAMERA_PREVIEW_RGB_BYTES)
#define CAMERA_PREVIEW_B64_BYTES (((CAMERA_PREVIEW_JPEG_BYTES + 2) / 3) * 4 + 1)
#define CAMERA_PREVIEW_JSON_BYTES (CAMERA_PREVIEW_B64_BYTES + 320)
#define SERIAL_COMMAND_BUFFER_BYTES 512
#define SD_CARD_MOUNT_POINT "/sdcard"
#define SD_LDO_CHAN_ID 4
#define MASTER_ON_PUMP_DELAY_MS 300

#if CONFIG_IDF_TARGET_ESP32P4 && CONFIG_ESP_HOSTED_SDIO_HOST_INTERFACE && (ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 0, 0))
#define LABGUARD_HOSTED_SDMMC_HOST_INIT 1
#else
#define LABGUARD_HOSTED_SDMMC_HOST_INIT 0
#endif

static uint8_t s_preview_rgb[CAMERA_PREVIEW_RGB_BYTES];
static unsigned char s_preview_b64[CAMERA_PREVIEW_B64_BYTES];
static uint8_t *s_preview_jpeg;
static size_t s_preview_jpeg_capacity;
static jpeg_encoder_handle_t s_jpeg_encoder;
static bool s_manual_audio_loop_requested;
static bool s_auto_audio_loop_requested;
static bool s_auto_audio_muted_until_normal;

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
        .node = LABGUARD_NODE_DEVICE,
        .online = true,
        .uptime_s = now_seconds(),
        .wifi_rssi = labguard_net_get_rssi(),
        .version = LABGUARD_VERSION,
        .audio_looping = audio_prompt_is_looping(),
        .light_on = actuator_ctrl_get_light(),
        .timestamp = now_seconds(),
    };
    publish_json(LABGUARD_TOPIC_DEVICE_STATUS, labguard_status_to_json(&status));
}

static void publish_event(labguard_risk_level_t level, const char *event, const char *actions)
{
    labguard_event_t message = {
        .node = LABGUARD_NODE_DEVICE,
        .level = level,
        .source = "device",
        .event = event,
        .actions = actions,
        .timestamp = now_seconds(),
    };
    event_log_append_event(&message);
    publish_json(LABGUARD_TOPIC_EVENT, labguard_event_to_json(&message));
}

static void publish_current_risk_state(void)
{
    publish_json(LABGUARD_TOPIC_DEVICE_RISK,
                 labguard_risk_state_to_json(actuator_ctrl_get_last_risk()));
}

static void publish_audio_failure_event(const char *event, esp_err_t ret)
{
    char actions[96];
    snprintf(actions, sizeof(actions), "audio_error_%s", esp_err_to_name(ret));
    publish_event(LABGUARD_RISK_WARNING, event, actions);
}

static void sync_auto_audio_loop(bool should_loop)
{
    if (should_loop) {
        bool was_requested = s_auto_audio_loop_requested;
        s_auto_audio_loop_requested = true;

        if (s_auto_audio_muted_until_normal) {
            if (!was_requested) {
                publish_event(LABGUARD_RISK_ALARM, "auto_audio_muted", "user_muted_until_normal");
            }
            return;
        }

        if (audio_prompt_is_looping()) {
            if (!was_requested) {
                publish_event(LABGUARD_RISK_ALARM, "auto_audio_on", "audio_loop_already_on");
            }
            return;
        }

        esp_err_t ret = audio_prompt_start_loop();
        if (ret == ESP_OK) {
            publish_status();
            publish_event(LABGUARD_RISK_ALARM, "auto_audio_on", "audio_loop_on");
        } else {
            s_auto_audio_loop_requested = false;
            publish_audio_failure_event("auto_audio_on_failed", ret);
        }
        return;
    }

    s_auto_audio_muted_until_normal = false;
    if (!s_auto_audio_loop_requested) {
        return;
    }
    s_auto_audio_loop_requested = false;

    if (!s_manual_audio_loop_requested && audio_prompt_is_looping()) {
        audio_prompt_stop_loop();
        publish_status();
        publish_event(LABGUARD_RISK_NORMAL, "auto_audio_off", "audio_loop_off");
    }
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

#if LABGUARD_HOSTED_SDMMC_HOST_INIT
static esp_err_t sdmmc_host_init_skip(void)
{
    return ESP_OK;
}

static esp_err_t sdmmc_host_deinit_skip(int slot)
{
    (void)slot;
    return ESP_OK;
}
#endif

static void init_sd_card(void)
{
    static sd_pwr_ctrl_handle_t s_sd_pwr_ctrl_handle = NULL;

    if (s_sd_pwr_ctrl_handle == NULL) {
        const sd_pwr_ctrl_ldo_config_t ldo_cfg = {
            .ldo_chan_id = SD_LDO_CHAN_ID,
        };
        esp_err_t ldo_ret = sd_pwr_ctrl_new_on_chip_ldo(&ldo_cfg, &s_sd_pwr_ctrl_handle);
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
    host.pwr_ctrl_handle = s_sd_pwr_ctrl_handle;
#if LABGUARD_HOSTED_SDMMC_HOST_INIT
    host.init = sdmmc_host_init_skip;
    host.deinit_p = sdmmc_host_deinit_skip;
#endif

    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
    slot_config.width = 4;
    slot_config.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;
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
    if (command == NULL || !labguard_command_targets_node(command, LABGUARD_NODE_DEVICE)) {
        return;
    }

    switch (command->type) {
    case LABGUARD_CMD_RESET:
    {
        const bool audio_was_looping = audio_prompt_is_looping();
        const bool auto_alarm_was_active = s_auto_audio_loop_requested ||
                                           actuator_ctrl_get_last_risk()->action_alarm;
        actuator_ctrl_set_fan(false);
        actuator_ctrl_set_pump(false);
        actuator_ctrl_clear_manual_overrides();
        actuator_ctrl_set_light(false);
        s_manual_audio_loop_requested = false;
        s_auto_audio_loop_requested = false;
        s_auto_audio_muted_until_normal = auto_alarm_was_active || audio_was_looping;
        audio_prompt_stop_loop();
        publish_current_risk_state();
        publish_status();
        publish_event(LABGUARD_RISK_NORMAL, "device_reset", "fan_off_pump_off_light_off_audio_off");
        break;
    }
    case LABGUARD_CMD_FAN_ON:
        actuator_ctrl_set_fan_level(command->level_pct < 0 ? 100 : command->level_pct);
        actuator_ctrl_set_fan(true);
        publish_current_risk_state();
        publish_event(LABGUARD_RISK_NORMAL, "manual_fan_on", "fan_on");
        break;
    case LABGUARD_CMD_FAN_OFF:
        actuator_ctrl_set_fan(false);
        publish_current_risk_state();
        publish_event(LABGUARD_RISK_NORMAL, "manual_fan_off", "fan_off");
        break;
    case LABGUARD_CMD_PUMP_ON:
        actuator_ctrl_set_pump_level(command->level_pct < 0 ? 100 : command->level_pct);
        actuator_ctrl_set_pump(true);
        publish_current_risk_state();
        publish_event(LABGUARD_RISK_NORMAL, "manual_pump_on", "pump_on");
        break;
    case LABGUARD_CMD_PUMP_OFF:
        actuator_ctrl_set_pump(false);
        publish_current_risk_state();
        publish_event(LABGUARD_RISK_NORMAL, "manual_pump_off", "pump_off");
        break;
    case LABGUARD_CMD_ALARM_ON:
    {
        esp_err_t ret = audio_prompt_play(AUDIO_PROMPT_TOXIC_GAS);
        if (ret == ESP_OK) {
            publish_event(LABGUARD_RISK_ALARM, "manual_alarm_on", "voice_alarm_on");
        } else {
            publish_audio_failure_event("manual_alarm_on_failed", ret);
        }
        break;
    }
    case LABGUARD_CMD_ALARM_OFF:
        publish_event(LABGUARD_RISK_NORMAL, "manual_alarm_off", "voice_alarm_off");
        break;
    case LABGUARD_CMD_AUDIO_ON:
    {
        s_auto_audio_muted_until_normal = false;
        esp_err_t ret = audio_prompt_start_loop();
        if (ret == ESP_OK) {
            s_manual_audio_loop_requested = true;
            publish_status();
            publish_event(LABGUARD_RISK_NORMAL, "manual_audio_on", "audio_loop_on");
        } else {
            publish_audio_failure_event("manual_audio_on_failed", ret);
        }
        break;
    }
    case LABGUARD_CMD_AUDIO_OFF:
    {
        const bool auto_alarm_was_active = s_auto_audio_loop_requested ||
                                           actuator_ctrl_get_last_risk()->action_alarm;
        s_manual_audio_loop_requested = false;
        s_auto_audio_loop_requested = false;
        s_auto_audio_muted_until_normal = auto_alarm_was_active;
        audio_prompt_stop_loop();
        publish_status();
        publish_event(LABGUARD_RISK_NORMAL,
                      "manual_audio_off",
                      auto_alarm_was_active ? "audio_loop_off_auto_muted" : "audio_loop_off");
        break;
    }
    case LABGUARD_CMD_LIGHT_ON:
        actuator_ctrl_set_light(true);
        publish_status();
        publish_event(LABGUARD_RISK_NORMAL, "manual_light_on", "light_on");
        break;
    case LABGUARD_CMD_LIGHT_OFF:
        actuator_ctrl_set_light(false);
        publish_status();
        publish_event(LABGUARD_RISK_NORMAL, "manual_light_off", "light_off");
        break;
    case LABGUARD_CMD_MASTER_ON:
    {
        actuator_ctrl_set_fan_level(100);
        actuator_ctrl_set_fan(true);
        actuator_ctrl_set_light(true);
        esp_err_t ret = audio_prompt_play(AUDIO_PROMPT_TOXIC_GAS);
        vTaskDelay(pdMS_TO_TICKS(MASTER_ON_PUMP_DELAY_MS));
        actuator_ctrl_set_pump_level(100);
        actuator_ctrl_set_pump(true);
        publish_current_risk_state();
        publish_status();
        if (ret == ESP_OK) {
            publish_event(LABGUARD_RISK_ALARM,
                          "manual_master_on",
                          "fan_100_light_on_voice_alarm_then_pump_100");
        } else {
            publish_event(LABGUARD_RISK_WARNING,
                          "manual_master_on_partial",
                          "fan_100_light_on_voice_alarm_failed_then_pump_100");
            publish_audio_failure_event("manual_master_on_failed", ret);
        }
        break;
    }
    case LABGUARD_CMD_MASTER_OFF:
        actuator_ctrl_set_fan(false);
        actuator_ctrl_set_pump(false);
        actuator_ctrl_set_light(false);
        publish_current_risk_state();
        publish_status();
        publish_event(LABGUARD_RISK_NORMAL, "manual_master_off", "fan_off_pump_off_light_off");
        break;
    case LABGUARD_CMD_NONE:
    default:
        break;
    }
}

static void handle_command_json(const char *json)
{
    labguard_command_t command = {0};
    if (labguard_command_from_json(json, &command)) {
        apply_command(&command);
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
        handle_command_json(buffer);
    }
}

static void serial_command_task(void *arg)
{
    (void)arg;

    char buffer[SERIAL_COMMAND_BUFFER_BYTES];
    size_t len = 0;

    while (true) {
        uint8_t byte = 0;
        int read_len = usb_serial_jtag_read_bytes(&byte, 1, pdMS_TO_TICKS(100));
        if (read_len <= 0) {
            continue;
        }

        if (byte == '\r' || byte == '\n') {
            if (len > 0) {
                buffer[len] = '\0';
                ESP_LOGI(TAG, "serial command payload=%s", buffer);
                handle_command_json(buffer);
                len = 0;
            }
            continue;
        }

        if (len < sizeof(buffer) - 1) {
            buffer[len++] = (char)byte;
        } else {
            ESP_LOGW(TAG, "serial command too long, dropping line");
            len = 0;
        }
    }
}

static void init_serial_commands(void)
{
    if (usb_serial_jtag_is_driver_installed()) {
        ESP_LOGI(TAG, "USB serial command driver already installed");
    } else {
        usb_serial_jtag_driver_config_t usb_cfg = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
        usb_cfg.rx_buffer_size = 1024;
        usb_cfg.tx_buffer_size = 1024;
        esp_err_t ret = usb_serial_jtag_driver_install(&usb_cfg);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "USB serial command driver init failed: %s", esp_err_to_name(ret));
            return;
        }
    }

    xTaskCreate(serial_command_task, "serial_cmd", 4096, NULL, 5, NULL);
    ESP_LOGI(TAG, "serial command input ready on USB-Serial-JTAG");
}

static void device_task(void *arg)
{
    (void)arg;

    labguard_risk_level_t last_level = LABGUARD_RISK_NORMAL;
    bool logged_missing_frame = false;
    bool logged_camera_init_failure = false;
    bool logged_waiting_first_frame = false;
    bool logged_first_frame_received = false;

    while (true) {
        labguard_sensor_data_t sensor = {0};
        labguard_hazard_result_t hazard = {0};
        labguard_risk_state_t risk = {0};
        indoor_camera_frame_t frame = {0};
        indoor_camera_status_t camera_status = {0};

        sensor_reader_read(&sensor);
        indoor_camera_capture_get_status(&camera_status);

        esp_err_t frame_ret = indoor_camera_capture_get_latest_frame(&frame);
        if (frame_ret == ESP_OK) {
            if (!logged_first_frame_received) {
                ESP_LOGI(TAG,
                         "first camera frame received width=%u height=%u sequence=%lu",
                         (unsigned int)frame.width,
                         (unsigned int)frame.height,
                         (unsigned long)frame.sequence);
                logged_first_frame_received = true;
            }
            logged_missing_frame = false;
            logged_camera_init_failure = false;
            logged_waiting_first_frame = false;
            hazard.smoke = false;
            hazard.flame = false;
            hazard.score_smoke = 0.0f;
            hazard.score_flame = 0.0f;
            hazard.detection_count = 0;
            hazard.model = "camera_streaming";
        } else if (!camera_status.ready) {
            logged_missing_frame = false;
            logged_first_frame_received = false;
            if (!logged_camera_init_failure) {
                ESP_LOGW(TAG,
                         "camera init failed: state=%s error=%s",
                         indoor_camera_capture_state_to_string(camera_status.state),
                         esp_err_to_name(camera_status.last_error));
                logged_camera_init_failure = true;
            }
            logged_waiting_first_frame = false;
            hazard.smoke = false;
            hazard.flame = false;
            hazard.score_smoke = 0.0f;
            hazard.score_flame = 0.0f;
            hazard.detection_count = 0;
            hazard.model = "camera_init_failed";
        } else if (!camera_status.first_frame_received) {
            logged_missing_frame = false;
            logged_camera_init_failure = false;
            logged_first_frame_received = false;
            if (!logged_waiting_first_frame) {
                ESP_LOGI(TAG,
                         "camera waiting for first frame: state=%s",
                         indoor_camera_capture_state_to_string(camera_status.state));
                logged_waiting_first_frame = true;
            }
            hazard.smoke = false;
            hazard.flame = false;
            hazard.score_smoke = 0.0f;
            hazard.score_flame = 0.0f;
            hazard.detection_count = 0;
            hazard.model = "camera_waiting_first_frame";
        } else {
            logged_camera_init_failure = false;
            logged_waiting_first_frame = false;
            logged_first_frame_received = false;
            if (!logged_missing_frame) {
                ESP_LOGW(TAG,
                         "camera frame unavailable after ready: %s (state=%s)",
                         esp_err_to_name(frame_ret),
                         indoor_camera_capture_state_to_string(camera_status.state));
                logged_missing_frame = true;
            }
            hazard.smoke = false;
            hazard.flame = false;
            hazard.score_smoke = 0.0f;
            hazard.score_flame = 0.0f;
            hazard.detection_count = 0;
            hazard.model = "camera_frame_unavailable";
        }

        risk_fusion_evaluate(&sensor, &hazard, &risk);
        actuator_ctrl_apply_risk(&risk);
        const labguard_risk_state_t *actual_risk = actuator_ctrl_get_last_risk();
        sync_auto_audio_loop(actual_risk->action_alarm);

        publish_json(LABGUARD_TOPIC_DEVICE_SENSOR, labguard_sensor_data_to_json(&sensor));
        publish_json(LABGUARD_TOPIC_DEVICE_RISK, labguard_risk_state_to_json(actual_risk));

        if (risk.risk_level != last_level) {
            publish_event(risk.risk_level,
                          risk.risk_text,
                          risk.action_pump ? "alarm_fan_pump" :
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
    bool logged_waiting_first_frame = false;

    while (true) {
        indoor_camera_frame_t frame = {0};
        indoor_camera_status_t camera_status = {0};
        indoor_camera_capture_get_status(&camera_status);

        esp_err_t frame_ret = indoor_camera_capture_get_latest_frame(&frame);
        if (frame_ret == ESP_OK) {
            logged_missing_frame = false;
            logged_waiting_first_frame = false;
            if (build_camera_preview_json(&frame, preview_json, sizeof(preview_json))) {
                labguard_net_publish(LABGUARD_TOPIC_DEVICE_CAMERA, preview_json, 0, false);
            } else {
                ESP_LOGW(TAG, "skip camera preview publish: preview encode failed");
            }
        } else if (!camera_status.first_frame_received) {
            logged_missing_frame = false;
            if (!logged_waiting_first_frame) {
                ESP_LOGI(TAG,
                         "camera preview waiting for first frame: state=%s",
                         indoor_camera_capture_state_to_string(camera_status.state));
                logged_waiting_first_frame = true;
            }
        } else if (!logged_missing_frame) {
            ESP_LOGW(TAG,
                     "camera preview unavailable after ready: %s (state=%s)",
                     esp_err_to_name(frame_ret),
                     indoor_camera_capture_state_to_string(camera_status.state));
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
    ESP_LOGI(TAG, "LabGuard device starting, version=%s", LABGUARD_VERSION);

    event_log_init(NULL);
    s_manual_audio_loop_requested = false;
    s_auto_audio_loop_requested = false;
    s_auto_audio_muted_until_normal = false;

    labguard_net_config_t net_config = {
        .wifi_ssid = CONFIG_LABGUARD_WIFI_SSID,
        .wifi_password = CONFIG_LABGUARD_WIFI_PASSWORD,
        .mqtt_uri = CONFIG_LABGUARD_MQTT_URI,
        .message_cb = net_message_cb,
        .user_ctx = NULL,
    };
    esp_err_t net_init_ret = labguard_net_init(&net_config);
    if (net_init_ret != ESP_OK) {
        ESP_LOGE(TAG, "labguard_net_init failed: %s; continue boot without network", esp_err_to_name(net_init_ret));
    }
    esp_err_t net_start_ret = labguard_net_start();
    if (net_start_ret != ESP_OK) {
        ESP_LOGE(TAG, "labguard_net_start failed: %s; continue boot without network", esp_err_to_name(net_start_ret));
    }
    init_sd_card();
#if CONFIG_LABGUARD_ESPDL_PROBE_ENABLE && CONFIG_LABGUARD_ESPDL_PROBE_RUN_ON_BOOT
    espdl_probe_run_once();
#endif
    labguard_net_subscribe(LABGUARD_TOPIC_CMD_RESET, 1);
    labguard_net_subscribe(LABGUARD_TOPIC_CMD_TEST, 1);
    init_serial_commands();

    audio_prompt_init();
    sensor_reader_init();
    esp_err_t camera_ret = indoor_camera_capture_init();
    if (camera_ret != ESP_OK) {
        ESP_LOGE(TAG,
                 "indoor_camera_capture_init failed: %s (state=%s)",
                 esp_err_to_name(camera_ret),
                 indoor_camera_capture_state_to_string(indoor_camera_capture_get_state()));
    }
    risk_fusion_init();
    actuator_ctrl_init();

    publish_status();
    publish_event(LABGUARD_RISK_NORMAL,
                  "device_boot",
                  camera_ret == ESP_OK ? "camera_sensor_actuator_ready" : "camera_init_failed_sensor_actuator_ready");

    xTaskCreate(device_task, "device_task", 8192, NULL, 5, NULL);
    if (camera_ret == ESP_OK) {
        xTaskCreate(camera_publish_task, "camera_publish", 8192, NULL, 4, NULL);
    } else {
        ESP_LOGW(TAG, "skip camera_publish task because camera pipeline did not initialize");
    }
    xTaskCreate(heartbeat_task, "device_heartbeat", 4096, NULL, 4, NULL);
}
