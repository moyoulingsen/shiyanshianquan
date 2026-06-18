#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    AUDIO_PROMPT_TOXIC_GAS = 0,
    AUDIO_PROMPT_FIRE,
    AUDIO_PROMPT_MANUAL_ALARM,
} audio_prompt_t;

esp_err_t audio_prompt_init(void);
esp_err_t audio_prompt_play(audio_prompt_t prompt);
const char *audio_prompt_to_string(audio_prompt_t prompt);
audio_prompt_t audio_prompt_get_last(void);

#ifdef __cplusplus
}
#endif
