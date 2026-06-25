#include "actuator_ctrl.h"

#include <string.h>

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/rmt_encoder.h"
#include "driver/rmt_tx.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
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

#ifdef CONFIG_LABGUARD_ACTUATOR_LIGHT_ACTIVE_LOW
#define LIGHT_ACTIVE_LOW 1
#else
#define LIGHT_ACTIVE_LOW 0
#endif

#define ACTUATOR_LEDC_MODE LEDC_LOW_SPEED_MODE
#define ACTUATOR_LEDC_TIMER LEDC_TIMER_0
#define ACTUATOR_LEDC_FAN_CHANNEL LEDC_CHANNEL_0
#define ACTUATOR_LEDC_PUMP_CHANNEL LEDC_CHANNEL_1
#define ACTUATOR_LEDC_DUTY_RES LEDC_TIMER_10_BIT
#define ACTUATOR_LEDC_DUTY_MAX ((1 << 10) - 1)
#define ACTUATOR_LEDC_FREQ_HZ 20000
#define ACTUATOR_LIGHT_RMT_RESOLUTION_HZ 10000000
#define ACTUATOR_LIGHT_POWER_SETTLE_MS 20
#define ACTUATOR_LIGHT_TX_TIMEOUT_MS 100
#define ACTUATOR_LIGHT_COLOR_R 64
#define ACTUATOR_LIGHT_COLOR_G 64
#define ACTUATOR_LIGHT_COLOR_B 64

static const char *TAG = "actuator_ctrl";
static labguard_risk_state_t s_last_risk;
static bool s_ledc_timer_configured;
static bool s_light_on;
static rmt_channel_handle_t s_light_rmt_chan;
static rmt_encoder_handle_t s_light_rmt_encoder;
static rmt_encoder_handle_t s_light_reset_encoder;
static bool s_light_rmt_ready;
static uint8_t s_light_pixels[CONFIG_LABGUARD_ACTUATOR_LIGHT_LED_COUNT * 3];
static const rmt_symbol_word_t s_light_reset_symbol = {
    .level0 = 0,
    .duration0 = ACTUATOR_LIGHT_RMT_RESOLUTION_HZ / 1000000 * 50 / 2,
    .level1 = 0,
    .duration1 = ACTUATOR_LIGHT_RMT_RESOLUTION_HZ / 1000000 * 50 / 2,
};

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

static uint32_t level_pct_to_duty(int level_pct, bool active_low)
{
    int clamped = clamp_level_pct(level_pct);
    uint32_t duty = (uint32_t)((clamped * ACTUATOR_LEDC_DUTY_MAX) / 100);
    if (active_low) {
        duty = ACTUATOR_LEDC_DUTY_MAX - duty;
    }
    return duty;
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

static esp_err_t set_gpio_output_level(int gpio_num, bool on, bool active_low)
{
    if (gpio_num < 0) {
        return ESP_OK;
    }

    int level = on ? 1 : 0;
    if (active_low) {
        level = !level;
    }
    esp_err_t ret = gpio_set_level(gpio_num, level);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "GPIO%d level update failed: %s", gpio_num, esp_err_to_name(ret));
    }
    return ret;
}

static esp_err_t configure_light_strip(void)
{
    if (CONFIG_LABGUARD_ACTUATOR_LIGHT_DATA_GPIO < 0) {
        ESP_LOGI(TAG, "LED strip data GPIO disabled");
        return ESP_OK;
    }

    rmt_tx_channel_config_t tx_chan_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .gpio_num = CONFIG_LABGUARD_ACTUATOR_LIGHT_DATA_GPIO,
        .resolution_hz = ACTUATOR_LIGHT_RMT_RESOLUTION_HZ,
        .mem_block_symbols = 64,
        .trans_queue_depth = 2,
    };
    esp_err_t ret = rmt_new_tx_channel(&tx_chan_config, &s_light_rmt_chan);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG,
                 "LED strip RMT channel init failed on GPIO%d: %s",
                 CONFIG_LABGUARD_ACTUATOR_LIGHT_DATA_GPIO,
                 esp_err_to_name(ret));
        return ret;
    }

    rmt_bytes_encoder_config_t bytes_encoder_config = {
        .bit0 = {
            .level0 = 1,
            .duration0 = 3,
            .level1 = 0,
            .duration1 = 9,
        },
        .bit1 = {
            .level0 = 1,
            .duration0 = 9,
            .level1 = 0,
            .duration1 = 3,
        },
        .flags.msb_first = 1,
    };
    ret = rmt_new_bytes_encoder(&bytes_encoder_config, &s_light_rmt_encoder);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "LED strip RMT encoder init failed: %s", esp_err_to_name(ret));
        rmt_del_channel(s_light_rmt_chan);
        s_light_rmt_chan = NULL;
        return ret;
    }
    rmt_copy_encoder_config_t reset_encoder_config = {};
    ret = rmt_new_copy_encoder(&reset_encoder_config, &s_light_reset_encoder);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "LED strip RMT reset encoder init failed: %s", esp_err_to_name(ret));
        rmt_del_encoder(s_light_rmt_encoder);
        rmt_del_channel(s_light_rmt_chan);
        s_light_rmt_encoder = NULL;
        s_light_rmt_chan = NULL;
        return ret;
    }

    ret = rmt_enable(s_light_rmt_chan);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "LED strip RMT enable failed: %s", esp_err_to_name(ret));
        rmt_del_encoder(s_light_reset_encoder);
        rmt_del_encoder(s_light_rmt_encoder);
        rmt_del_channel(s_light_rmt_chan);
        s_light_reset_encoder = NULL;
        s_light_rmt_encoder = NULL;
        s_light_rmt_chan = NULL;
        return ret;
    }

    s_light_rmt_ready = true;
    ESP_LOGI(TAG,
             "LED strip data initialized GPIO%d pixels=%d",
             CONFIG_LABGUARD_ACTUATOR_LIGHT_DATA_GPIO,
             CONFIG_LABGUARD_ACTUATOR_LIGHT_LED_COUNT);
    return ESP_OK;
}

static esp_err_t set_light_strip_color(uint8_t red, uint8_t green, uint8_t blue)
{
    if (!s_light_rmt_ready) {
        return ESP_ERR_INVALID_STATE;
    }

    for (int i = 0; i < CONFIG_LABGUARD_ACTUATOR_LIGHT_LED_COUNT; ++i) {
        s_light_pixels[i * 3 + 0] = green;
        s_light_pixels[i * 3 + 1] = red;
        s_light_pixels[i * 3 + 2] = blue;
    }

    rmt_transmit_config_t tx_config = {
        .loop_count = 0,
        .flags.eot_level = 0,
    };
    esp_err_t ret = rmt_transmit(s_light_rmt_chan,
                                 s_light_rmt_encoder,
                                 s_light_pixels,
                                 sizeof(s_light_pixels),
                                 &tx_config);
    if (ret == ESP_OK) {
        ret = rmt_transmit(s_light_rmt_chan,
                           s_light_reset_encoder,
                           &s_light_reset_symbol,
                           sizeof(s_light_reset_symbol),
                           &tx_config);
    }
    if (ret == ESP_OK) {
        ret = rmt_tx_wait_all_done(s_light_rmt_chan, ACTUATOR_LIGHT_TX_TIMEOUT_MS);
    }
    if (ret == ESP_OK) {
        vTaskDelay(pdMS_TO_TICKS(1));
    } else {
        ESP_LOGW(TAG, "LED strip update failed: %s", esp_err_to_name(ret));
    }
    return ret;
}

esp_err_t actuator_ctrl_init(void)
{
    memset(&s_last_risk, 0, sizeof(s_last_risk));
    s_last_risk.risk_text = "normal";
    s_last_risk.fan_level_pct = 100;
    s_last_risk.pump_level_pct = 100;
    s_ledc_timer_configured = false;
    s_light_on = false;
    s_light_rmt_chan = NULL;
    s_light_rmt_encoder = NULL;
    s_light_reset_encoder = NULL;
    s_light_rmt_ready = false;
    memset(s_light_pixels, 0, sizeof(s_light_pixels));

    configure_output(CONFIG_LABGUARD_ACTUATOR_FAN_GPIO);
    configure_output(CONFIG_LABGUARD_ACTUATOR_PUMP_GPIO);
    configure_output(CONFIG_LABGUARD_ACTUATOR_LIGHT_GPIO);
    configure_ledc_channel(CONFIG_LABGUARD_ACTUATOR_FAN_GPIO, ACTUATOR_LEDC_FAN_CHANNEL, FAN_ACTIVE_LOW);
    configure_ledc_channel(CONFIG_LABGUARD_ACTUATOR_PUMP_GPIO, ACTUATOR_LEDC_PUMP_CHANNEL, PUMP_ACTIVE_LOW);

    set_pwm_level(CONFIG_LABGUARD_ACTUATOR_FAN_GPIO, ACTUATOR_LEDC_FAN_CHANNEL, 0, FAN_ACTIVE_LOW);
    set_pwm_level(CONFIG_LABGUARD_ACTUATOR_PUMP_GPIO, ACTUATOR_LEDC_PUMP_CHANNEL, 0, PUMP_ACTIVE_LOW);
    set_gpio_output_level(CONFIG_LABGUARD_ACTUATOR_LIGHT_GPIO, false, LIGHT_ACTIVE_LOW);
    configure_light_strip();

    ESP_LOGI(TAG, "actuator controller initialized fan=GPIO%d pump=GPIO%d light=GPIO%d light_data=GPIO%d",
             CONFIG_LABGUARD_ACTUATOR_FAN_GPIO,
             CONFIG_LABGUARD_ACTUATOR_PUMP_GPIO,
             CONFIG_LABGUARD_ACTUATOR_LIGHT_GPIO,
             CONFIG_LABGUARD_ACTUATOR_LIGHT_DATA_GPIO);
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

    ESP_LOGI(TAG,
             "risk=%s alarm=%d fan=%d(%d%%) pump=%d(%d%%) light=%d temp=%.1f",
             labguard_risk_level_to_string(risk->risk_level),
             risk->action_alarm,
             risk->action_fan,
             s_last_risk.fan_level_pct,
             risk->action_pump,
             s_last_risk.pump_level_pct,
             s_light_on,
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
    return ESP_OK;
}

esp_err_t actuator_ctrl_set_light(bool on)
{
    s_light_on = on;
    esp_err_t ret = ESP_OK;
    esp_err_t strip_ret = ESP_OK;
    if (on) {
        ret = set_gpio_output_level(CONFIG_LABGUARD_ACTUATOR_LIGHT_GPIO, true, LIGHT_ACTIVE_LOW);
        vTaskDelay(pdMS_TO_TICKS(ACTUATOR_LIGHT_POWER_SETTLE_MS));
        strip_ret = set_light_strip_color(ACTUATOR_LIGHT_COLOR_R,
                                          ACTUATOR_LIGHT_COLOR_G,
                                          ACTUATOR_LIGHT_COLOR_B);
    } else {
        strip_ret = set_light_strip_color(0, 0, 0);
        ret = set_gpio_output_level(CONFIG_LABGUARD_ACTUATOR_LIGHT_GPIO, false, LIGHT_ACTIVE_LOW);
    }
    int level = on ? 1 : 0;
    if (LIGHT_ACTIVE_LOW) {
        level = !level;
    }
    if (strip_ret != ESP_OK && strip_ret != ESP_ERR_INVALID_STATE) {
        ret = strip_ret;
    }
    ESP_LOGI(TAG,
             "light %s GPIO%d level=%d data_gpio=%d strip=%s",
             on ? "on" : "off",
             CONFIG_LABGUARD_ACTUATOR_LIGHT_GPIO,
             level,
             CONFIG_LABGUARD_ACTUATOR_LIGHT_DATA_GPIO,
             s_light_rmt_ready ? "updated" : "disabled");
    return ret;
}

bool actuator_ctrl_get_light(void)
{
    return s_light_on;
}

const labguard_risk_state_t *actuator_ctrl_get_last_risk(void)
{
    return &s_last_risk;
}
