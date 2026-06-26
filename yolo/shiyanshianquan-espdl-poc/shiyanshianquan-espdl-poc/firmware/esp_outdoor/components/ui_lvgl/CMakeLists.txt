idf_component_register(
    SRCS "ui_lvgl.c"
    INCLUDE_DIRS "include"
    REQUIRES labguard_common event_log esp_timer
)
