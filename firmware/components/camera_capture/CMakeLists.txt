idf_component_register(
    SRCS "indoor_camera_capture.c"
    INCLUDE_DIRS "include"
    REQUIRES
        esp_driver_gpio
        esp_driver_ledc
        esp_driver_isp
        esp_driver_cam
        esp_driver_i2c
        esp_lcd
        esp_lcd_ek79007
        esp_mm
        esp_psram
        esp_rom
        esp_cam_sensor
        esp_sccb_intf
        sensor_reader
        freertos
)
