#include "audio_prompt.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "driver/i2s_std.h"
#include "esp_log.h"
#include "sdkconfig.h"

static const char *TAG = "audio_prompt";
static audio_prompt_t s_last_prompt = AUDIO_PROMPT_ACCESS_DENIED;

#if CONFIG_LABGUARD_AUDIO_PROMPT_I2S_WAV
#define AUDIO_PROMPT_PCM_SAMPLE_RATE_HZ 16000
#define AUDIO_PROMPT_PCM_BITS_PER_SAMPLE 16
#define AUDIO_PROMPT_FRAME_BYTES 4
#define AUDIO_PROMPT_IO_BUFFER_BYTES 1024
#define AUDIO_PROMPT_PATH_BYTES 160

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
static bool s_i2s_enabled;
static bool s_i2s_ready;

static const char *prompt_file_name(audio_prompt_t prompt)
{
    switch (prompt) {
    case AUDIO_PROMPT_ACCESS_GRANTED:
        return "0001.wav";
    case AUDIO_PROMPT_MISSING_GOGGLES:
        return "0002.wav";
    case AUDIO_PROMPT_MISSING_LABCOAT:
        return "0003.wav";
    case AUDIO_PROMPT_INDOOR_DANGER:
        return "0004.wav";
    case AUDIO_PROMPT_ACCESS_DENIED:
    default:
        return "0005.wav";
    }
}

static bool prompt_file_path(audio_prompt_t prompt, char *path, size_t path_size)
{
    if (path == NULL || path_size == 0) {
        return false;
    }
    int len = snprintf(path,
                       path_size,
                       "%s%s/%s",
                       CONFIG_LABGUARD_AUDIO_PROMPT_SD_MOUNT_POINT,
                       CONFIG_LABGUARD_AUDIO_PROMPT_DIR,
                       prompt_file_name(prompt));
    return len > 0 && (size_t)len < path_size;
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
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (out->bits_per_sample != AUDIO_PROMPT_PCM_BITS_PER_SAMPLE) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (out->sample_rate != AUDIO_PROMPT_PCM_SAMPLE_RATE_HZ) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (!(out->num_channels == 1 || out->num_channels == 2)) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (out->block_align == 0) {
        return ESP_ERR_INVALID_SIZE;
    }

    return ESP_OK;
}

static esp_err_t audio_prompt_init_i2s(void)
{
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
    esp_err_t ret = i2s_new_channel(&chan_cfg, &s_tx_chan, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "i2s_new_channel failed: %s", esp_err_to_name(ret));
        return ret;
    }

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(AUDIO_PROMPT_PCM_SAMPLE_RATE_HZ),
        .slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
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
        ESP_LOGE(TAG, "i2s_channel_init_std_mode failed: %s", esp_err_to_name(ret));
        i2s_del_channel(s_tx_chan);
        s_tx_chan = NULL;
        return ret;
    }

    ret = i2s_channel_enable(s_tx_chan);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "i2s_channel_enable failed: %s", esp_err_to_name(ret));
        i2s_del_channel(s_tx_chan);
        s_tx_chan = NULL;
        return ret;
    }

    s_i2s_enabled = true;
    s_i2s_ready = true;
    ESP_LOGI(TAG,
             "audio prompt I2S ready dout=GPIO%d bclk=GPIO%d ws=GPIO%d sample_rate=%d",
             CONFIG_LABGUARD_AUDIO_PROMPT_I2S_DOUT_GPIO,
             CONFIG_LABGUARD_AUDIO_PROMPT_I2S_BCLK_GPIO,
             CONFIG_LABGUARD_AUDIO_PROMPT_I2S_WS_GPIO,
             AUDIO_PROMPT_PCM_SAMPLE_RATE_HZ);
    return ESP_OK;
}

static esp_err_t audio_prompt_write_pcm(FILE *file, const wav_info_t *wav)
{
    if (file == NULL || wav == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_i2s_ready || s_tx_chan == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t read_buf[AUDIO_PROMPT_IO_BUFFER_BYTES];
    uint8_t stereo_buf[AUDIO_PROMPT_IO_BUFFER_BYTES * 2];
    uint32_t remaining = wav->data_size;

    while (remaining > 0) {
        size_t chunk = remaining > sizeof(read_buf) ? sizeof(read_buf) : remaining;
        size_t read_bytes = fread(read_buf, 1, chunk, file);
        if (read_bytes == 0) {
            return ferror(file) ? ESP_FAIL : ESP_OK;
        }
        remaining -= (uint32_t)read_bytes;

        const void *write_ptr = read_buf;
        size_t write_bytes = read_bytes;

        if (wav->num_channels == 1) {
            size_t sample_count = read_bytes / 2;
            int16_t *src = (int16_t *)read_buf;
            int16_t *dst = (int16_t *)stereo_buf;
            for (size_t i = 0; i < sample_count; ++i) {
                dst[i * 2] = src[i];
                dst[i * 2 + 1] = src[i];
            }
            write_ptr = stereo_buf;
            write_bytes = sample_count * AUDIO_PROMPT_FRAME_BYTES;
        }

        size_t total_written = 0;
        while (total_written < write_bytes) {
            size_t bytes_written = 0;
            esp_err_t ret = i2s_channel_write(s_tx_chan,
                                              ((const uint8_t *)write_ptr) + total_written,
                                              write_bytes - total_written,
                                              &bytes_written,
                                              1000);
            if (ret != ESP_OK) {
                ESP_LOGW(TAG, "i2s write failed: %s", esp_err_to_name(ret));
                return ret;
            }
            total_written += bytes_written;
        }
    }

    return ESP_OK;
}

static esp_err_t audio_prompt_play_file(audio_prompt_t prompt)
{
    char path[AUDIO_PROMPT_PATH_BYTES];
    if (!prompt_file_path(prompt, path, sizeof(path))) {
        ESP_LOGW(TAG, "audio prompt path too long for prompt=%s", audio_prompt_to_string(prompt));
        return ESP_ERR_INVALID_SIZE;
    }

    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        ESP_LOGW(TAG, "audio prompt file missing: %s", path);
        return ESP_ERR_NOT_FOUND;
    }

    wav_info_t wav = {0};
    esp_err_t ret = wav_read_header(file, &wav);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG,
                 "invalid wav file %s: format=%u channels=%u rate=%u bits=%u ret=%s",
                 path,
                 wav.audio_format,
                 wav.num_channels,
                 (unsigned int)wav.sample_rate,
                 wav.bits_per_sample,
                 esp_err_to_name(ret));
        fclose(file);
        return ret;
    }

    ESP_LOGI(TAG,
             "play prompt=%s file=%s channels=%u sample_rate=%u bits=%u bytes=%u",
             audio_prompt_to_string(prompt),
             path,
             wav.num_channels,
             (unsigned int)wav.sample_rate,
             wav.bits_per_sample,
             (unsigned int)wav.data_size);
    ret = audio_prompt_write_pcm(file, &wav);
    fclose(file);
    return ret;
}
#endif

const char *audio_prompt_to_string(audio_prompt_t prompt)
{
    switch (prompt) {
    case AUDIO_PROMPT_ACCESS_GRANTED:
        return "access_granted";
    case AUDIO_PROMPT_MISSING_GOGGLES:
        return "missing_goggles";
    case AUDIO_PROMPT_MISSING_LABCOAT:
        return "missing_labcoat";
    case AUDIO_PROMPT_INDOOR_DANGER:
        return "indoor_danger";
    case AUDIO_PROMPT_ACCESS_DENIED:
    default:
        return "access_denied";
    }
}

esp_err_t audio_prompt_init(void)
{
    s_last_prompt = AUDIO_PROMPT_ACCESS_DENIED;
#if CONFIG_LABGUARD_AUDIO_PROMPT_I2S_WAV
    esp_err_t ret = audio_prompt_init_i2s();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "audio prompt init degraded to log-only mode: %s", esp_err_to_name(ret));
        return ret;
    }
#endif
    ESP_LOGI(TAG, "audio prompt initialized");
    return ESP_OK;
}

esp_err_t audio_prompt_play(audio_prompt_t prompt)
{
    s_last_prompt = prompt;
#if CONFIG_LABGUARD_AUDIO_PROMPT_I2S_WAV
    esp_err_t ret = audio_prompt_play_file(prompt);
    if (ret == ESP_OK) {
        return ESP_OK;
    }
    ESP_LOGW(TAG, "audio prompt playback failed, prompt=%s err=%s", audio_prompt_to_string(prompt), esp_err_to_name(ret));
    return ret;
#else
    ESP_LOGI(TAG, "play prompt=%s", audio_prompt_to_string(prompt));
    return ESP_OK;
#endif
}

audio_prompt_t audio_prompt_get_last(void)
{
    return s_last_prompt;
}
