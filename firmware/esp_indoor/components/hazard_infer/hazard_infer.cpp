#include "hazard_infer.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <list>
#include <memory>
#include <new>
#include <string>
#include <sys/stat.h>
#include <type_traits>
#include <vector>

#include "dl_detect_base.hpp"
#include "dl_detect_define.hpp"
#include "dl_detect_postprocessor.hpp"
#include "dl_image_define.hpp"
#include "dl_image_preprocessor.hpp"
#include "dl_model_base.hpp"
#include "dl_tensor_base.hpp"
#include "esp_check.h"
#include "esp_log.h"
#include "sdkconfig.h"

using dl::Model;
using dl::TensorBase;
using dl::detect::DetectImpl;
using dl::detect::DetectPostprocessor;
using dl::detect::result_t;
using dl::image::ImagePreprocessor;
using dl::image::img_t;
using dl::image::DL_IMAGE_PIX_TYPE_RGB565LE;

namespace {

static const char *TAG = "hazard_infer";
static const char *MODEL_MOCK = "hazard_mock_v2";
static const char *MODEL_PLACEHOLDER = "hazard_onboard_pending";
static const char *MODEL_ESPDL = "lab_fire_smoke_espdl";

static constexpr uint16_t MODEL_INPUT_WIDTH = 320;
static constexpr uint16_t MODEL_INPUT_HEIGHT = 320;
static constexpr size_t MODEL_PLACEHOLDER_ONNX_SIZE_BYTES = 12372584;
static constexpr int MODEL_FEATURES_MIN = 6;
static constexpr int MODEL_CLASS_FIRE = 0;
static constexpr int MODEL_CLASS_SMOKE = 1;

class FireSmokePostprocessor : public DetectPostprocessor {
public:
    FireSmokePostprocessor(Model *model,
                           ImagePreprocessor *image_preprocessor,
                           float score_thr,
                           float nms_thr,
                           int top_k) :
        DetectPostprocessor(model, image_preprocessor, score_thr, nms_thr, top_k)
    {
    }

    void postprocess() override
    {
        TensorBase *output = m_model->get_output();
        if (output == nullptr || output->data == nullptr || output->shape.size() != 3) {
            ESP_LOGE(TAG, "unexpected model output");
            return;
        }

        int feature_dim = 0;
        int box_count = 0;
        bool channels_first = false;
        if (output->shape[1] >= MODEL_FEATURES_MIN && output->shape[2] > 0) {
            feature_dim = output->shape[1];
            box_count = output->shape[2];
            channels_first = true;
        } else if (output->shape[2] >= MODEL_FEATURES_MIN && output->shape[1] > 0) {
            feature_dim = output->shape[2];
            box_count = output->shape[1];
            channels_first = false;
        } else {
            ESP_LOGE(TAG,
                     "unsupported output shape=[%d,%d,%d]",
                     output->shape[0],
                     output->shape[1],
                     output->shape[2]);
            return;
        }

        switch (output->dtype) {
        case dl::DATA_TYPE_FLOAT:
            parse_tensor<float>(output, feature_dim, box_count, channels_first, 1.0f);
            break;
        case dl::DATA_TYPE_INT8:
            parse_tensor<int8_t>(output, feature_dim, box_count, channels_first, DL_SCALE((int)output->exponent));
            break;
        case dl::DATA_TYPE_INT16:
            parse_tensor<int16_t>(output, feature_dim, box_count, channels_first, DL_SCALE((int)output->exponent));
            break;
        case dl::DATA_TYPE_INT32:
            parse_tensor<int32_t>(output, feature_dim, box_count, channels_first, DL_SCALE((int)output->exponent));
            break;
        default:
            ESP_LOGE(TAG, "unsupported output dtype=%d", (int)output->dtype);
            return;
        }

        nms();
    }

private:
    template <typename T>
    static float decode_value(T value, float scale)
    {
        if constexpr (std::is_same_v<T, float>) {
            (void)scale;
            return value;
        } else if constexpr (std::is_same_v<T, int8_t> || std::is_same_v<T, int16_t>) {
            return dl::dequantize(value, scale);
        } else {
            return (float)value * scale;
        }
    }

    template <typename T>
    void parse_tensor(TensorBase *output,
                      int feature_dim,
                      int box_count,
                      bool channels_first,
                      float scale)
    {
        const T *data = static_cast<const T *>(output->data);
        const float inv_resize_scale_x = m_image_preprocessor->get_resize_scale_x(true);
        const float inv_resize_scale_y = m_image_preprocessor->get_resize_scale_y(true);
        const int border_left = m_image_preprocessor->get_border_left();
        const int border_top = m_image_preprocessor->get_border_top();

        auto tensor_at = [&](int feature_idx, int box_idx) -> float {
            size_t offset = 0;
            if (channels_first) {
                offset = (size_t)feature_idx * (size_t)box_count + (size_t)box_idx;
            } else {
                offset = (size_t)box_idx * (size_t)feature_dim + (size_t)feature_idx;
            }
            return decode_value<T>(data[offset], scale);
        };

        for (int box_idx = 0; box_idx < box_count; ++box_idx) {
            float best_score = 0.0f;
            int best_class = -1;
            for (int class_idx = 4; class_idx < feature_dim; ++class_idx) {
                const float score = tensor_at(class_idx, box_idx);
                if (score > best_score) {
                    best_score = score;
                    best_class = class_idx - 4;
                }
            }

            if (best_class < 0 || best_score < m_score_thr) {
                continue;
            }

            const float center_x = tensor_at(0, box_idx);
            const float center_y = tensor_at(1, box_idx);
            const float width = tensor_at(2, box_idx);
            const float height = tensor_at(3, box_idx);
            if (width <= 0.0f || height <= 0.0f) {
                continue;
            }

            const float left = center_x - width * 0.5f;
            const float top = center_y - height * 0.5f;
            const float right = center_x + width * 0.5f;
            const float bottom = center_y + height * 0.5f;

            result_t detection = {
                .category = best_class,
                .score = best_score,
                .box = {
                    (int)std::lround((left - (float)border_left) * inv_resize_scale_x),
                    (int)std::lround((top - (float)border_top) * inv_resize_scale_y),
                    (int)std::lround((right - (float)border_left) * inv_resize_scale_x),
                    (int)std::lround((bottom - (float)border_top) * inv_resize_scale_y),
                },
                .keypoint = {},
            };
            m_box_list.insert(std::upper_bound(m_box_list.begin(), m_box_list.end(), detection, dl::detect::greater_box),
                              detection);
            if ((int)m_box_list.size() > m_top_k) {
                m_box_list.pop_back();
            }
        }
    }
};

class FireSmokeDetector : public DetectImpl {
public:
    FireSmokeDetector(const char *model_path, float score_thr, float nms_thr, int top_k)
    {
        const bool param_copy = true;
        m_model = new Model(model_path,
                            fbs::MODEL_LOCATION_IN_SDCARD,
                            CONFIG_LABGUARD_HAZARD_MAX_INTERNAL_BYTES,
                            dl::MEMORY_MANAGER_GREEDY,
                            nullptr,
                            param_copy);
        m_model->minimize();
        m_image_preprocessor = new ImagePreprocessor(m_model, {0, 0, 0}, {255, 255, 255});
        m_image_preprocessor->enable_letterbox({114, 114, 114});
        m_postprocessor = new FireSmokePostprocessor(m_model, m_image_preprocessor, score_thr, nms_thr, top_k);
    }
};

static labguard_profile_t s_profile = LABGUARD_PROFILE_AUTO;
static labguard_hazard_backend_t s_backend = LABGUARD_HAZARD_BACKEND_ONBOARD_PENDING;
static hazard_infer_runtime_info_t s_runtime_info = {
    .input_width = MODEL_INPUT_WIDTH,
    .input_height = MODEL_INPUT_HEIGHT,
    .workspace_bytes = 0,
    .model_file_present = false,
    .runtime_available = false,
};
static std::unique_ptr<FireSmokeDetector> s_detector;
static uint32_t s_cycle;
static bool s_model_ready;
static bool s_logged_missing_model;
static bool s_logged_frame_seen;
static bool s_logged_shape_once;

static size_t best_onnx_size(void)
{
    return MODEL_PLACEHOLDER_ONNX_SIZE_BYTES;
}

static bool file_exists(const char *path, size_t *size_out)
{
    if (path == nullptr || path[0] == '\0') {
        return false;
    }

    struct stat st = {};
    if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) {
        return false;
    }

    if (size_out != nullptr) {
        *size_out = (size_t)st.st_size;
    }
    return true;
}

static void fill_mock_result(labguard_hazard_result_t *out)
{
    s_cycle++;
    out->smoke = false;
    out->flame = false;
    out->score_smoke = 0.03f;
    out->score_flame = 0.02f;
    out->detection_count = 0;
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
        out->detection_count = 2;
        break;
    case LABGUARD_PROFILE_AUTO:
    default:
        switch (s_cycle % 14U) {
        case 5:
        case 6:
            out->smoke = true;
            out->score_smoke = 0.58f;
            out->detection_count = 1;
            break;
        case 10:
        case 11:
            out->smoke = true;
            out->score_smoke = 0.82f;
            out->detection_count = 1;
            break;
        case 12:
            out->smoke = true;
            out->flame = true;
            out->score_smoke = 0.90f;
            out->score_flame = 0.93f;
            out->detection_count = 2;
            break;
        default:
            break;
        }
        break;
    }
}

static void fill_empty_result(labguard_hazard_result_t *out, const char *model_name)
{
    std::memset(out, 0, sizeof(*out));
    out->model = model_name;
}

static esp_err_t load_model_placeholder(void)
{
    const size_t model_size = best_onnx_size();
    s_runtime_info.model_file_present = true;
    s_runtime_info.runtime_available = false;
    s_runtime_info.workspace_bytes = (size_t)MODEL_INPUT_WIDTH * MODEL_INPUT_HEIGHT * 3;
    s_model_ready = false;

    ESP_LOGW(TAG,
             "ONNX model is available offline (%u bytes), but this build has no ESP-DL runtime or .espdl artifact yet",
             (unsigned)model_size);
    return ESP_ERR_NOT_SUPPORTED;
}

static esp_err_t run_model_placeholder(const void *frame, size_t frame_len, labguard_hazard_result_t *out)
{
    if (frame == nullptr || frame_len == 0 || out == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    const uint16_t *pixels = static_cast<const uint16_t *>(frame);
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

    fill_empty_result(out, MODEL_PLACEHOLDER);
    out->score_flame = std::min(hot_ratio * 10.0f, 0.99f);
    out->score_smoke = std::min(smoke_ratio * 6.0f, 0.95f);
    out->flame = out->score_flame >= ((float)CONFIG_LABGUARD_HAZARD_FLAME_THRESHOLD_PERCENT / 100.0f);
    out->smoke = out->score_smoke >= ((float)CONFIG_LABGUARD_HAZARD_SMOKE_THRESHOLD_PERCENT / 100.0f);
    out->detection_count = (out->flame ? 1 : 0) + (out->smoke ? 1 : 0);

    return ESP_OK;
}

static esp_err_t load_espdl_detector(void)
{
    size_t model_size = 0;
    if (!file_exists(CONFIG_LABGUARD_HAZARD_MODEL_SDCARD_PATH, &model_size)) {
        s_runtime_info.model_file_present = false;
        s_runtime_info.runtime_available = false;
        s_runtime_info.workspace_bytes = 0;
        s_model_ready = false;
        ESP_LOGW(TAG, "ESP-DL model not found at %s", CONFIG_LABGUARD_HAZARD_MODEL_SDCARD_PATH);
        return ESP_ERR_NOT_FOUND;
    }

    s_runtime_info.model_file_present = true;
    s_runtime_info.runtime_available = false;
    s_runtime_info.workspace_bytes = 0;

    const float score_thr = (float)CONFIG_LABGUARD_HAZARD_SCORE_THRESHOLD_PERCENT / 100.0f;
    const float nms_thr = (float)CONFIG_LABGUARD_HAZARD_NMS_THRESHOLD_PERCENT / 100.0f;
    s_detector = std::make_unique<FireSmokeDetector>(CONFIG_LABGUARD_HAZARD_MODEL_SDCARD_PATH,
                                                     score_thr,
                                                     nms_thr,
                                                     CONFIG_LABGUARD_HAZARD_TOP_K);

    s_runtime_info.runtime_available = true;
    s_runtime_info.workspace_bytes = model_size;
    s_model_ready = true;
    s_backend = LABGUARD_HAZARD_BACKEND_ESPDL_SDCARD;
    ESP_LOGI(TAG,
             "ESP-DL detector ready, model=%s, size=%u bytes",
             CONFIG_LABGUARD_HAZARD_MODEL_SDCARD_PATH,
             (unsigned)model_size);
    return ESP_OK;
}

static esp_err_t run_espdl_detector(const void *frame,
                                    size_t frame_len,
                                    uint16_t width,
                                    uint16_t height,
                                    labguard_hazard_result_t *out)
{
    if (frame == nullptr || frame_len == 0 || out == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_detector) {
        return ESP_ERR_INVALID_STATE;
    }

    img_t image = {
        .data = const_cast<void *>(frame),
        .width = width,
        .height = height,
        .pix_type = DL_IMAGE_PIX_TYPE_RGB565LE,
    };

    std::list<result_t> &results = s_detector->run(image);
    fill_empty_result(out, MODEL_ESPDL);

    const float flame_thr = (float)CONFIG_LABGUARD_HAZARD_FLAME_THRESHOLD_PERCENT / 100.0f;
    const float smoke_thr = (float)CONFIG_LABGUARD_HAZARD_SMOKE_THRESHOLD_PERCENT / 100.0f;

    for (const result_t &result : results) {
        if (result.category == MODEL_CLASS_FIRE) {
            out->score_flame = std::max(out->score_flame, result.score);
        } else if (result.category == MODEL_CLASS_SMOKE) {
            out->score_smoke = std::max(out->score_smoke, result.score);
        }
    }

    out->detection_count = (uint16_t)results.size();
    out->flame = out->score_flame >= flame_thr;
    out->smoke = out->score_smoke >= smoke_thr;

    if (!s_logged_shape_once) {
        TensorBase *output = s_detector->get_raw_model()->get_output();
        if (output != nullptr) {
            ESP_LOGI(TAG,
                     "ESP-DL output shape=[%d,%d,%d], dtype=%d",
                     output->shape.size() > 0 ? output->shape[0] : -1,
                     output->shape.size() > 1 ? output->shape[1] : -1,
                     output->shape.size() > 2 ? output->shape[2] : -1,
                     (int)output->dtype);
        }
        s_logged_shape_once = true;
    }

    return ESP_OK;
}

} // namespace

extern "C" esp_err_t hazard_infer_init(void)
{
    s_profile = LABGUARD_PROFILE_AUTO;
    s_backend = LABGUARD_HAZARD_BACKEND_ONBOARD_PENDING;
    s_cycle = 0;
    s_logged_missing_model = false;
    s_logged_frame_seen = false;
    s_logged_shape_once = false;
    s_detector.reset();
    s_model_ready = false;
    s_runtime_info = {
        .input_width = MODEL_INPUT_WIDTH,
        .input_height = MODEL_INPUT_HEIGHT,
        .workspace_bytes = 0,
        .model_file_present = false,
        .runtime_available = false,
    };

#if CONFIG_LABGUARD_HAZARD_ENABLE_ESPDL_BACKEND
    esp_err_t ret = load_espdl_detector();
    if (ret == ESP_OK) {
        ESP_LOGI(TAG,
                 "hazard inference initialized, backend=espdl_sdcard, model_present=%d, runtime_available=%d",
                 s_runtime_info.model_file_present,
                 s_runtime_info.runtime_available);
        return ESP_OK;
    }

    if (!s_logged_missing_model) {
        ESP_LOGW(TAG,
                 "ESP-DL detector unavailable (%s); falling back to provisional hazard path",
                 esp_err_to_name(ret));
        s_logged_missing_model = true;
    }
#endif

    ret = load_model_placeholder();
    if (ret != ESP_OK) {
        ESP_LOGI(TAG, "on-board model path not fully ready; using provisional fallback until ESP-DL model is available");
    }

    ESP_LOGI(TAG,
             "hazard inference initialized, backend=onboard_pending, model_present=%d, runtime_available=%d",
             s_runtime_info.model_file_present,
             s_runtime_info.runtime_available);
    return ESP_OK;
}

extern "C" void hazard_infer_set_profile(labguard_profile_t profile)
{
    s_profile = profile;
}

extern "C" labguard_profile_t hazard_infer_get_profile(void)
{
    return s_profile;
}

extern "C" labguard_hazard_backend_t hazard_infer_get_backend(void)
{
    return s_backend;
}

extern "C" bool hazard_infer_model_is_ready(void)
{
    return s_model_ready;
}

extern "C" const hazard_infer_runtime_info_t *hazard_infer_get_runtime_info(void)
{
    return &s_runtime_info;
}

extern "C" esp_err_t hazard_infer_run(const void *frame,
                                      size_t frame_len,
                                      uint16_t frame_width,
                                      uint16_t frame_height,
                                      labguard_hazard_result_t *out)
{
    if (out == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    if (frame != nullptr && frame_len > 0 && !s_logged_frame_seen) {
        ESP_LOGI(TAG, "camera frame input reached hazard_infer_run (len=%u)", (unsigned)frame_len);
        s_logged_frame_seen = true;
    }

    if (s_profile != LABGUARD_PROFILE_AUTO) {
        fill_mock_result(out);
        return ESP_OK;
    }

    if (s_detector && frame != nullptr && frame_len > 0 && frame_width > 0 && frame_height > 0) {
        esp_err_t ret = run_espdl_detector(frame, frame_len, frame_width, frame_height, out);
        if (ret == ESP_OK) {
            return ESP_OK;
        }
        ESP_LOGW(TAG, "ESP-DL inference failed: %s", esp_err_to_name(ret));
    }

    if (s_backend == LABGUARD_HAZARD_BACKEND_ONBOARD_PENDING && frame != nullptr && frame_len > 0) {
        esp_err_t ret = run_model_placeholder(frame, frame_len, out);
        if (ret == ESP_OK) {
            return ESP_OK;
        }
        if (!s_logged_missing_model) {
            ESP_LOGI(TAG, "on-board model runtime is incomplete; falling back to mock inference output");
            s_logged_missing_model = true;
        }
    }

    if (frame == nullptr || frame_len == 0) {
        fill_empty_result(out, s_model_ready ? MODEL_ESPDL : MODEL_PLACEHOLDER);
        return ESP_OK;
    }

    fill_mock_result(out);
    return ESP_OK;
}
