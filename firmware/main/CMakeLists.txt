idf_component_register(
    SRCS "app_main.c"
    INCLUDE_DIRS "."
    REQUIRES
        labguard_common
        labguard_net
        event_log
        sensor_reader
        risk_fusion
        actuator_ctrl
        audio_prompt
        camera_capture
        mbedtls
        esp_driver_jpeg
        fatfs
        sdmmc
)
