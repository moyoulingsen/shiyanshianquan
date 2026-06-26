#include "hazard_infer.h"

#include <stdint.h>
#include <string.h>

#include "esp_log.h"

static const char *TAG = "hazard_infer";
static const char *MODEL_MOCK = "hazard_mock_v2";
static const char *MODEL_PLACEHOLDER = "hazard_onboard_pending";

static const uint16_t MODEL_INPUT_WIDTH = 320;
static const uint16_t MODEL_INPUT_HEIGHT = 320;
static const size_t MODEL_ONNX_SIZE_BYTES = 12372584;

static labguard_profile_t s_profile = LABGUARD_PROFILE_AUTO;
static labguard_hazard_backend_t s_backend = LABGUARD_HAZARD_BACKEND_ONBOARD_PENDING;
static hazard_infer_runtime_info_t s_runtime_info = {
    .input_width = MODEL_INPUT_WIDTH,
    .input_height = MODEL_INPUT_HEIGHT,
};
static uint32_t s_cycle;
static bool s_model_ready;
static bool s_logged_missing_model;
static bool s_logged_frame_seen;

static size_t best_onnx_size(void)
{
    return MODEL_ONNX_SIZE_BYTES;
}

static esp_err_t load_model_placeholder(void)
{
    const size_t model_size = best_onnx_size();
    s_runtime_info.model_file_present = true;
    s_runtime_info.runtime_available = false;
    s_runtime_info.workspace_bytes = (size_t)MODEL_INPUT_WIDTH * MODEL_INPUT_HEIGHT * 3;
    s_model_ready = false;

    if (!s_runtime_info.model_file_present) {
        ESP_LOGW(TAG, "ONNX model metadata missing");
        return ESP_ERR_NOT_FOUND;
    }

    ESP_LOGW(TAG,
             "ONNX model is available offline (%u bytes), but this build has no ESP-DL runtime or .espdl artifact yet",
             (unsigned)model_size);
    return ESP_ERR_NOT_SUPPORTED;
}

static esp_err_t run_model_placeholder(const void *frame, size_t frame_len, labguard_hazard_result_t *out)
{
    if (frame == NULL || frame_len == 0 || out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    const uint16_t *pixels = (const uint16_t *)frame;
    const size_t pixel_count = frame_len / sizeof(uint16_t);
    if (pixel_count == 0) {
        return ESP_ERR_INVALID_SIZE;
    }

    uint32_t hot_pixels = 0;
    uint32_t smoky_pixels = 0;
    for (size_t i = 0; i < pixel_count; ++i) {
        const uint16_t pixel = pixels[i];
        const uint8_t r = (uint8_t)((pixel >> 11) & 0x1F);
        const uint8_t g = (uint8_t)((pixel >> 5) & 0x3F);
        const uint8_t b = (uint8_t)(pixel & 0x1F);

        if (r >= 20 && g >= 10 && g <= 30 && b <= 10) {
            hot_pixels++;
        }
        if (r >= 10 && g >= 10 && b >= 10 && r <= 24 && g <= 32 && b <= 24) {
            smoky_pixels++;
        }
    }

    const float hot_ratio = (float)hot_pixels / (float)pixel_count;
    const float smoke_ratio = (float)smoky_pixels / (float)pixel_count;

    memset(out, 0, sizeof(*out));
    out->score_flame = hot_ratio * 10.0f;
    if (out->score_flame > 0.99f) {
        out->score_flame = 0.99f;
    }
    out->score_smoke = smoke_ratio * 6.0f;
    if (out->score_smoke > 0.95f) {
        out->score_smoke = 0.95f;
    }
    out->flame = out->score_flame >= 0.70f;
    out->smoke = out->score_smoke >= 0.50f;
    out->model = MODEL_PLACEHOLDER;

    return ESP_OK;
}

static void fill_mock_result(labguard_hazard_result_t *out)
{
    s_cycle++;
    out->smoke = false;
    out->flame = false;
    out->score_smoke = 0.03f;
    out->score_flame = 0.02f;
    out->model = MODEL_MOCK;

    switch (s_profile) {
    case LABGUARD_PROFILE_NORMAL:
        break;
    case LABGUARD_PROFILE_WARNING:
        out->smoke = true;
        out->score_smoke = 0.56f;
        break;
    case LABGUARD_PROFILE_ALARM:
        out->smoke = true;
        out->score_smoke = 0.86f;
        break;
    case LABGUARD_PROFILE_EMERGENCY:
        out->smoke = true;
        out->flame = true;
        out->score_smoke = 0.88f;
        out->score_flame = 0.91f;
        break;
    case LABGUARD_PROFILE_AUTO:
    default:
        switch (s_cycle % 14U) {
        case 5:
        case 6:
            out->smoke = true;
            out->score_smoke = 0.58f;
            break;
        case 10:
        case 11:
            out->smoke = true;
            out->score_smoke = 0.82f;
            break;
        case 12:
            out->smoke = true;
            out->flame = true;
            out->score_smoke = 0.90f;
            out->score_flame = 0.93f;
            break;
        default:
            break;
        }
        break;
    }
}

esp_err_t hazard_infer_init(void)
{
    s_profile = LABGUARD_PROFILE_AUTO;
    s_backend = LABGUARD_HAZARD_BACKEND_ONBOARD_PENDING;
    s_cycle = 0;
    s_logged_missing_model = false;
    s_logged_frame_seen = false;

    esp_err_t ret = load_model_placeholder();
    if (ret != ESP_OK) {
        ESP_LOGI(TAG, "on-board model path not fully ready; using provisional frame heuristics until ESP-DL runtime is wired");
    }

    ESP_LOGI(TAG, "hazard inference initialized, backend=onboard_pending, model_present=%d, runtime_available=%d",
             s_runtime_info.model_file_present, s_runtime_info.runtime_available);
    return ESP_OK;
}

void hazard_infer_set_profile(labguard_profile_t profile)
{
    s_profile = profile;
}

labguard_profile_t hazard_infer_get_profile(void)
{
    return s_profile;
}

labguard_hazard_backend_t hazard_infer_get_backend(void)
{
    return s_backend;
}

bool hazard_infer_model_is_ready(void)
{
    return s_model_ready;
}

const hazard_infer_runtime_info_t *hazard_infer_get_runtime_info(void)
{
    return &s_runtime_info;
}

esp_err_t hazard_infer_run(const void *frame, size_t frame_len, labguard_hazard_result_t *out)
{
    if (out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (frame != NULL && frame_len > 0 && !s_logged_frame_seen) {
        ESP_LOGI(TAG, "camera frame input reached hazard_infer_run (len=%u)", (unsigned)frame_len);
        s_logged_frame_seen = true;
    }

    if (s_backend == LABGUARD_HAZARD_BACKEND_ONBOARD_PENDING) {
        esp_err_t ret = run_model_placeholder(frame, frame_len, out);
        if (ret == ESP_OK) {
            return ESP_OK;
        }
        if (!s_logged_missing_model) {
            ESP_LOGI(TAG, "on-board model runtime is incomplete; falling back to mock inference output");
            s_logged_missing_model = true;
        }
    }

    fill_mock_result(out);
    return ESP_OK;
}
