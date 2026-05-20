idf_component_register(
    SRCS "sensor_reader.c"
    INCLUDE_DIRS "include"
    REQUIRES labguard_common esp_timer esp_driver_i2c esp_driver_gpio freertos
)
