#include "audio_prompt.h"

#include "esp_log.h"

static const char *TAG = "audio_prompt";
static audio_prompt_t s_last_prompt = AUDIO_PROMPT_ACCESS_DENIED;

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
    ESP_LOGI(TAG, "audio prompt initialized");
    return ESP_OK;
}

esp_err_t audio_prompt_play(audio_prompt_t prompt)
{
    s_last_prompt = prompt;
    ESP_LOGI(TAG, "play prompt=%s", audio_prompt_to_string(prompt));
    return ESP_OK;
}

audio_prompt_t audio_prompt_get_last(void)
{
    return s_last_prompt;
}
