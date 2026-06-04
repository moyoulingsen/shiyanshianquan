idf_component_register(
    SRCS "app_main.c"
    INCLUDE_DIRS "."
    REQUIRES
        labguard_common
        labguard_net
        event_log
        camera_capture
        espdl_probe
        ppe_infer
        door_fsm
        ui_lvgl
        audio_prompt
        led_status
        fatfs
        sdmmc
)

