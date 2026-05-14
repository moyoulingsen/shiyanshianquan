#include "labguard_net.h"

#include <stdio.h>
#include <stdbool.h>
#include <string.h>

#include "esp_log.h"
#include "nvs_flash.h"
#include "soc/soc_caps.h"

#if SOC_WIFI_SUPPORTED
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "mqtt_client.h"
#endif

static const char *TAG = "labguard_net";

static labguard_net_config_t s_config;
static bool s_wifi_ready;
static bool s_mqtt_ready;
static const int s_max_subscriptions = 8;

#if SOC_WIFI_SUPPORTED
static esp_mqtt_client_handle_t s_mqtt_client;
static bool s_netif_ready;
#endif

typedef struct {
    char topic[96];
    int qos;
    bool used;
} subscription_slot_t;

static subscription_slot_t s_subscriptions[8];

static bool str_not_empty(const char *value)
{
    return value != NULL && value[0] != '\0' && strcmp(value, "CONFIGURE_ME") != 0;
}

static void remember_subscription(const char *topic, int qos)
{
    for (int i = 0; i < s_max_subscriptions; ++i) {
        if (s_subscriptions[i].used && strcmp(s_subscriptions[i].topic, topic) == 0) {
            s_subscriptions[i].qos = qos;
            return;
        }
    }

    for (int i = 0; i < s_max_subscriptions; ++i) {
        if (!s_subscriptions[i].used) {
            snprintf(s_subscriptions[i].topic, sizeof(s_subscriptions[i].topic), "%s", topic);
            s_subscriptions[i].qos = qos;
            s_subscriptions[i].used = true;
            return;
        }
    }

    ESP_LOGW(TAG, "subscription cache full, dropping topic=%s", topic);
}

#if SOC_WIFI_SUPPORTED

static void subscribe_all_registered_topics(void)
{
    for (int i = 0; i < s_max_subscriptions; ++i) {
        if (!s_subscriptions[i].used || s_subscriptions[i].topic[0] == '\0') {
            continue;
        }
        int msg_id = esp_mqtt_client_subscribe(s_mqtt_client, s_subscriptions[i].topic, s_subscriptions[i].qos);
        ESP_LOGI(TAG, "subscribe topic=%s qos=%d msg_id=%d", s_subscriptions[i].topic, s_subscriptions[i].qos, msg_id);
    }
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    (void)handler_args;
    (void)base;

    esp_mqtt_event_handle_t event = event_data;
    if (event == NULL) {
        return;
    }

    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        s_mqtt_ready = true;
        ESP_LOGI(TAG, "MQTT connected");
        subscribe_all_registered_topics();
        break;
    case MQTT_EVENT_DISCONNECTED:
        s_mqtt_ready = false;
        ESP_LOGW(TAG, "MQTT disconnected");
        break;
    case MQTT_EVENT_DATA:
        if (s_config.message_cb != NULL) {
            char topic[128];
            int topic_len = event->topic_len;
            if (topic_len < 0) {
                topic_len = 0;
            }
            if (topic_len >= (int)sizeof(topic)) {
                topic_len = sizeof(topic) - 1;
            }
            memcpy(topic, event->topic, (size_t)topic_len);
            topic[topic_len] = '\0';
            s_config.message_cb(topic, event->data, event->data_len, s_config.user_ctx);
        }
        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGW(TAG, "MQTT event error");
        break;
    default:
        break;
    }
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    (void)arg;
    (void)event_data;

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
        return;
    }

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        s_wifi_ready = false;
        s_mqtt_ready = false;
        ESP_LOGW(TAG, "Wi-Fi disconnected, retrying");
        esp_wifi_connect();
        return;
    }

    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        s_wifi_ready = true;
        ESP_LOGI(TAG, "Wi-Fi connected and got IP");
    }
}

#endif /* SOC_WIFI_SUPPORTED */

esp_err_t labguard_net_init(const labguard_net_config_t *config)
{
    esp_err_t err;

    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    s_config = *config;
    s_wifi_ready = false;
    s_mqtt_ready = false;
    memset(s_subscriptions, 0, sizeof(s_subscriptions));

    err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        return err;
    }

#if SOC_WIFI_SUPPORTED
    if (str_not_empty(s_config.wifi_ssid)) {
        if (!s_netif_ready) {
            ESP_ERROR_CHECK(esp_netif_init());
            ESP_ERROR_CHECK(esp_event_loop_create_default());
            esp_netif_create_default_wifi_sta();
            s_netif_ready = true;
        }

        ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
        ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL));

        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        ESP_ERROR_CHECK(esp_wifi_init(&cfg));
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

        wifi_config_t wifi_config = {0};
        snprintf((char *)wifi_config.sta.ssid, sizeof(wifi_config.sta.ssid), "%s", s_config.wifi_ssid);
        snprintf((char *)wifi_config.sta.password, sizeof(wifi_config.sta.password), "%s", s_config.wifi_password ? s_config.wifi_password : "");
        wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
        wifi_config.sta.pmf_cfg.capable = true;
        wifi_config.sta.pmf_cfg.required = false;
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    }
#endif

    ESP_LOGI(TAG,
             "network config staged: ssid=%s mqtt=%s",
             s_config.wifi_ssid ? s_config.wifi_ssid : "(unset)",
             s_config.mqtt_uri ? s_config.mqtt_uri : "(unset)");
    return ESP_OK;
}

esp_err_t labguard_net_start(void)
{
#if SOC_WIFI_SUPPORTED
    if (str_not_empty(s_config.wifi_ssid)) {
        ESP_ERROR_CHECK(esp_wifi_start());
    } else {
        ESP_LOGW(TAG, "Wi-Fi credentials not configured, running in local-only mode");
    }

    if (str_not_empty(s_config.mqtt_uri)) {
        esp_mqtt_client_config_t mqtt_cfg = {
            .broker.address.uri = s_config.mqtt_uri,
        };
        s_mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
        if (s_mqtt_client == NULL) {
            return ESP_FAIL;
        }
        ESP_ERROR_CHECK(esp_mqtt_client_register_event(s_mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL));
        ESP_ERROR_CHECK(esp_mqtt_client_start(s_mqtt_client));
    } else {
        ESP_LOGW(TAG, "MQTT URI not configured, publish/subscribe will stay local");
    }
#else
    ESP_LOGW(TAG, "Wi-Fi/MQTT not supported on this SoC, running in local-only mode");
#endif

    return ESP_OK;
}

esp_err_t labguard_net_publish(const char *topic, const char *payload, int qos, bool retain)
{
    if (topic == NULL || payload == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

#if SOC_WIFI_SUPPORTED
    if (s_mqtt_client != NULL && s_mqtt_ready) {
        int msg_id = esp_mqtt_client_publish(s_mqtt_client, topic, payload, 0, qos, retain ? 1 : 0);
        if (msg_id < 0) {
            ESP_LOGW(TAG, "MQTT publish failed topic=%s", topic);
            return ESP_FAIL;
        }
        return ESP_OK;
    }
#endif

    ESP_LOGI(TAG, "local publish topic=%s qos=%d retain=%d payload=%s", topic, qos, retain, payload);
    return ESP_OK;
}

esp_err_t labguard_net_subscribe(const char *topic, int qos)
{
    if (topic == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    remember_subscription(topic, qos);

#if SOC_WIFI_SUPPORTED
    if (s_mqtt_client != NULL && s_mqtt_ready) {
        int msg_id = esp_mqtt_client_subscribe(s_mqtt_client, topic, qos);
        if (msg_id < 0) {
            ESP_LOGW(TAG, "MQTT subscribe failed topic=%s", topic);
            return ESP_FAIL;
        }
        return ESP_OK;
    }
#endif

    ESP_LOGI(TAG, "local subscribe topic=%s qos=%d", topic, qos);
    return ESP_OK;
}

bool labguard_net_is_connected(void)
{
#if SOC_WIFI_SUPPORTED
    if (s_mqtt_client != NULL) {
        return s_wifi_ready && s_mqtt_ready;
    }
#endif

    return s_wifi_ready;
}

int labguard_net_get_rssi(void)
{
#if SOC_WIFI_SUPPORTED
    wifi_ap_record_t ap = {0};
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
        return ap.rssi;
    }
#endif
    return 0;
}
