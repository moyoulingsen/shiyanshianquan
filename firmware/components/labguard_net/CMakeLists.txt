idf_component_register(
    SRCS "labguard_net.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_wifi esp_event nvs_flash esp-mqtt labguard_common
)

