#include "actuator_ctrl.h"

#include <string.h>

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_log.h"
#include "esp_timer.h"
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

static const char *TAG = "actuator_ctrl";
static labguard_risk_state_t s_last_risk;
static bool s_ledc_timer_configured;

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

static void drive(int gpio_num, bool on, bool active_low)
{
    if (gpio_num < 0) {
        return;
    }
    int level = on ? 1 : 0;
    if (active_low) {
        level = !level;
    }
    gpio_set_level(gpio_num, level);
}

static bool alarm_output_on(const labguard_risk_state_t *risk)
{
    if (risk == NULL || !risk->action_alarm) {
        return false;
    }

    if (risk->risk_level < LABGUARD_RISK_ALARM) {
        return true;
    }

    if (risk->risk_level >= LABGUARD_RISK_EMERGENCY) {
        return true;
    }

    int64_t tick_ms = esp_timer_get_time() / 1000;
    return ((tick_ms / 500) % 2) == 0;
}

esp_err_t actuator_ctrl_init(void)
{
    memset(&s_last_risk, 0, sizeof(s_last_risk));
    s_last_risk.risk_text = "normal";
    s_last_risk.fan_level_pct = 100;
    s_last_risk.pump_level_pct = 100;
    s_ledc_timer_configured = false;

    configure_output(CONFIG_LABGUARD_ACTUATOR_FAN_GPIO);
    configure_output(CONFIG_LABGUARD_ACTUATOR_PUMP_GPIO);
    configure_output(CONFIG_LABGUARD_ACTUATOR_ALARM_GPIO);
    configure_ledc_channel(CONFIG_LABGUARD_ACTUATOR_FAN_GPIO, ACTUATOR_LEDC_FAN_CHANNEL, FAN_ACTIVE_LOW);
    configure_ledc_channel(CONFIG_LABGUARD_ACTUATOR_PUMP_GPIO, ACTUATOR_LEDC_PUMP_CHANNEL, PUMP_ACTIVE_LOW);

    set_pwm_level(CONFIG_LABGUARD_ACTUATOR_FAN_GPIO, ACTUATOR_LEDC_FAN_CHANNEL, 0, FAN_ACTIVE_LOW);
    set_pwm_level(CONFIG_LABGUARD_ACTUATOR_PUMP_GPIO, ACTUATOR_LEDC_PUMP_CHANNEL, 0, PUMP_ACTIVE_LOW);
    drive(CONFIG_LABGUARD_ACTUATOR_ALARM_GPIO, false, ALARM_ACTIVE_LOW);

    ESP_LOGI(TAG, "actuator controller initialized fan=GPIO%d pump=GPIO%d alarm=GPIO%d",
             CONFIG_LABGUARD_ACTUATOR_FAN_GPIO,
             CONFIG_LABGUARD_ACTUATOR_PUMP_GPIO,
             CONFIG_LABGUARD_ACTUATOR_ALARM_GPIO);
    return ESP_OK;
}

esp_err_t actuator_ctrl_apply_risk(const labguard_risk_state_t *risk)
{
    if (risk == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    s_last_risk = *risk;
    if (s_last_risk.fan_level_pct <= 0) {
        s_last_risk.fan_level_pct = s_last_risk.action_fan ? 100 : 0;
    }
    if (s_last_risk.pump_level_pct <= 0) {
        s_last_risk.pump_level_pct = s_last_risk.action_pump ? 100 : 0;
    }

    set_pwm_level(CONFIG_LABGUARD_ACTUATOR_FAN_GPIO,
                  ACTUATOR_LEDC_FAN_CHANNEL,
                  risk->action_fan ? s_last_risk.fan_level_pct : 0,
                  FAN_ACTIVE_LOW);
    set_pwm_level(CONFIG_LABGUARD_ACTUATOR_PUMP_GPIO,
                  ACTUATOR_LEDC_PUMP_CHANNEL,
                  risk->action_pump ? s_last_risk.pump_level_pct : 0,
                  PUMP_ACTIVE_LOW);
    drive(CONFIG_LABGUARD_ACTUATOR_ALARM_GPIO, alarm_output_on(risk), ALARM_ACTIVE_LOW);

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
    drive(CONFIG_LABGUARD_ACTUATOR_ALARM_GPIO, on, ALARM_ACTIVE_LOW);
    ESP_LOGI(TAG, "manual alarm=%d", on);
    return ESP_OK;
}

const labguard_risk_state_t *actuator_ctrl_get_last_risk(void)
{
    return &s_last_risk;
}
