idf_component_register(
    SRCS "hazard_infer.cpp"
    INCLUDE_DIRS "include"
    REQUIRES esp_timer labguard_common esp-dl
)

