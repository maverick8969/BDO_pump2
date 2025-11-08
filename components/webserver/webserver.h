/**
 * @file webserver.h
 * @brief Web server for BDO pump controller WebUI
 */

#ifndef WEBSERVER_H
#define WEBSERVER_H

#include "esp_err.h"
#include "system_state.h"

/**
 * @brief Initialize and start the web server
 * @return ESP_OK on success
 */
esp_err_t webserver_init(void);

/**
 * @brief Stop the web server
 * @return ESP_OK on success
 */
esp_err_t webserver_stop(void);

#endif // WEBSERVER_H
