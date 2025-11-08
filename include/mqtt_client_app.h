/**
 * @file mqtt_client_app.h
 * @brief MQTT client for factory integration
 */

#ifndef MQTT_CLIENT_APP_H
#define MQTT_CLIENT_APP_H

#include "esp_err.h"
#include "system_state.h"

/**
 * @brief Start MQTT client
 * @return ESP_OK on success
 */
esp_err_t mqtt_app_start(void);

/**
 * @brief Publish fill completion event
 * @return ESP_OK on success
 */
esp_err_t mqtt_publish_fill_complete(void);

/**
 * @brief Publish system status
 * @param state Pointer to system state
 * @return ESP_OK on success
 */
esp_err_t mqtt_publish_status(system_state_t *state);

/**
 * @brief Publish system event
 * @param event Event type string
 * @param details Event details
 * @return ESP_OK on success
 */
esp_err_t mqtt_publish_event(const char *event, const char *details);

#endif // MQTT_CLIENT_APP_H
