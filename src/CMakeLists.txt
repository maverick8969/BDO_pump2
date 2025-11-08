# Main component registration
# ESP-IDF v5.5.0

idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "../include"
    REQUIRES
        freertos
        nvs_flash
        esp_wifi
        esp_netif
        esp_event
        driver
        esp_timer
        pressure_controller
        safety_system
        webserver
        scale_driver
        display_driver
        mqtt_client_app
)
