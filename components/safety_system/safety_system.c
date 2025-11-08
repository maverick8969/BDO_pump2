/**
 * @file safety_system.c
 * @brief 4-stage safety interlock system implementation
 *
 * Implements non-blocking state machine for safety checks using LCD prompts
 * and rotary encoder button confirmation.
 */

#include "safety_system.h"
#include "config.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include <string.h>

static const char *TAG = "SAFETY";

/* =============================================================================
 * INTERNAL STATE
 * ===========================================================================*/

typedef struct {
    uint64_t check_start_time_us;  // Start time of current check (microseconds)
    bool button_last_state;         // Previous button state for edge detection
    bool waiting_for_release;       // Waiting for button release before next press
} safety_internal_t;

static safety_internal_t s_safety = {0};

/* External global state */
extern system_state_t g_system_state;

/* =============================================================================
 * SAFETY CHECK PROMPTS
 * ===========================================================================*/

typedef struct {
    const char *line1;
    const char *line2;
} safety_prompt_t;

static const safety_prompt_t s_prompts[] = {
    [SAFETY_IDLE] = {"Ready", "Press to start"},
    [SAFETY_AIR_CHECK] = {"SAFETY CHECK 1/4", "Air line OK?"},
    [SAFETY_HOSE_CHECK] = {"SAFETY CHECK 2/4", "Fill hose OK?"},
    [SAFETY_POSITION_CHECK] = {"SAFETY CHECK 3/4", "Tank position?"},
    [SAFETY_START_CHECK] = {"SAFETY CHECK 4/4", "Ready to fill?"},
    [SAFETY_COMPLETE] = {"Safety Complete", "Starting fill..."},
    [SAFETY_TIMEOUT] = {"SAFETY TIMEOUT", "Sequence abort"},
    [SAFETY_CANCELLED] = {"CANCELLED", "Safety aborted"}
};

/* =============================================================================
 * HELPER FUNCTIONS
 * ===========================================================================*/

/**
 * @brief Read current encoder button state
 * @return true if button is pressed (active LOW on GPIO34)
 */
static bool read_button_state(void)
{
    // GPIO34 is input-only on ESP32, active LOW when pressed
    return (gpio_get_level(PIN_ENCODER_SW) == 0);
}

/**
 * @brief Detect button press event (falling edge with debounce)
 * @return true if button was just pressed
 */
static bool button_pressed(void)
{
    bool current_state = read_button_state();
    bool pressed = false;

    // Edge detection: transition from released to pressed
    if (current_state && !s_safety.button_last_state && !s_safety.waiting_for_release) {
        pressed = true;
        s_safety.waiting_for_release = true;  // Wait for release before detecting next press
        ESP_LOGI(TAG, "Button press detected");
    }

    // Reset waiting flag when button is released
    if (!current_state && s_safety.waiting_for_release) {
        s_safety.waiting_for_release = false;
    }

    s_safety.button_last_state = current_state;
    return pressed;
}

/**
 * @brief Check if current safety check has timed out
 * @return true if timeout occurred
 */
static bool check_timeout(void)
{
    uint64_t elapsed_ms = (esp_timer_get_time() - s_safety.check_start_time_us) / 1000;
    return (elapsed_ms > SAFETY_CHECK_TIMEOUT_MS);
}

/**
 * @brief Start a new safety check stage
 * @param new_state The safety state to transition to
 */
static void start_check_stage(safety_state_t new_state)
{
    g_system_state.safety_state = new_state;
    s_safety.check_start_time_us = esp_timer_get_time();
    s_safety.waiting_for_release = true;  // Require button release before next confirmation

    ESP_LOGI(TAG, "Starting safety check stage: %s", s_prompts[new_state].line2);
}

/* =============================================================================
 * PUBLIC FUNCTIONS
 * ===========================================================================*/

esp_err_t safety_init(void)
{
    ESP_LOGI(TAG, "Initializing safety system");

    // Configure encoder button GPIO (input-only pin on ESP32)
    gpio_config_t button_cfg = {
        .pin_bit_mask = (1ULL << PIN_ENCODER_SW),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,  // Enable internal pull-up
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };

    esp_err_t ret = gpio_config(&button_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure encoder button GPIO: %s", esp_err_to_name(ret));
        return ret;
    }

    // Initialize internal state
    s_safety.check_start_time_us = 0;
    s_safety.button_last_state = false;
    s_safety.waiting_for_release = false;

    // Initialize safety state to IDLE
    g_system_state.safety_state = SAFETY_IDLE;

    ESP_LOGI(TAG, "Safety system initialized successfully");
    return ESP_OK;
}

esp_err_t safety_run_checks(void)
{
    // Check for timeout on all active check stages
    if (g_system_state.safety_state >= SAFETY_AIR_CHECK &&
        g_system_state.safety_state <= SAFETY_START_CHECK) {
        if (check_timeout()) {
            ESP_LOGW(TAG, "Safety check timeout at stage %d", g_system_state.safety_state);
            g_system_state.safety_state = SAFETY_TIMEOUT;
            g_system_state.error = ERROR_SAFETY_TIMEOUT;
            return ESP_FAIL;
        }
    }

    // State machine
    switch (g_system_state.safety_state) {
        case SAFETY_IDLE:
            // Start first check
            start_check_stage(SAFETY_AIR_CHECK);
            return ESP_ERR_INVALID_STATE;  // Still in progress

        case SAFETY_AIR_CHECK:
            if (button_pressed()) {
                ESP_LOGI(TAG, "Air line check confirmed");
                start_check_stage(SAFETY_HOSE_CHECK);
            }
            return ESP_ERR_INVALID_STATE;  // Still in progress

        case SAFETY_HOSE_CHECK:
            if (button_pressed()) {
                ESP_LOGI(TAG, "Fill hose check confirmed");
                start_check_stage(SAFETY_POSITION_CHECK);
            }
            return ESP_ERR_INVALID_STATE;  // Still in progress

        case SAFETY_POSITION_CHECK:
            if (button_pressed()) {
                ESP_LOGI(TAG, "Tank position check confirmed");
                start_check_stage(SAFETY_START_CHECK);
            }
            return ESP_ERR_INVALID_STATE;  // Still in progress

        case SAFETY_START_CHECK:
            if (button_pressed()) {
                ESP_LOGI(TAG, "Final start confirmation received");
                g_system_state.safety_state = SAFETY_COMPLETE;
                ESP_LOGI(TAG, "All safety checks passed!");
                return ESP_OK;  // All checks complete
            }
            return ESP_ERR_INVALID_STATE;  // Still in progress

        case SAFETY_COMPLETE:
            // Already complete
            return ESP_OK;

        case SAFETY_TIMEOUT:
        case SAFETY_CANCELLED:
            // Failed states
            return ESP_FAIL;

        default:
            ESP_LOGE(TAG, "Invalid safety state: %d", g_system_state.safety_state);
            return ESP_FAIL;
    }
}

void safety_cancel(void)
{
    ESP_LOGW(TAG, "Safety check sequence cancelled by user");
    g_system_state.safety_state = SAFETY_CANCELLED;
    s_safety.check_start_time_us = 0;
}

void safety_get_prompt(char *line1, char *line2)
{
    if (line1 == NULL || line2 == NULL) {
        ESP_LOGE(TAG, "NULL pointer passed to safety_get_prompt");
        return;
    }

    // Get current safety state
    safety_state_t state = g_system_state.safety_state;

    // Bounds check
    if (state >= SAFETY_IDLE && state <= SAFETY_CANCELLED) {
        strncpy(line1, s_prompts[state].line1, 16);
        strncpy(line2, s_prompts[state].line2, 16);
        line1[16] = '\0';  // Ensure null termination
        line2[16] = '\0';
    } else {
        strncpy(line1, "ERROR", 16);
        strncpy(line2, "Invalid state", 16);
        line1[16] = '\0';
        line2[16] = '\0';
    }
}
