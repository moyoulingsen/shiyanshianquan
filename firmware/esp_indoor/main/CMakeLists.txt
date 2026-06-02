idf_component_register(
    SRCS "app_main.c"
    INCLUDE_DIRS "."
    REQUIRES
        labguard_common
        labguard_net
        event_log
        sensor_reader
        hazard_infer
        risk_fusion
        actuator_ctrl
        camera_capture
        mbedtls
        esp_driver_jpeg
        fatfs
        sdmmc
)

