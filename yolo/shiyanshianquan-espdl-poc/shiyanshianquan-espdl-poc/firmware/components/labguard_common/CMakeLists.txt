idf_component_register(
    SRCS "labguard_common.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_timer
    PRIV_REQUIRES espressif__cjson
)
