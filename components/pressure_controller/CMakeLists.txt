idf_component_register(
    SRCS "pressure_controller.c"
    INCLUDE_DIRS "../../include"
    REQUIRES driver esp_timer nvs_flash
)
