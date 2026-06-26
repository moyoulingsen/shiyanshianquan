#pragma once

#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*labguard_net_message_cb_t)(const char *topic, const char *payload, int payload_len, void *user_ctx);

typedef struct {
    const char *wifi_ssid;
    const char *wifi_password;
    const char *mqtt_uri;
    labguard_net_message_cb_t message_cb;
    void *user_ctx;
} labguard_net_config_t;

esp_err_t labguard_net_init(const labguard_net_config_t *config);
esp_err_t labguard_net_start(void);
esp_err_t labguard_net_publish(const char *topic, const char *payload, int qos, bool retain);
esp_err_t labguard_net_subscribe(const char *topic, int qos);
bool labguard_net_is_connected(void);
int labguard_net_get_rssi(void);

#ifdef __cplusplus
}
#endif
