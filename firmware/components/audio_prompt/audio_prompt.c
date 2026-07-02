#include "audio_prompt.h"

#include <ctype.h>
#include <dirent.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "driver/i2s_std.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#ifndef CONFIG_LABGUARD_AUDIO_PROMPT_SD_MODE_GPIO
#define CONFIG_LABGUARD_AUDIO_PROMPT_SD_MODE_GPIO -1
#endif

static const char *TAG = "audio_prompt";
static audio_prompt_t s_last_prompt = AUDIO_PROMPT_TOXIC_GAS;

#define AUDIO_PROMPT_DEFAULT_SAMPLE_RATE_HZ 16000
#define AUDIO_PROMPT_MIN_SAMPLE_RATE_HZ 8000
#define AUDIO_PROMPT_MAX_SAMPLE_RATE_HZ 48000
#define AUDIO_PROMPT_PCM_BITS_PER_SAMPLE 16
#define AUDIO_PROMPT_SAMPLE_BYTES 2
#define AUDIO_PROMPT_STEREO_FRAME_BYTES 4
#define AUDIO_PROMPT_IO_BUFFER_BYTES 1024
#define AUDIO_PROMPT_PATH_BYTES 160
#define AUDIO_PROMPT_MAX_LOOP_FILES 48
#define AUDIO_PROMPT_LOOP_TASK_STACK 8192
#define AUDIO_PROMPT_LOOP_IDLE_MS 1500
#define AUDIO_PROMPT_LOOP_BETWEEN_FILES_MS 80
#define AUDIO_PROMPT_QUIESCE_MS 120
#define AUDIO_PROMPT_AMP_SETTLE_MS 3

typedef struct __attribute__((packed)) {
    char riff[4];
    uint32_t overall_size;
    char wave[4];
} wav_riff_header_t;

typedef struct __attribute__((packed)) {
    char id[4];
    uint32_t size;
} wav_chunk_header_t;

typedef struct __attribute__((packed)) {
    uint16_t audio_format;
    uint16_t num_channels;
    uint32_t sample_rate;
    uint32_t byte_rate;
    uint16_t block_align;
    uint16_t bits_per_sample;
} wav_fmt_data_t;

typedef struct {
    uint16_t audio_format;
    uint16_t num_channels;
    uint32_t sample_rate;
    uint16_t bits_per_sample;
    uint16_t block_align;
    uint32_t data_size;
} wav_info_t;

static i2s_chan_handle_t s_tx_chan;
static bool s_i2s_ready;
static bool s_i2s_enabled;
static uint32_t s_i2s_sample_rate_hz;
static SemaphoreHandle_t s_play_mutex;
static TaskHandle_t s_loop_task;
static volatile bool s_loop_enabled;
static bool s_amp_enabled;

static void audio_prompt_set_amp_enabled(bool enabled)
{
#if CONFIG_LABGUARD_AUDIO_PROMPT_SD_MODE_GPIO >= 0
    gpio_set_level(CONFIG_LABGUARD_AUDIO_PROMPT_SD_MODE_GPIO, enabled ? 1 : 0);
#endif
    s_amp_enabled = enabled;
}

static esp_err_t audio_prompt_init_amp(void)
{
    s_amp_enabled = false;
#if CONFIG_LABGUARD_AUDIO_PROMPT_SD_MODE_GPIO >= 0
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << CONFIG_LABGUARD_AUDIO_PROMPT_SD_MODE_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t ret = gpio_config(&cfg);
    if (ret != ESP_OK) {
        return ret;
    }
    audio_prompt_set_amp_enabled(false);
    ESP_LOGI(TAG, "audio amplifier SD/MODE initialized GPIO%d",
             CONFIG_LABGUARD_AUDIO_PROMPT_SD_MODE_GPIO);
#endif
    return ESP_OK;
}

static bool prompt_file_path(audio_prompt_t prompt, char *path, size_t path_size)
{
    (void)prompt;
    if (path == NULL || path_size == 0) {
        return false;
    }
    int len = snprintf(path,
                       path_size,
                       "%s%s/%s",
                       CONFIG_LABGUARD_AUDIO_PROMPT_SD_MOUNT_POINT,
                       CONFIG_LABGUARD_AUDIO_PROMPT_DIR,
                       "0001_test_loud.wav");
    return len > 0 && (size_t)len < path_size;
}

static bool prompt_dir_path(char *path, size_t path_size)
{
    if (path == NULL || path_size == 0) {
        return false;
    }
    int len = snprintf(path,
                       path_size,
                       "%s%s",
                       CONFIG_LABGUARD_AUDIO_PROMPT_SD_MOUNT_POINT,
                       CONFIG_LABGUARD_AUDIO_PROMPT_DIR);
    return len > 0 && (size_t)len < path_size;
}

static bool file_name_has_wav_extension(const char *name)
{
    if (name == NULL) {
        return false;
    }
    size_t len = strlen(name);
    if (len < 4) {
        return false;
    }

    const char *ext = name + len - 4;
    return (char)tolower((unsigned char)ext[0]) == '.' &&
           (char)tolower((unsigned char)ext[1]) == 'w' &&
           (char)tolower((unsigned char)ext[2]) == 'a' &&
           (char)tolower((unsigned char)ext[3]) == 'v';
}

static int compare_file_paths(const void *a, const void *b)
{
    return strcmp((const char *)a, (const char *)b);
}

static int scan_audio_files(char paths[][AUDIO_PROMPT_PATH_BYTES], size_t max_files)
{
    if (paths == NULL || max_files == 0) {
        return -1;
    }

    char dir_path[AUDIO_PROMPT_PATH_BYTES];
    if (!prompt_dir_path(dir_path, sizeof(dir_path))) {
        return -1;
    }

    DIR *dir = opendir(dir_path);
    if (dir == NULL) {
        return -1;
    }

    size_t count = 0;
    struct dirent *entry = NULL;
    while ((entry = readdir(dir)) != NULL && count < max_files) {
        if (entry->d_name[0] == '.' || !file_name_has_wav_extension(entry->d_name)) {
            continue;
        }
        int len = snprintf(paths[count],
                           AUDIO_PROMPT_PATH_BYTES,
                           "%s/%s",
                           dir_path,
                           entry->d_name);
        if (len > 0 && len < AUDIO_PROMPT_PATH_BYTES) {
            ++count;
        }
    }
    closedir(dir);

    qsort(paths, count, AUDIO_PROMPT_PATH_BYTES, compare_file_paths);
    return (int)count;
}

static bool wav_chunk_id_equals(const char actual[4], const char expected[4])
{
    return memcmp(actual, expected, 4) == 0;
}

static esp_err_t wav_skip_bytes(FILE *file, uint32_t bytes)
{
    if (bytes == 0) {
        return ESP_OK;
    }
    if (fseek(file, (long)bytes, SEEK_CUR) != 0) {
        return ESP_FAIL;
    }
    return ESP_OK;
}

static esp_err_t wav_read_header(FILE *file, wav_info_t *out)
{
    if (file == NULL || out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(out, 0, sizeof(*out));

    wav_riff_header_t riff = {0};
    if (fread(&riff, sizeof(riff), 1, file) != 1) {
        return ESP_FAIL;
    }
    if (!wav_chunk_id_equals(riff.riff, "RIFF") || !wav_chunk_id_equals(riff.wave, "WAVE")) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    bool found_fmt = false;
    bool found_data = false;

    while (!found_data) {
        wav_chunk_header_t chunk = {0};
        if (fread(&chunk, sizeof(chunk), 1, file) != 1) {
            return ESP_FAIL;
        }

        if (wav_chunk_id_equals(chunk.id, "fmt ")) {
            wav_fmt_data_t fmt = {0};
            if (chunk.size < sizeof(fmt)) {
                return ESP_ERR_INVALID_SIZE;
            }
            if (fread(&fmt, sizeof(fmt), 1, file) != 1) {
                return ESP_FAIL;
            }
            uint32_t remaining = chunk.size - sizeof(fmt);
            if (wav_skip_bytes(file, remaining) != ESP_OK) {
                return ESP_FAIL;
            }
            if (chunk.size & 1U) {
                if (wav_skip_bytes(file, 1) != ESP_OK) {
                    return ESP_FAIL;
                }
            }

            out->audio_format = fmt.audio_format;
            out->num_channels = fmt.num_channels;
            out->sample_rate = fmt.sample_rate;
            out->bits_per_sample = fmt.bits_per_sample;
            out->block_align = fmt.block_align;
            found_fmt = true;
            continue;
        }

        if (wav_chunk_id_equals(chunk.id, "data")) {
            out->data_size = chunk.size;
            found_data = true;
            break;
        }

        if (wav_skip_bytes(file, chunk.size) != ESP_OK) {
            return ESP_FAIL;
        }
        if (chunk.size & 1U) {
            if (wav_skip_bytes(file, 1) != ESP_OK) {
                return ESP_FAIL;
            }
        }
    }

    if (!found_fmt || !found_data) {
        return ESP_ERR_NOT_FOUND;
    }
    if (out->audio_format != 1) {
        ESP_LOGW(TAG, "unsupported wav format=%u, need PCM", out->audio_format);
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (out->bits_per_sample != AUDIO_PROMPT_PCM_BITS_PER_SAMPLE) {
        ESP_LOGW(TAG, "unsupported wav bits=%u, need %d", out->bits_per_sample, AUDIO_PROMPT_PCM_BITS_PER_SAMPLE);
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (out->sample_rate < AUDIO_PROMPT_MIN_SAMPLE_RATE_HZ ||
        out->sample_rate > AUDIO_PROMPT_MAX_SAMPLE_RATE_HZ) {
        ESP_LOGW(TAG, "unsupported wav sample_rate=%lu, need %d..%d",
                 (unsigned long)out->sample_rate,
                 AUDIO_PROMPT_MIN_SAMPLE_RATE_HZ,
                 AUDIO_PROMPT_MAX_SAMPLE_RATE_HZ);
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (!(out->num_channels == 1 || out->num_channels == 2)) {
        ESP_LOGW(TAG, "unsupported wav channels=%u, need mono or stereo", out->num_channels);
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (out->block_align == 0) {
        ESP_LOGW(TAG, "invalid wav block_align=0");
        return ESP_ERR_INVALID_SIZE;
    }

    return ESP_OK;
}

static esp_err_t audio_prompt_init_i2s(void)
{
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
    esp_err_t ret = i2s_new_channel(&chan_cfg, &s_tx_chan, NULL);
    if (ret != ESP_OK) {
        return ret;
    }

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(AUDIO_PROMPT_DEFAULT_SAMPLE_RATE_HZ),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = CONFIG_LABGUARD_AUDIO_PROMPT_I2S_BCLK_GPIO,
            .ws = CONFIG_LABGUARD_AUDIO_PROMPT_I2S_WS_GPIO,
            .dout = CONFIG_LABGUARD_AUDIO_PROMPT_I2S_DOUT_GPIO,
            .din = I2S_GPIO_UNUSED,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };
    ret = i2s_channel_init_std_mode(s_tx_chan, &std_cfg);
    if (ret != ESP_OK) {
        i2s_del_channel(s_tx_chan);
        s_tx_chan = NULL;
        return ret;
    }

    s_i2s_ready = true;
    s_i2s_enabled = false;
    s_i2s_sample_rate_hz = AUDIO_PROMPT_DEFAULT_SAMPLE_RATE_HZ;
    ESP_LOGI(TAG,
             "audio I2S initialized format=philips stereo DIN=GPIO%d BCLK=GPIO%d WS=GPIO%d",
             CONFIG_LABGUARD_AUDIO_PROMPT_I2S_DOUT_GPIO,
             CONFIG_LABGUARD_AUDIO_PROMPT_I2S_BCLK_GPIO,
             CONFIG_LABGUARD_AUDIO_PROMPT_I2S_WS_GPIO);
    return ESP_OK;
}

static esp_err_t audio_prompt_enable_i2s(void)
{
    if (!s_i2s_ready || s_tx_chan == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_i2s_enabled) {
        return ESP_OK;
    }

    esp_err_t ret = i2s_channel_enable(s_tx_chan);
    if (ret == ESP_OK) {
        s_i2s_enabled = true;
    }
    return ret;
}

static esp_err_t audio_prompt_disable_i2s(void)
{
    if (!s_i2s_ready || s_tx_chan == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!s_i2s_enabled) {
        return ESP_OK;
    }

    esp_err_t ret = i2s_channel_disable(s_tx_chan);
    if (ret == ESP_OK) {
        s_i2s_enabled = false;
    }
    return ret;
}

static esp_err_t audio_prompt_set_sample_rate(uint32_t sample_rate_hz)
{
    if (!s_i2s_ready || s_tx_chan == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (sample_rate_hz == s_i2s_sample_rate_hz) {
        return ESP_OK;
    }

    bool was_enabled = s_i2s_enabled;
    esp_err_t ret = audio_prompt_disable_i2s();
    if (ret != ESP_OK) {
        return ret;
    }

    i2s_std_clk_config_t clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(sample_rate_hz);
    ret = i2s_channel_reconfig_std_clock(s_tx_chan, &clk_cfg);
    if (ret != ESP_OK) {
        if (was_enabled) {
            esp_err_t enable_ret = audio_prompt_enable_i2s();
            if (enable_ret != ESP_OK) {
                ESP_LOGW(TAG, "I2S re-enable also failed after clock error: %s", esp_err_to_name(enable_ret));
            }
        }
        return ret;
    }

    s_i2s_sample_rate_hz = sample_rate_hz;
    if (was_enabled) {
        ret = audio_prompt_enable_i2s();
        if (ret != ESP_OK) {
            return ret;
        }
    }
    ESP_LOGI(TAG, "audio I2S sample rate set to %lu Hz", (unsigned long)sample_rate_hz);
    return ESP_OK;
}

static esp_err_t audio_prompt_write_silence_ms(uint32_t duration_ms)
{
    if (!s_i2s_ready || s_tx_chan == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!s_i2s_enabled) {
        return ESP_OK;
    }

    const uint32_t bytes_per_ms = (s_i2s_sample_rate_hz * AUDIO_PROMPT_STEREO_FRAME_BYTES) / 1000;
    uint32_t remaining = bytes_per_ms * duration_ms;
    uint8_t silence[AUDIO_PROMPT_IO_BUFFER_BYTES] = {0};
    while (remaining > 0) {
        size_t chunk = remaining > sizeof(silence) ? sizeof(silence) : remaining;
        size_t bytes_written = 0;
        esp_err_t ret = i2s_channel_write(s_tx_chan, silence, chunk, &bytes_written, 200);
        if (ret != ESP_OK) {
            return ret;
        }
        if (bytes_written == 0) {
            return ESP_ERR_TIMEOUT;
        }
        remaining -= (uint32_t)bytes_written;
    }

    return ESP_OK;
}

static esp_err_t audio_prompt_quiesce_i2s(void)
{
    if (!s_i2s_ready || s_tx_chan == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t ret = audio_prompt_write_silence_ms(AUDIO_PROMPT_QUIESCE_MS);
    if (ret != ESP_OK) {
        return ret;
    }

    return audio_prompt_disable_i2s();
}

static esp_err_t audio_prompt_write_pcm(FILE *file, const wav_info_t *wav, bool loop_controlled)
{
    if (file == NULL || wav == NULL || !s_i2s_ready || s_tx_chan == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t read_buf[AUDIO_PROMPT_IO_BUFFER_BYTES];
    uint8_t stereo_buf[AUDIO_PROMPT_IO_BUFFER_BYTES * 2];
    uint32_t remaining = wav->data_size;

    while (remaining > 0) {
        if (loop_controlled && !s_loop_enabled) {
            return ESP_OK;
        }

        size_t chunk = remaining > sizeof(read_buf) ? sizeof(read_buf) : remaining;
        size_t read_bytes = fread(read_buf, 1, chunk, file);
        if (read_bytes == 0) {
            return ferror(file) ? ESP_FAIL : ESP_OK;
        }
        remaining -= (uint32_t)read_bytes;

        const void *write_ptr = read_buf;
        size_t write_bytes = read_bytes;

        if (wav->num_channels == 1) {
            size_t sample_count = read_bytes / AUDIO_PROMPT_SAMPLE_BYTES;
            int16_t *src = (int16_t *)read_buf;
            int16_t *dst = (int16_t *)stereo_buf;
            for (size_t i = 0; i < sample_count; ++i) {
                dst[i * 2] = src[i];
                dst[i * 2 + 1] = src[i];
            }
            write_ptr = stereo_buf;
            write_bytes = sample_count * AUDIO_PROMPT_STEREO_FRAME_BYTES;
        }

        size_t total_written = 0;
        while (total_written < write_bytes) {
            if (loop_controlled && !s_loop_enabled) {
                return ESP_OK;
            }

            size_t bytes_written = 0;
            esp_err_t ret = i2s_channel_write(s_tx_chan,
                                              ((const uint8_t *)write_ptr) + total_written,
                                              write_bytes - total_written,
                                              &bytes_written,
                                              1000);
            if (ret != ESP_OK) {
                return ret;
            }
            total_written += bytes_written;
        }
    }

    return ESP_OK;
}

static esp_err_t audio_prompt_play_path(const char *path, bool loop_controlled)
{
    if (path == NULL || path[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_i2s_ready || s_tx_chan == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        ESP_LOGW(TAG, "open wav failed path=%s", path);
        return ESP_ERR_NOT_FOUND;
    }

    if (s_play_mutex != NULL) {
        xSemaphoreTake(s_play_mutex, portMAX_DELAY);
    }

    wav_info_t wav = {0};
    esp_err_t ret = wav_read_header(file, &wav);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "invalid wav path=%s: %s", path, esp_err_to_name(ret));
        fclose(file);
        if (!loop_controlled || !s_loop_enabled) {
            audio_prompt_set_amp_enabled(false);
        }
        if (s_play_mutex != NULL) {
            xSemaphoreGive(s_play_mutex);
        }
        return ret;
    }

    ret = audio_prompt_set_sample_rate(wav.sample_rate);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG,
                 "set wav sample rate failed path=%s rate=%lu: %s",
                 path,
                 (unsigned long)wav.sample_rate,
                 esp_err_to_name(ret));
        fclose(file);
        if (!loop_controlled || !s_loop_enabled) {
            audio_prompt_set_amp_enabled(false);
        }
        if (s_play_mutex != NULL) {
            xSemaphoreGive(s_play_mutex);
        }
        return ret;
    }

    ret = audio_prompt_enable_i2s();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "audio I2S enable failed path=%s: %s", path, esp_err_to_name(ret));
        fclose(file);
        if (!loop_controlled || !s_loop_enabled) {
            audio_prompt_set_amp_enabled(false);
        }
        if (s_play_mutex != NULL) {
            xSemaphoreGive(s_play_mutex);
        }
        return ret;
    }

    audio_prompt_set_amp_enabled(true);
    vTaskDelay(pdMS_TO_TICKS(AUDIO_PROMPT_AMP_SETTLE_MS));

    ESP_LOGI(TAG,
             "playing wav path=%s channels=%u rate=%lu data=%lu",
             path,
             (unsigned int)wav.num_channels,
             (unsigned long)wav.sample_rate,
             (unsigned long)wav.data_size);

    ret = audio_prompt_write_pcm(file, &wav, loop_controlled);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "wav playback failed path=%s: %s", path, esp_err_to_name(ret));
    }
    fclose(file);
    if (!loop_controlled || !s_loop_enabled) {
        esp_err_t quiet_ret = audio_prompt_quiesce_i2s();
        if (quiet_ret != ESP_OK) {
            ESP_LOGW(TAG, "audio I2S quiesce failed after playback: %s", esp_err_to_name(quiet_ret));
        }
        audio_prompt_set_amp_enabled(false);
    }
    if (s_play_mutex != NULL) {
        xSemaphoreGive(s_play_mutex);
    }
    return ret;
}

static esp_err_t audio_prompt_play_file(audio_prompt_t prompt)
{
    char path[AUDIO_PROMPT_PATH_BYTES];
    if (!prompt_file_path(prompt, path, sizeof(path))) {
        return ESP_ERR_INVALID_SIZE;
    }
    return audio_prompt_play_path(path, false);
}

static void audio_prompt_loop_task(void *arg)
{
    (void)arg;

    char path[AUDIO_PROMPT_PATH_BYTES];
    if (!prompt_file_path(s_last_prompt, path, sizeof(path))) {
        ESP_LOGW(TAG, "audio loop prompt path invalid");
        s_loop_enabled = false;
        s_loop_task = NULL;
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "audio loop started file=%s", path);

    while (s_loop_enabled) {
        esp_err_t ret = audio_prompt_play_path(path, true);
        if (ret != ESP_OK && s_loop_enabled) {
            ESP_LOGW(TAG, "audio loop playback failed path=%s: %s", path, esp_err_to_name(ret));
            vTaskDelay(pdMS_TO_TICKS(AUDIO_PROMPT_LOOP_IDLE_MS));
            continue;
        }
        vTaskDelay(pdMS_TO_TICKS(AUDIO_PROMPT_LOOP_BETWEEN_FILES_MS));
    }

    esp_err_t quiet_ret = audio_prompt_quiesce_i2s();
    if (quiet_ret != ESP_OK) {
        ESP_LOGW(TAG, "audio I2S quiesce failed after loop stop: %s", esp_err_to_name(quiet_ret));
    }
    audio_prompt_set_amp_enabled(false);
    ESP_LOGI(TAG, "audio loop stopped");
    s_loop_task = NULL;
    vTaskDelete(NULL);
}

const char *audio_prompt_to_string(audio_prompt_t prompt)
{
    (void)prompt;
    return "toxic_gas";
}

esp_err_t audio_prompt_init(void)
{
    s_last_prompt = AUDIO_PROMPT_TOXIC_GAS;
    s_loop_enabled = false;
    s_loop_task = NULL;
    esp_err_t amp_ret = audio_prompt_init_amp();
    if (amp_ret != ESP_OK) {
        ESP_LOGW(TAG, "audio amplifier control init failed: %s", esp_err_to_name(amp_ret));
        return amp_ret;
    }
    if (s_play_mutex == NULL) {
        s_play_mutex = xSemaphoreCreateMutex();
        if (s_play_mutex == NULL) {
            ESP_LOGW(TAG, "audio prompt mutex allocation failed");
            return ESP_ERR_NO_MEM;
        }
    }

    esp_err_t ret = audio_prompt_init_i2s();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "audio prompt init failed: %s", esp_err_to_name(ret));
        return ret;
    }
    return ESP_OK;
}

esp_err_t audio_prompt_play(audio_prompt_t prompt)
{
    s_last_prompt = prompt;
    return audio_prompt_play_file(prompt);
}

esp_err_t audio_prompt_start_loop(void)
{
    if (!s_i2s_ready || s_tx_chan == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_loop_task != NULL) {
        s_loop_enabled = true;
        return ESP_OK;
    }

    char path[AUDIO_PROMPT_PATH_BYTES];
    if (!prompt_file_path(s_last_prompt, path, sizeof(path))) {
        ESP_LOGW(TAG, "audio loop start failed, prompt path invalid");
        return ESP_ERR_INVALID_SIZE;
    }

    FILE *probe = fopen(path, "rb");
    if (probe == NULL) {
        ESP_LOGW(TAG, "audio loop start failed, wav missing: %s", path);
        return ESP_ERR_NOT_FOUND;
    }
    fclose(probe);

    s_loop_enabled = true;
    BaseType_t task_ret = xTaskCreate(audio_prompt_loop_task,
                                      "audio_loop",
                                      AUDIO_PROMPT_LOOP_TASK_STACK,
                                      NULL,
                                      4,
                                      &s_loop_task);
    if (task_ret != pdPASS) {
        s_loop_enabled = false;
        s_loop_task = NULL;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t audio_prompt_stop_loop(void)
{
    s_loop_enabled = false;
    audio_prompt_set_amp_enabled(false);
    if (s_play_mutex != NULL && xSemaphoreTake(s_play_mutex, pdMS_TO_TICKS(200)) == pdTRUE) {
        esp_err_t ret = audio_prompt_quiesce_i2s();
        xSemaphoreGive(s_play_mutex);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "audio I2S quiesce failed on stop: %s", esp_err_to_name(ret));
            return ret;
        }
    }
    return ESP_OK;
}

bool audio_prompt_is_looping(void)
{
    return s_loop_enabled;
}

audio_prompt_t audio_prompt_get_last(void)
{
    return s_last_prompt;
}
