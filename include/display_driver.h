/**
 * @file display_driver.h
 * @brief LCD1602 display and rotary encoder driver
 */

#ifndef DISPLAY_DRIVER_H
#define DISPLAY_DRIVER_H

#include "esp_err.h"
#include "system_state.h"

/**
 * @brief Initialize LCD display and rotary encoder
 * @return ESP_OK on success
 */
esp_err_t display_init(void);

/**
 * @brief Update display with current system state
 * @param state Pointer to system state structure
 * @return ESP_OK on success
 */
esp_err_t display_update(system_state_t *state);

/**
 * @brief Handle rotary encoder input
 * @return ESP_OK on success
 */
esp_err_t display_handle_encoder(void);

#endif // DISPLAY_DRIVER_H
