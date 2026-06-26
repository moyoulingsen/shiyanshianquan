idf_component_register(
    SRCS "app_main.c"
    INCLUDE_DIRS "."
    REQUIRES
        labguard_common
        labguard_net
        event_log
        espdl_probe
        sensor_reader
        risk_fusion
        actuator_ctrl
        audio_prompt
        camera_capture
        mbedtls
        esp_driver_jpeg
        esp_driver_usb_serial_jtag
        fatfs
        sdmmc
)
