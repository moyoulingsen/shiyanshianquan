#include "actuator_ctrl.h"

#include <string.h>

#include "driver/gpio.h"
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

static const char *TAG = "actuator_ctrl";
static labguard_risk_state_t s_last_risk;

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

    configure_output(CONFIG_LABGUARD_ACTUATOR_FAN_GPIO);
    configure_output(CONFIG_LABGUARD_ACTUATOR_PUMP_GPIO);
    configure_output(CONFIG_LABGUARD_ACTUATOR_ALARM_GPIO);

    drive(CONFIG_LABGUARD_ACTUATOR_FAN_GPIO, false, FAN_ACTIVE_LOW);
    drive(CONFIG_LABGUARD_ACTUATOR_PUMP_GPIO, false, PUMP_ACTIVE_LOW);
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

    drive(CONFIG_LABGUARD_ACTUATOR_FAN_GPIO, risk->action_fan, FAN_ACTIVE_LOW);
    drive(CONFIG_LABGUARD_ACTUATOR_PUMP_GPIO, risk->action_pump, PUMP_ACTIVE_LOW);
    drive(CONFIG_LABGUARD_ACTUATOR_ALARM_GPIO, alarm_output_on(risk), ALARM_ACTIVE_LOW);

    ESP_LOGI(TAG,
             "risk=%s alarm=%d fan=%d pump=%d temp=%.1f",
             labguard_risk_level_to_string(risk->risk_level),
             risk->action_alarm,
             risk->action_fan,
             risk->action_pump,
             risk->temperature_c);
    return ESP_OK;
}

esp_err_t actuator_ctrl_set_fan(bool on)
{
    drive(CONFIG_LABGUARD_ACTUATOR_FAN_GPIO, on, FAN_ACTIVE_LOW);
    s_last_risk.action_fan = on;
    ESP_LOGI(TAG, "manual fan=%d", on);
    return ESP_OK;
}

const labguard_risk_state_t *actuator_ctrl_get_last_risk(void)
{
    return &s_last_risk;
}
