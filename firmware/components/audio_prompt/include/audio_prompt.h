#pragma once

#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    AUDIO_PROMPT_TOXIC_GAS = 0,
} audio_prompt_t;

esp_err_t audio_prompt_init(void);
esp_err_t audio_prompt_play(audio_prompt_t prompt);
esp_err_t audio_prompt_start_loop(void);
esp_err_t audio_prompt_stop_loop(void);
bool audio_prompt_is_looping(void);
const char *audio_prompt_to_string(audio_prompt_t prompt);
audio_prompt_t audio_prompt_get_last(void);

#ifdef __cplusplus
}
#endif
