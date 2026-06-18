#include "actuator_ctrl.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/rmt_tx.h"
#include "esp_log.h"
#include "sdkconfig.h"

#ifdef CONFIG_LABGUARD_ACTUATOR_FAN_ACTIVE_LOW
#define FAN_ACTIVE_LOW 1
#else
#define FAN_ACTIVE_LOW 0
#endif

#ifdef CONFIG_LABGUARD_ACTUATOR_PUMP_ACTIVE_LOW
#define PUMP_ACTIVE_LOW 1
#else
#define PUMP_ACTIVE_LOW 0
#endif

#ifdef CONFIG_LABGUARD_ACTUATOR_ALARM_ACTIVE_LOW
#define ALARM_ACTIVE_LOW 1
#else
#define ALARM_ACTIVE_LOW 0
#endif

#define ACTUATOR_LEDC_MODE LEDC_LOW_SPEED_MODE
#define ACTUATOR_LEDC_TIMER LEDC_TIMER_0
#define ACTUATOR_LEDC_FAN_CHANNEL LEDC_CHANNEL_0
#define ACTUATOR_LEDC_PUMP_CHANNEL LEDC_CHANNEL_1
#define ACTUATOR_LEDC_DUTY_RES LEDC_TIMER_10_BIT
#define ACTUATOR_LEDC_DUTY_MAX ((1 << 10) - 1)
#define ACTUATOR_LEDC_FREQ_HZ 20000
#define ALARM_STRIP_RESOLUTION_HZ 10000000
#define ALARM_STRIP_TASK_STACK_BYTES 3072
#define ACTUATOR_CONTAINER_OF(ptr, type, member) ((type *)((char *)(ptr) - offsetof(type, member)))

static const char *TAG = "actuator_ctrl";
static labguard_risk_state_t s_last_risk;
static bool s_ledc_timer_configured;
static rmt_channel_handle_t s_alarm_strip_chan;
static rmt_encoder_handle_t s_alarm_strip_encoder;
static uint8_t s_alarm_strip_pixels[CONFIG_LABGUARD_ACTUATOR_ALARM_STRIP_LED_COUNT * 3];
static TaskHandle_t s_alarm_strip_task;
static bool s_alarm_strip_ready;
static volatile bool s_alarm_strip_on;

typedef struct {
    uint32_t resolution_hz;
} alarm_strip_encoder_config_t;

typedef struct {
    rmt_encoder_t base;
    rmt_encoder_t *bytes_encoder;
    rmt_encoder_t *copy_encoder;
    int state;
    rmt_symbol_word_t reset_code;
} alarm_strip_encoder_t;

static int clamp_level_pct(int level_pct)
{
    if (level_pct < 0) {
        return 0;
    }
    if (level_pct > 100) {
        return 100;
    }
    return level_pct;
}

static bool should_use_pwm(int gpio_num)
{
    return gpio_num >= 0;
}

static bool should_use_gpio_output(int gpio_num)
{
    return gpio_num >= 0;
}

static uint32_t level_pct_to_duty(int level_pct, bool active_low)
{
    int clamped = clamp_level_pct(level_pct);
    uint32_t duty = (uint32_t)((clamped * ACTUATOR_LEDC_DUTY_MAX) / 100);
    if (active_low) {
        duty = ACTUATOR_LEDC_DUTY_MAX - duty;
    }
    return duty;
}

static int alarm_gpio_level(bool on)
{
    bool level = on;
    if (ALARM_ACTIVE_LOW) {
        level = !level;
    }
    return level ? 1 : 0;
}

RMT_ENCODER_FUNC_ATTR
static size_t alarm_strip_encode(rmt_encoder_t *encoder,
                                 rmt_channel_handle_t channel,
                                 const void *primary_data,
                                 size_t data_size,
                                 rmt_encode_state_t *ret_state)
{
    alarm_strip_encoder_t *strip_encoder = ACTUATOR_CONTAINER_OF(encoder, alarm_strip_encoder_t, base);
    rmt_encoder_handle_t bytes_encoder = strip_encoder->bytes_encoder;
    rmt_encoder_handle_t copy_encoder = strip_encoder->copy_encoder;
    rmt_encode_state_t session_state = RMT_ENCODING_RESET;
    rmt_encode_state_t state = RMT_ENCODING_RESET;
    size_t encoded_symbols = 0;

    switch (strip_encoder->state) {
    case 0:
        encoded_symbols += bytes_encoder->encode(bytes_encoder, channel, primary_data, data_size, &session_state);
        if (session_state & RMT_ENCODING_COMPLETE) {
            strip_encoder->state = 1;
        }
        if (session_state & RMT_ENCODING_MEM_FULL) {
            state |= RMT_ENCODING_MEM_FULL;
            goto out;
        }
        __attribute__((fallthrough));
    case 1:
        encoded_symbols += copy_encoder->encode(copy_encoder,
                                                channel,
                                                &strip_encoder->reset_code,
                                                sizeof(strip_encoder->reset_code),
                                                &session_state);
        if (session_state & RMT_ENCODING_COMPLETE) {
            strip_encoder->state = RMT_ENCODING_RESET;
            state |= RMT_ENCODING_COMPLETE;
        }
        if (session_state & RMT_ENCODING_MEM_FULL) {
            state |= RMT_ENCODING_MEM_FULL;
            goto out;
        }
    }

out:
    *ret_state = state;
    return encoded_symbols;
}

static esp_err_t alarm_strip_encoder_del(rmt_encoder_t *encoder)
{
    alarm_strip_encoder_t *strip_encoder = ACTUATOR_CONTAINER_OF(encoder, alarm_strip_encoder_t, base);
    if (strip_encoder->bytes_encoder) {
        rmt_del_encoder(strip_encoder->bytes_encoder);
    }
    if (strip_encoder->copy_encoder) {
        rmt_del_encoder(strip_encoder->copy_encoder);
    }
    free(strip_encoder);
    return ESP_OK;
}

RMT_ENCODER_FUNC_ATTR
static esp_err_t alarm_strip_encoder_reset(rmt_encoder_t *encoder)
{
    alarm_strip_encoder_t *strip_encoder = ACTUATOR_CONTAINER_OF(encoder, alarm_strip_encoder_t, base);
    rmt_encoder_reset(strip_encoder->bytes_encoder);
    rmt_encoder_reset(strip_encoder->copy_encoder);
    strip_encoder->state = RMT_ENCODING_RESET;
    return ESP_OK;
}

static esp_err_t alarm_strip_new_encoder(const alarm_strip_encoder_config_t *config,
                                         rmt_encoder_handle_t *ret_encoder)
{
    esp_err_t ret = ESP_OK;
    alarm_strip_encoder_t *strip_encoder = NULL;
    if (config == NULL || ret_encoder == NULL || config->resolution_hz == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    strip_encoder = rmt_alloc_encoder_mem(sizeof(alarm_strip_encoder_t));
    if (strip_encoder == NULL) {
        return ESP_ERR_NO_MEM;
    }

    strip_encoder->base.encode = alarm_strip_encode;
    strip_encoder->base.del = alarm_strip_encoder_del;
    strip_encoder->base.reset = alarm_strip_encoder_reset;

    rmt_bytes_encoder_config_t bytes_encoder_config = {
        .bit0 = {
            .level0 = 1,
            .duration0 = (uint32_t)(0.3 * config->resolution_hz / 1000000),
            .level1 = 0,
            .duration1 = (uint32_t)(0.9 * config->resolution_hz / 1000000),
        },
        .bit1 = {
            .level0 = 1,
            .duration0 = (uint32_t)(0.9 * config->resolution_hz / 1000000),
            .level1 = 0,
            .duration1 = (uint32_t)(0.3 * config->resolution_hz / 1000000),
        },
        .flags.msb_first = 1,
    };
    ret = rmt_new_bytes_encoder(&bytes_encoder_config, &strip_encoder->bytes_encoder);
    if (ret != ESP_OK) {
        goto fail;
    }

    rmt_copy_encoder_config_t copy_encoder_config = {};
    ret = rmt_new_copy_encoder(&copy_encoder_config, &strip_encoder->copy_encoder);
    if (ret != ESP_OK) {
        goto fail;
    }

    uint32_t reset_ticks = config->resolution_hz / 1000000 * 50 / 2;
    strip_encoder->reset_code = (rmt_symbol_word_t) {
        .level0 = 0,
        .duration0 = reset_ticks,
        .level1 = 0,
        .duration1 = reset_ticks,
    };
    *ret_encoder = &strip_encoder->base;
    return ESP_OK;

fail:
    if (strip_encoder->bytes_encoder) {
        rmt_del_encoder(strip_encoder->bytes_encoder);
    }
    if (strip_encoder->copy_encoder) {
        rmt_del_encoder(strip_encoder->copy_encoder);
    }
    free(strip_encoder);
    return ret;
}

static void configure_output(int gpio_num)
{
    if (gpio_num < 0) {
        return;
    }
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << gpio_num,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t ret = gpio_config(&cfg);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "GPIO%d output config failed: %s", gpio_num, esp_err_to_name(ret));
    }
}

static void set_alarm_gpio(bool on)
{
    if (!should_use_gpio_output(CONFIG_LABGUARD_ACTUATOR_ALARM_GPIO)) {
        return;
    }

    esp_err_t ret = gpio_set_level(CONFIG_LABGUARD_ACTUATOR_ALARM_GPIO, alarm_gpio_level(on));
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "alarm GPIO%d write failed: %s", CONFIG_LABGUARD_ACTUATOR_ALARM_GPIO, esp_err_to_name(ret));
    }
}

static esp_err_t alarm_strip_flush(void)
{
    if (!s_alarm_strip_ready || s_alarm_strip_chan == NULL || s_alarm_strip_encoder == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    rmt_transmit_config_t tx_config = {
        .loop_count = 0,
    };
    esp_err_t ret = rmt_transmit(s_alarm_strip_chan,
                                 s_alarm_strip_encoder,
                                 s_alarm_strip_pixels,
                                 sizeof(s_alarm_strip_pixels),
                                 &tx_config);
    if (ret == ESP_OK) {
        ret = rmt_tx_wait_all_done(s_alarm_strip_chan, 100);
    }
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "alarm strip refresh failed: %s", esp_err_to_name(ret));
    }
    return ret;
}

static void alarm_strip_fill(uint8_t red, uint8_t green, uint8_t blue)
{
    for (int i = 0; i < CONFIG_LABGUARD_ACTUATOR_ALARM_STRIP_LED_COUNT; ++i) {
        s_alarm_strip_pixels[i * 3 + 0] = green;
        s_alarm_strip_pixels[i * 3 + 1] = red;
        s_alarm_strip_pixels[i * 3 + 2] = blue;
    }
}

static void set_alarm_strip_raw(bool on)
{
    if (!s_alarm_strip_ready) {
        return;
    }

    if (on) {
        alarm_strip_fill(CONFIG_LABGUARD_ACTUATOR_ALARM_STRIP_BRIGHTNESS, 0, 0);
    } else {
        alarm_strip_fill(0, 0, 0);
    }
    alarm_strip_flush();
}

static void alarm_strip_task(void *arg)
{
    (void)arg;
    bool lit = false;

    while (true) {
        if (s_alarm_strip_on) {
            lit = !lit;
            set_alarm_strip_raw(lit);
            ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(CONFIG_LABGUARD_ACTUATOR_ALARM_STRIP_BLINK_MS));
        } else {
            if (lit) {
                lit = false;
                set_alarm_strip_raw(false);
            }
            ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(100));
        }
    }
}

static void set_alarm_strip(bool on)
{
    s_alarm_strip_on = on;
    if (s_alarm_strip_task != NULL) {
        xTaskNotifyGive(s_alarm_strip_task);
    }
}

static esp_err_t configure_alarm_strip(void)
{
    if (CONFIG_LABGUARD_ACTUATOR_ALARM_STRIP_GPIO < 0) {
        return ESP_OK;
    }

    rmt_tx_channel_config_t tx_chan_config = {
        .gpio_num = CONFIG_LABGUARD_ACTUATOR_ALARM_STRIP_GPIO,
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = ALARM_STRIP_RESOLUTION_HZ,
        .mem_block_symbols = 64,
        .trans_queue_depth = 4,
    };
    esp_err_t ret = rmt_new_tx_channel(&tx_chan_config, &s_alarm_strip_chan);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "alarm strip RMT channel on GPIO%d failed: %s",
                 CONFIG_LABGUARD_ACTUATOR_ALARM_STRIP_GPIO,
                 esp_err_to_name(ret));
        return ret;
    }

    alarm_strip_encoder_config_t encoder_config = {
        .resolution_hz = ALARM_STRIP_RESOLUTION_HZ,
    };
    ret = alarm_strip_new_encoder(&encoder_config, &s_alarm_strip_encoder);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "alarm strip encoder init failed: %s", esp_err_to_name(ret));
        rmt_del_channel(s_alarm_strip_chan);
        s_alarm_strip_chan = NULL;
        return ret;
    }

    ret = rmt_enable(s_alarm_strip_chan);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "alarm strip RMT enable failed: %s", esp_err_to_name(ret));
        rmt_del_encoder(s_alarm_strip_encoder);
        rmt_del_channel(s_alarm_strip_chan);
        s_alarm_strip_encoder = NULL;
        s_alarm_strip_chan = NULL;
        return ret;
    }

    s_alarm_strip_ready = true;
    set_alarm_strip_raw(false);

    BaseType_t task_ret = xTaskCreate(alarm_strip_task,
                                      "alarm_strip",
                                      ALARM_STRIP_TASK_STACK_BYTES,
                                      NULL,
                                      4,
                                      &s_alarm_strip_task);
    if (task_ret != pdPASS) {
        ESP_LOGW(TAG, "alarm strip task create failed");
        s_alarm_strip_ready = false;
        rmt_disable(s_alarm_strip_chan);
        rmt_del_encoder(s_alarm_strip_encoder);
        rmt_del_channel(s_alarm_strip_chan);
        s_alarm_strip_encoder = NULL;
        s_alarm_strip_chan = NULL;
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG,
             "alarm RGB strip ready gpio=GPIO%d leds=%d brightness=%d blink=%dms",
             CONFIG_LABGUARD_ACTUATOR_ALARM_STRIP_GPIO,
             CONFIG_LABGUARD_ACTUATOR_ALARM_STRIP_LED_COUNT,
             CONFIG_LABGUARD_ACTUATOR_ALARM_STRIP_BRIGHTNESS,
             CONFIG_LABGUARD_ACTUATOR_ALARM_STRIP_BLINK_MS);
    return ESP_OK;
}

static void configure_ledc_timer_once(void)
{
    if (s_ledc_timer_configured) {
        return;
    }

    ledc_timer_config_t timer_cfg = {
        .speed_mode = ACTUATOR_LEDC_MODE,
        .duty_resolution = ACTUATOR_LEDC_DUTY_RES,
        .timer_num = ACTUATOR_LEDC_TIMER,
        .freq_hz = ACTUATOR_LEDC_FREQ_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    esp_err_t ret = ledc_timer_config(&timer_cfg);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "LEDC timer config failed: %s", esp_err_to_name(ret));
        return;
    }

    s_ledc_timer_configured = true;
}

static void configure_ledc_channel(int gpio_num, ledc_channel_t channel, bool active_low)
{
    if (!should_use_pwm(gpio_num)) {
        return;
    }

    configure_ledc_timer_once();
    if (!s_ledc_timer_configured) {
        return;
    }

    ledc_channel_config_t channel_cfg = {
        .gpio_num = gpio_num,
        .speed_mode = ACTUATOR_LEDC_MODE,
        .channel = channel,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = ACTUATOR_LEDC_TIMER,
        .duty = level_pct_to_duty(0, active_low),
        .hpoint = 0,
    };
    esp_err_t ret = ledc_channel_config(&channel_cfg);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "LEDC channel config failed for GPIO%d: %s", gpio_num, esp_err_to_name(ret));
    }
}

static void set_pwm_level(int gpio_num, ledc_channel_t channel, int level_pct, bool active_low)
{
    if (!should_use_pwm(gpio_num)) {
        return;
    }
    if (!s_ledc_timer_configured) {
        return;
    }

    uint32_t duty = level_pct_to_duty(level_pct, active_low);
    esp_err_t ret = ledc_set_duty(ACTUATOR_LEDC_MODE, channel, duty);
    if (ret == ESP_OK) {
        ret = ledc_update_duty(ACTUATOR_LEDC_MODE, channel);
    }
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "LEDC duty update failed for GPIO%d: %s", gpio_num, esp_err_to_name(ret));
    }
}

esp_err_t actuator_ctrl_init(void)
{
    memset(&s_last_risk, 0, sizeof(s_last_risk));
    s_last_risk.risk_text = "normal";
    s_last_risk.fan_level_pct = 100;
    s_last_risk.pump_level_pct = 100;
    s_ledc_timer_configured = false;
    s_alarm_strip_on = false;

    configure_output(CONFIG_LABGUARD_ACTUATOR_FAN_GPIO);
    configure_output(CONFIG_LABGUARD_ACTUATOR_PUMP_GPIO);
    configure_output(CONFIG_LABGUARD_ACTUATOR_ALARM_GPIO);
    configure_ledc_channel(CONFIG_LABGUARD_ACTUATOR_FAN_GPIO, ACTUATOR_LEDC_FAN_CHANNEL, FAN_ACTIVE_LOW);
    configure_ledc_channel(CONFIG_LABGUARD_ACTUATOR_PUMP_GPIO, ACTUATOR_LEDC_PUMP_CHANNEL, PUMP_ACTIVE_LOW);

    set_pwm_level(CONFIG_LABGUARD_ACTUATOR_FAN_GPIO, ACTUATOR_LEDC_FAN_CHANNEL, 0, FAN_ACTIVE_LOW);
    set_pwm_level(CONFIG_LABGUARD_ACTUATOR_PUMP_GPIO, ACTUATOR_LEDC_PUMP_CHANNEL, 0, PUMP_ACTIVE_LOW);
    set_alarm_gpio(false);
    configure_alarm_strip();

    ESP_LOGI(TAG, "actuator controller initialized fan=GPIO%d pump=GPIO%d alarm=GPIO%d strip=GPIO%d",
             CONFIG_LABGUARD_ACTUATOR_FAN_GPIO,
             CONFIG_LABGUARD_ACTUATOR_PUMP_GPIO,
             CONFIG_LABGUARD_ACTUATOR_ALARM_GPIO,
             CONFIG_LABGUARD_ACTUATOR_ALARM_STRIP_GPIO);
    return ESP_OK;
}

esp_err_t actuator_ctrl_apply_risk(const labguard_risk_state_t *risk)
{
    if (risk == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    s_last_risk = *risk;
    if (s_last_risk.action_fan && s_last_risk.fan_level_pct < 0) {
        s_last_risk.fan_level_pct = 100;
    }
    if (!s_last_risk.action_fan) {
        s_last_risk.fan_level_pct = 0;
    }
    if (s_last_risk.action_pump && s_last_risk.pump_level_pct < 0) {
        s_last_risk.pump_level_pct = 100;
    }
    if (!s_last_risk.action_pump) {
        s_last_risk.pump_level_pct = 0;
    }

    set_pwm_level(CONFIG_LABGUARD_ACTUATOR_FAN_GPIO,
                  ACTUATOR_LEDC_FAN_CHANNEL,
                  risk->action_fan ? s_last_risk.fan_level_pct : 0,
                  FAN_ACTIVE_LOW);
    set_pwm_level(CONFIG_LABGUARD_ACTUATOR_PUMP_GPIO,
                  ACTUATOR_LEDC_PUMP_CHANNEL,
                  risk->action_pump ? s_last_risk.pump_level_pct : 0,
                  PUMP_ACTIVE_LOW);
    set_alarm_gpio(risk->action_alarm);
    set_alarm_strip(risk->action_alarm);

    ESP_LOGI(TAG,
             "risk=%s alarm=%d fan=%d(%d%%) pump=%d(%d%%) temp=%.1f",
             labguard_risk_level_to_string(risk->risk_level),
             risk->action_alarm,
             risk->action_fan,
             s_last_risk.fan_level_pct,
             risk->action_pump,
             s_last_risk.pump_level_pct,
             risk->temperature_c);
    return ESP_OK;
}

esp_err_t actuator_ctrl_set_fan(bool on)
{
    s_last_risk.action_fan = on;
    if (on && s_last_risk.fan_level_pct <= 0) {
        s_last_risk.fan_level_pct = 100;
    }
    set_pwm_level(CONFIG_LABGUARD_ACTUATOR_FAN_GPIO,
                  ACTUATOR_LEDC_FAN_CHANNEL,
                  on ? s_last_risk.fan_level_pct : 0,
                  FAN_ACTIVE_LOW);
    ESP_LOGI(TAG, "manual fan=%d", on);
    return ESP_OK;
}

esp_err_t actuator_ctrl_set_fan_level(int level_pct)
{
    s_last_risk.fan_level_pct = clamp_level_pct(level_pct);
    if (s_last_risk.action_fan) {
        set_pwm_level(CONFIG_LABGUARD_ACTUATOR_FAN_GPIO,
                      ACTUATOR_LEDC_FAN_CHANNEL,
                      s_last_risk.fan_level_pct,
                      FAN_ACTIVE_LOW);
    }
    ESP_LOGI(TAG, "manual fan level=%d%%", s_last_risk.fan_level_pct);
    return ESP_OK;
}

esp_err_t actuator_ctrl_set_pump(bool on)
{
    s_last_risk.action_pump = on;
    if (on && s_last_risk.pump_level_pct <= 0) {
        s_last_risk.pump_level_pct = 100;
    }
    set_pwm_level(CONFIG_LABGUARD_ACTUATOR_PUMP_GPIO,
                  ACTUATOR_LEDC_PUMP_CHANNEL,
                  on ? s_last_risk.pump_level_pct : 0,
                  PUMP_ACTIVE_LOW);
    ESP_LOGI(TAG, "manual pump=%d", on);
    return ESP_OK;
}

esp_err_t actuator_ctrl_set_pump_level(int level_pct)
{
    s_last_risk.pump_level_pct = clamp_level_pct(level_pct);
    if (s_last_risk.action_pump) {
        set_pwm_level(CONFIG_LABGUARD_ACTUATOR_PUMP_GPIO,
                      ACTUATOR_LEDC_PUMP_CHANNEL,
                      s_last_risk.pump_level_pct,
                      PUMP_ACTIVE_LOW);
    }
    ESP_LOGI(TAG, "manual pump level=%d%%", s_last_risk.pump_level_pct);
    return ESP_OK;
}

esp_err_t actuator_ctrl_set_alarm(bool on)
{
    s_last_risk.action_alarm = on;
    set_alarm_gpio(on);
    set_alarm_strip(on);
    ESP_LOGI(TAG, "manual alarm=%d", on);
    return ESP_OK;
}

const labguard_risk_state_t *actuator_ctrl_get_last_risk(void)
{
    return &s_last_risk;
}
