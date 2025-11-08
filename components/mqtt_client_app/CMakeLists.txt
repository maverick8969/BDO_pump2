idf_component_register(
    SRCS "mqtt_client_app.c"
    INCLUDE_DIRS "../../include"
    REQUIRES mqtt esp_event json esp_timer
)
