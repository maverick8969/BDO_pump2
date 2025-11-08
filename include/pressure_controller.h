/**
 * @file pressure_controller.h
 * @brief ITV2030 pressure controller (DAC output 0-10V) with PID control
 */

#ifndef PRESSURE_CONTROLLER_H
#define PRESSURE_CONTROLLER_H

#include "esp_err.h"
#include <stdbool.h>

/**
 * @brief Initialize DAC and pressure control
 * @return ESP_OK on success
 */
esp_err_t pressure_controller_init(void);

/**
 * @brief Set pressure as percentage (0-100%) - direct open-loop control
 * @param percent Pressure percentage (0.0 - 100.0)
 * @return ESP_OK on success
 */
esp_err_t pressure_controller_set_percent(float percent);

/**
 * @brief Read ITV2030 PNP feedback status
 * @return true if pressure reached, false otherwise
 */
bool pressure_controller_get_feedback(void);

/**
 * @brief Set PID parameters
 * @param kp Proportional gain
 * @param ki Integral gain
 * @param kd Derivative gain
 * @return ESP_OK on success
 */
esp_err_t pressure_controller_set_pid_params(float kp, float ki, float kd);

/**
 * @brief Get current PID parameters
 * @param kp Pointer to store Kp value
 * @param ki Pointer to store Ki value
 * @param kd Pointer to store Kd value
 * @return ESP_OK on success
 */
esp_err_t pressure_controller_get_pid_params(float *kp, float *ki, float *kd);

/**
 * @brief Load PID parameters from NVS
 * @return ESP_OK on success, ESP_ERR_NOT_FOUND if not saved
 */
esp_err_t pressure_controller_load_pid_params(void);

/**
 * @brief Save PID parameters to NVS
 * @return ESP_OK on success
 */
esp_err_t pressure_controller_save_pid_params(void);

/**
 * @brief PID control update - computes output based on error
 * @param setpoint Desired flow rate (lbs/sec or similar process variable)
 * @param measurement Current flow rate
 * @return Computed pressure percentage (0-100%)
 */
float pressure_controller_compute_pid(float setpoint, float measurement);

/**
 * @brief Reset PID controller (clear integral, derivative history)
 */
void pressure_controller_reset_pid(void);

/**
 * @brief Start auto-tune sequence (Relay/Ziegler-Nichols method)
 * @return ESP_OK on success
 */
esp_err_t pressure_controller_start_autotune(void);

/**
 * @brief Run auto-tune state machine (call periodically during auto-tune)
 * @param current_weight Current weight from scale
 * @return ESP_OK if complete, ESP_ERR_INVALID_STATE if in progress, ESP_FAIL on error
 */
esp_err_t pressure_controller_run_autotune(float current_weight);

/**
 * @brief Cancel auto-tune sequence
 */
void pressure_controller_cancel_autotune(void);

/**
 * @brief Check if auto-tune is running
 * @return true if auto-tune in progress
 */
bool pressure_controller_is_autotuning(void);

/**
 * @brief Get auto-tune results
 * @param kp Pointer to store calculated Kp
 * @param ki Pointer to store calculated Ki
 * @param kd Pointer to store calculated Kd
 * @return ESP_OK if results available
 */
esp_err_t pressure_controller_get_autotune_results(float *kp, float *ki, float *kd);

#endif // PRESSURE_CONTROLLER_H
