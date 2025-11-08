/**
 * @file safety_system.h
 * @brief 4-stage safety interlock system using LCD prompts
 *
 * Safety checks displayed on LCD, confirmed with rotary encoder button.
 * Sequence:
 *   1. Air line connection check
 *   2. Fill hose connection check
 *   3. Tank/valve position check
 *   4. Final start confirmation
 *
 * Each check shows prompt on LCD and waits for encoder button press.
 */

#ifndef SAFETY_SYSTEM_H
#define SAFETY_SYSTEM_H

#include "esp_err.h"
#include "system_state.h"

/**
 * @brief Initialize safety system
 * @return ESP_OK on success
 */
esp_err_t safety_init(void);

/**
 * @brief Run safety check sequence (non-blocking state machine)
 *
 * Call this repeatedly from display task. Updates g_system_state.safety_state.
 * Displays prompts on LCD and waits for encoder button confirmation.
 *
 * @return ESP_OK if all checks complete, ESP_FAIL on timeout/cancel
 */
esp_err_t safety_run_checks(void);

/**
 * @brief Cancel safety check sequence
 */
void safety_cancel(void);

/**
 * @brief Get current safety check prompt text
 * @param line1 Buffer for LCD line 1 (16 chars + null)
 * @param line2 Buffer for LCD line 2 (16 chars + null)
 */
void safety_get_prompt(char *line1, char *line2);

#endif // SAFETY_SYSTEM_H
