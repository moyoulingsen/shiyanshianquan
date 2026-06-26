idf_component_register(
    SRCS "risk_fusion.c"
    INCLUDE_DIRS "include"
    REQUIRES labguard_common hazard_infer esp_timer
)

