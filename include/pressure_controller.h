/**
 * @file pressure_controller.h
 * @brief ITV2030 pressure controller (DAC output 0-10V)
 */

#ifndef PRESSURE_CONTROLLER_H
#define PRESSURE_CONTROLLER_H

#include "esp_err.h"

/**
 * @brief Initialize DAC and pressure control
 * @return ESP_OK on success
 */
esp_err_t pressure_controller_init(void);

/**
 * @brief Set pressure as percentage (0-100%)
 * @param percent Pressure percentage (0.0 - 100.0)
 * @return ESP_OK on success
 */
esp_err_t pressure_controller_set_percent(float percent);

/**
 * @brief Read ITV2030 PNP feedback status
 * @return true if pressure reached, false otherwise
 */
bool pressure_controller_get_feedback(void);

#endif // PRESSURE_CONTROLLER_H
