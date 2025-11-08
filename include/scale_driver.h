/**
 * @file scale_driver.h
 * @brief PS-IN202 Scale driver (RS232 communication)
 */

#ifndef SCALE_DRIVER_H
#define SCALE_DRIVER_H

#include "esp_err.h"

/**
 * @brief Initialize scale UART communication
 * @return ESP_OK on success
 */
esp_err_t scale_init(void);

/**
 * @brief Read weight from scale
 * @param weight Pointer to store weight value (lbs)
 * @return ESP_OK on success, ESP_FAIL on error
 */
esp_err_t scale_read_weight(float *weight);

#endif // SCALE_DRIVER_H
