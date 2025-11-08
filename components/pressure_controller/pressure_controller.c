/**
 * @file pressure_controller.c
 * @brief ITV2030 pressure controller with PID control and auto-tuning
 *
 * Features:
 * - DAC output control (0-10V via op-amp)
 * - PID controller with anti-windup
 * - Relay auto-tuning (Ziegler-Nichols method)
 * - NVS storage for PID parameters
 */

#include "pressure_controller.h"
#include "config.h"
#include "system_state.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/dac.h"
#include "driver/gpio.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <math.h>
#include <string.h>

static const char *TAG = "PRESSURE_CTRL";

/* =============================================================================
 * INTERNAL STATE
 * ===========================================================================*/

typedef struct {
    // PID parameters
    float kp;
    float ki;
    float kd;

    // PID state
    float integral;
    float prev_error;
    float prev_measurement;
    uint64_t last_time_us;

    // Current output
    float output_percent;

} pid_state_t;

typedef struct {
    // Auto-tune state
    bool active;
    uint64_t start_time_us;
    float peak_times[10];          // Timestamps of peaks
    float peak_values[10];         // Peak weight values
    uint8_t peak_count;
    bool relay_state;              // true = high, false = low
    float last_weight;
    float relay_output_high;
    float relay_output_low;

    // Results
    float ultimate_gain;           // Ku from relay test
    float ultimate_period;         // Pu from relay test
    float calculated_kp;
    float calculated_ki;
    float calculated_kd;

} autotune_state_t;

static pid_state_t s_pid = {0};
static autotune_state_t s_autotune = {0};

/* External global state */
extern system_state_t g_system_state;

/* =============================================================================
 * DAC CONTROL FUNCTIONS
 * ===========================================================================*/

/**
 * @brief Set DAC output voltage
 * @param percent Pressure percentage (0-100%)
 * @return ESP_OK on success
 */
static esp_err_t set_dac_output(float percent)
{
    // Clamp to valid range
    if (percent < 0.0f) percent = 0.0f;
    if (percent > 100.0f) percent = 100.0f;

    // Convert percentage to DAC value (0-255)
    // DAC output: 0-3.3V, op-amp gain = 3.0, so output = 0-9.9V (close to 0-10V)
    uint8_t dac_value = (uint8_t)((percent / 100.0f) * DAC_MAX_VALUE);

    // Set DAC output on GPIO25 (DAC channel 1)
    esp_err_t ret = dac_output_voltage(DAC_CHANNEL_1, dac_value);

    if (ret == ESP_OK) {
        s_pid.output_percent = percent;
    }

    return ret;
}

/* =============================================================================
 * NVS STORAGE FUNCTIONS
 * ===========================================================================*/

esp_err_t pressure_controller_load_pid_params(void)
{
    nvs_handle_t nvs_handle;
    esp_err_t ret;

    ret = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "NVS not found, using defaults");
        return ESP_ERR_NOT_FOUND;
    }

    // Read PID parameters
    size_t size = sizeof(float);
    ret = nvs_get_blob(nvs_handle, NVS_KEY_KP, &s_pid.kp, &size);
    if (ret != ESP_OK) goto cleanup;

    size = sizeof(float);
    ret = nvs_get_blob(nvs_handle, NVS_KEY_KI, &s_pid.ki, &size);
    if (ret != ESP_OK) goto cleanup;

    size = sizeof(float);
    ret = nvs_get_blob(nvs_handle, NVS_KEY_KD, &s_pid.kd, &size);
    if (ret != ESP_OK) goto cleanup;

    uint8_t tuned = 0;
    ret = nvs_get_u8(nvs_handle, NVS_KEY_TUNED, &tuned);
    if (ret == ESP_OK) {
        g_system_state.pid_tuned = (tuned != 0);
    }

    ESP_LOGI(TAG, "Loaded PID params: Kp=%.3f, Ki=%.3f, Kd=%.3f (tuned=%d)",
             s_pid.kp, s_pid.ki, s_pid.kd, g_system_state.pid_tuned);

cleanup:
    nvs_close(nvs_handle);
    return ret;
}

esp_err_t pressure_controller_save_pid_params(void)
{
    nvs_handle_t nvs_handle;
    esp_err_t ret;

    ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS for writing: %s", esp_err_to_name(ret));
        return ret;
    }

    // Write PID parameters
    ret = nvs_set_blob(nvs_handle, NVS_KEY_KP, &s_pid.kp, sizeof(float));
    if (ret != ESP_OK) goto cleanup;

    ret = nvs_set_blob(nvs_handle, NVS_KEY_KI, &s_pid.ki, sizeof(float));
    if (ret != ESP_OK) goto cleanup;

    ret = nvs_set_blob(nvs_handle, NVS_KEY_KD, &s_pid.kd, sizeof(float));
    if (ret != ESP_OK) goto cleanup;

    ret = nvs_set_u8(nvs_handle, NVS_KEY_TUNED, g_system_state.pid_tuned ? 1 : 0);
    if (ret != ESP_OK) goto cleanup;

    ret = nvs_commit(nvs_handle);

    ESP_LOGI(TAG, "Saved PID params: Kp=%.3f, Ki=%.3f, Kd=%.3f (tuned=%d)",
             s_pid.kp, s_pid.ki, s_pid.kd, g_system_state.pid_tuned);

cleanup:
    nvs_close(nvs_handle);
    return ret;
}

/* =============================================================================
 * PID CONTROLLER FUNCTIONS
 * ===========================================================================*/

esp_err_t pressure_controller_set_pid_params(float kp, float ki, float kd)
{
    s_pid.kp = kp;
    s_pid.ki = ki;
    s_pid.kd = kd;

    // Update global state
    g_system_state.pid_kp = kp;
    g_system_state.pid_ki = ki;
    g_system_state.pid_kd = kd;

    ESP_LOGI(TAG, "PID params updated: Kp=%.3f, Ki=%.3f, Kd=%.3f", kp, ki, kd);
    return ESP_OK;
}

esp_err_t pressure_controller_get_pid_params(float *kp, float *ki, float *kd)
{
    if (kp == NULL || ki == NULL || kd == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    *kp = s_pid.kp;
    *ki = s_pid.ki;
    *kd = s_pid.kd;

    return ESP_OK;
}

void pressure_controller_reset_pid(void)
{
    s_pid.integral = 0.0f;
    s_pid.prev_error = 0.0f;
    s_pid.prev_measurement = 0.0f;
    s_pid.last_time_us = esp_timer_get_time();

    ESP_LOGI(TAG, "PID controller reset");
}

float pressure_controller_compute_pid(float setpoint, float measurement)
{
    uint64_t now_us = esp_timer_get_time();
    float dt = (now_us - s_pid.last_time_us) / 1000000.0f;  // Convert to seconds

    // Handle first call or very small dt
    if (dt <= 0.0f || dt > 1.0f) {
        s_pid.last_time_us = now_us;
        s_pid.prev_measurement = measurement;
        return s_pid.output_percent;  // Return last output
    }

    // Calculate error
    float error = setpoint - measurement;

    // Proportional term
    float p_term = s_pid.kp * error;

    // Integral term with anti-windup
    s_pid.integral += error * dt;

    // Clamp integral to prevent windup
    if (s_pid.integral > PID_INTEGRAL_MAX) {
        s_pid.integral = PID_INTEGRAL_MAX;
    } else if (s_pid.integral < PID_INTEGRAL_MIN) {
        s_pid.integral = PID_INTEGRAL_MIN;
    }

    float i_term = s_pid.ki * s_pid.integral;

    // Derivative term (on measurement to avoid derivative kick)
    float derivative = (measurement - s_pid.prev_measurement) / dt;
    float d_term = -s_pid.kd * derivative;  // Negative because we use derivative of measurement

    // Compute output
    float output = p_term + i_term + d_term;

    // Clamp output
    if (output < PID_OUTPUT_MIN) {
        output = PID_OUTPUT_MIN;
    } else if (output > PID_OUTPUT_MAX) {
        output = PID_OUTPUT_MAX;
    }

    // Update state
    s_pid.prev_error = error;
    s_pid.prev_measurement = measurement;
    s_pid.last_time_us = now_us;
    s_pid.output_percent = output;

    return output;
}

/* =============================================================================
 * AUTO-TUNE FUNCTIONS (Relay Method / Ziegler-Nichols)
 * ===========================================================================*/

esp_err_t pressure_controller_start_autotune(void)
{
    ESP_LOGI(TAG, "Starting auto-tune sequence");

    // Reset autotune state
    memset(&s_autotune, 0, sizeof(autotune_state_t));

    s_autotune.active = true;
    s_autotune.start_time_us = esp_timer_get_time();
    s_autotune.relay_state = true;
    s_autotune.relay_output_high = AUTOTUNE_PRESSURE_CENTER + AUTOTUNE_STEP_PERCENT;
    s_autotune.relay_output_low = AUTOTUNE_PRESSURE_CENTER - AUTOTUNE_STEP_PERCENT;

    // Update system state
    g_system_state.autotune_state = AUTOTUNE_INIT;

    // Reset PID controller
    pressure_controller_reset_pid();

    return ESP_OK;
}

void pressure_controller_cancel_autotune(void)
{
    ESP_LOGW(TAG, "Auto-tune cancelled");
    s_autotune.active = false;
    g_system_state.autotune_state = AUTOTUNE_CANCELLED;
    set_dac_output(0.0f);
}

bool pressure_controller_is_autotuning(void)
{
    return s_autotune.active;
}

esp_err_t pressure_controller_get_autotune_results(float *kp, float *ki, float *kd)
{
    if (kp == NULL || ki == NULL || kd == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (g_system_state.autotune_state != AUTOTUNE_COMPLETE) {
        return ESP_ERR_INVALID_STATE;
    }

    *kp = s_autotune.calculated_kp;
    *ki = s_autotune.calculated_ki;
    *kd = s_autotune.calculated_kd;

    return ESP_OK;
}

/**
 * @brief Detect peak in weight measurement
 * @return true if peak detected
 */
static bool detect_peak(float current_weight)
{
    static float prev_prev_weight = 0.0f;
    bool is_peak = false;

    // Detect local maximum (peak)
    if (s_autotune.last_weight > current_weight &&
        s_autotune.last_weight > prev_prev_weight) {

        // Store peak
        if (s_autotune.peak_count < 10) {
            s_autotune.peak_times[s_autotune.peak_count] = esp_timer_get_time() / 1000000.0f;
            s_autotune.peak_values[s_autotune.peak_count] = s_autotune.last_weight;
            s_autotune.peak_count++;
            is_peak = true;

            ESP_LOGI(TAG, "Peak %d detected: %.2f lbs at %.2f sec",
                     s_autotune.peak_count, s_autotune.last_weight,
                     s_autotune.peak_times[s_autotune.peak_count - 1]);
        }
    }

    prev_prev_weight = s_autotune.last_weight;
    s_autotune.last_weight = current_weight;

    return is_peak;
}

/**
 * @brief Calculate PID parameters from relay test results
 */
static void calculate_pid_params(void)
{
    if (s_autotune.peak_count < AUTOTUNE_MIN_OSCILLATIONS + 1) {
        ESP_LOGE(TAG, "Not enough peaks for auto-tune");
        g_system_state.autotune_state = AUTOTUNE_TIMEOUT;
        return;
    }

    // Calculate average period (time between peaks)
    float total_period = 0.0f;
    int period_count = 0;

    for (int i = 1; i < s_autotune.peak_count; i++) {
        float period = s_autotune.peak_times[i] - s_autotune.peak_times[i-1];
        total_period += period;
        period_count++;
    }

    s_autotune.ultimate_period = total_period / period_count;

    // Calculate amplitude of oscillation
    float max_amplitude = 0.0f;
    for (int i = 0; i < s_autotune.peak_count - 1; i++) {
        float amplitude = fabs(s_autotune.peak_values[i+1] - s_autotune.peak_values[i]);
        if (amplitude > max_amplitude) {
            max_amplitude = amplitude;
        }
    }

    // Calculate ultimate gain (Ku) from relay amplitude
    float relay_amplitude = AUTOTUNE_STEP_PERCENT;
    s_autotune.ultimate_gain = (4.0f * relay_amplitude) / (M_PI * max_amplitude);

    // Ziegler-Nichols PID tuning rules
    s_autotune.calculated_kp = 0.6f * s_autotune.ultimate_gain;
    s_autotune.calculated_ki = 1.2f * s_autotune.ultimate_gain / s_autotune.ultimate_period;
    s_autotune.calculated_kd = 0.075f * s_autotune.ultimate_gain * s_autotune.ultimate_period;

    ESP_LOGI(TAG, "Auto-tune complete:");
    ESP_LOGI(TAG, "  Ku=%.3f, Pu=%.3f sec", s_autotune.ultimate_gain, s_autotune.ultimate_period);
    ESP_LOGI(TAG, "  Calculated Kp=%.3f, Ki=%.3f, Kd=%.3f",
             s_autotune.calculated_kp, s_autotune.calculated_ki, s_autotune.calculated_kd);

    // Update system state
    g_system_state.autotune_kp = s_autotune.calculated_kp;
    g_system_state.autotune_ki = s_autotune.calculated_ki;
    g_system_state.autotune_kd = s_autotune.calculated_kd;
    g_system_state.autotune_state = AUTOTUNE_COMPLETE;
}

esp_err_t pressure_controller_run_autotune(float current_weight)
{
    if (!s_autotune.active) {
        return ESP_ERR_INVALID_STATE;
    }

    uint64_t elapsed_ms = (esp_timer_get_time() - s_autotune.start_time_us) / 1000;

    // Check for timeout
    if (elapsed_ms > AUTOTUNE_TIMEOUT_MS) {
        ESP_LOGE(TAG, "Auto-tune timeout");
        s_autotune.active = false;
        g_system_state.autotune_state = AUTOTUNE_TIMEOUT;
        g_system_state.error = ERROR_AUTOTUNE_TIMEOUT;
        set_dac_output(0.0f);
        return ESP_FAIL;
    }

    switch (g_system_state.autotune_state) {
        case AUTOTUNE_INIT:
            // Initialize test
            ESP_LOGI(TAG, "Auto-tune: Initializing 50lb test fill");
            s_autotune.last_weight = current_weight;
            g_system_state.autotune_state = AUTOTUNE_SETTLING;
            g_system_state.target_weight_lbs = AUTOTUNE_TARGET_WEIGHT;
            set_dac_output(s_autotune.relay_output_high);
            return ESP_ERR_INVALID_STATE;  // Still in progress

        case AUTOTUNE_SETTLING:
            // Wait for system to start responding
            if (current_weight > 5.0f) {  // Wait for some flow
                ESP_LOGI(TAG, "Auto-tune: Starting relay test");
                g_system_state.autotune_state = AUTOTUNE_RELAY_TEST;
            }
            return ESP_ERR_INVALID_STATE;  // Still in progress

        case AUTOTUNE_RELAY_TEST:
            // Relay feedback test
            detect_peak(current_weight);

            // Relay logic: switch output based on weight error
            // Oscillate around mid-point of test fill
            float error = AUTOTUNE_WEIGHT_SETPOINT - current_weight;

            if (error > 0 && !s_autotune.relay_state) {
                // Below setpoint: Switch to high pressure
                s_autotune.relay_state = true;
                set_dac_output(s_autotune.relay_output_high);
            } else if (error < 0 && s_autotune.relay_state) {
                // Above setpoint: Switch to low pressure
                s_autotune.relay_state = false;
                set_dac_output(s_autotune.relay_output_low);
            }

            // Check if we have enough oscillations or reached target
            if (s_autotune.peak_count >= AUTOTUNE_MIN_OSCILLATIONS + 1 ||
                current_weight >= AUTOTUNE_TARGET_WEIGHT) {

                ESP_LOGI(TAG, "Auto-tune: Calculating PID parameters");
                g_system_state.autotune_state = AUTOTUNE_CALCULATING;
                set_dac_output(0.0f);
            }
            return ESP_ERR_INVALID_STATE;  // Still in progress

        case AUTOTUNE_CALCULATING:
            calculate_pid_params();
            s_autotune.active = false;

            if (g_system_state.autotune_state == AUTOTUNE_COMPLETE) {
                return ESP_OK;  // Complete!
            } else {
                return ESP_FAIL;  // Failed to calculate
            }

        default:
            return ESP_ERR_INVALID_STATE;
    }
}

/* =============================================================================
 * PUBLIC API FUNCTIONS
 * ===========================================================================*/

esp_err_t pressure_controller_init(void)
{
    ESP_LOGI(TAG, "Initializing pressure controller");

    // Enable DAC on GPIO25 (DAC channel 1)
    esp_err_t ret = dac_output_enable(DAC_CHANNEL_1);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enable DAC: %s", esp_err_to_name(ret));
        return ret;
    }

    // Configure ITV feedback GPIO (GPIO26)
    gpio_config_t feedback_cfg = {
        .pin_bit_mask = (1ULL << PIN_ITV_FEEDBACK),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };

    ret = gpio_config(&feedback_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure feedback GPIO: %s", esp_err_to_name(ret));
        return ret;
    }

    // Set initial DAC output to 0
    set_dac_output(0.0f);

    // Try to load PID parameters from NVS
    if (pressure_controller_load_pid_params() != ESP_OK) {
        // Use defaults if not found
        ESP_LOGI(TAG, "Using default PID parameters");
        s_pid.kp = DEFAULT_PID_KP;
        s_pid.ki = DEFAULT_PID_KI;
        s_pid.kd = DEFAULT_PID_KD;
        g_system_state.pid_tuned = false;
    }

    // Update global state
    g_system_state.pid_kp = s_pid.kp;
    g_system_state.pid_ki = s_pid.ki;
    g_system_state.pid_kd = s_pid.kd;
    g_system_state.pid_enabled = false;  // Start with zone control
    g_system_state.autotune_state = AUTOTUNE_IDLE;

    // Initialize PID state
    pressure_controller_reset_pid();

    ESP_LOGI(TAG, "Pressure controller initialized (Kp=%.3f, Ki=%.3f, Kd=%.3f)",
             s_pid.kp, s_pid.ki, s_pid.kd);

    return ESP_OK;
}

esp_err_t pressure_controller_set_percent(float percent)
{
    return set_dac_output(percent);
}

bool pressure_controller_get_feedback(void)
{
    // Read PNP feedback from ITV2030
    return (gpio_get_level(PIN_ITV_FEEDBACK) == 1);
}

/* =============================================================================
 * HYBRID ZONE/PID CONTROL
 * ===========================================================================*/

/**
 * @brief Get zone-specific PID gain multiplier
 * @param zone Current fill zone
 * @return Gain multiplier for this zone
 */
static float get_zone_gain_multiplier(fill_zone_t zone)
{
    switch (zone) {
        case ZONE_FAST:     return PID_GAIN_MULT_FAST;
        case ZONE_MODERATE: return PID_GAIN_MULT_MODERATE;
        case ZONE_SLOW:     return PID_GAIN_MULT_SLOW;
        case ZONE_FINE:     return PID_GAIN_MULT_FINE;
        default:            return 1.0f;
    }
}

/**
 * @brief Get zone-specific PID output range
 * @param zone Current fill zone
 * @return Max PID adjustment range (±) in percentage points
 */
static float get_zone_pid_range(fill_zone_t zone)
{
    switch (zone) {
        case ZONE_FAST:     return PID_RANGE_FAST;
        case ZONE_MODERATE: return PID_RANGE_MODERATE;
        case ZONE_SLOW:     return PID_RANGE_SLOW;
        case ZONE_FINE:     return PID_RANGE_FINE;
        default:            return 10.0f;
    }
}

esp_err_t pressure_controller_set_hybrid(float zone_setpoint, float current_pressure)
{
    // Get current zone from global state
    fill_zone_t zone = g_system_state.active_zone;

    // Get zone-specific gain multiplier
    float gain_mult = get_zone_gain_multiplier(zone);

    // Apply zone-specific gains temporarily for this computation
    float temp_kp = s_pid.kp * gain_mult;
    float temp_ki = s_pid.ki * gain_mult;
    float temp_kd = s_pid.kd * gain_mult;

    // Calculate error (setpoint - measurement)
    float error = zone_setpoint - current_pressure;

    uint64_t now_us = esp_timer_get_time();
    float dt = (now_us - s_pid.last_time_us) / 1000000.0f;

    // Handle first call or very small dt
    if (dt <= 0.0f || dt > 1.0f) {
        s_pid.last_time_us = now_us;
        s_pid.prev_measurement = current_pressure;
        return set_dac_output(zone_setpoint);  // Use zone setpoint directly
    }

    // Proportional term
    float p_term = temp_kp * error;

    // Integral term with anti-windup
    s_pid.integral += error * dt;

    // Clamp integral based on zone range
    float zone_range = get_zone_pid_range(zone);
    float integral_limit = zone_range / (temp_ki + 0.001f);  // Prevent divide by zero

    if (s_pid.integral > integral_limit) {
        s_pid.integral = integral_limit;
    } else if (s_pid.integral < -integral_limit) {
        s_pid.integral = -integral_limit;
    }

    float i_term = temp_ki * s_pid.integral;

    // Derivative term (on measurement to avoid derivative kick)
    float derivative = (current_pressure - s_pid.prev_measurement) / dt;
    float d_term = -temp_kd * derivative;

    // Compute PID adjustment
    float pid_adjustment = p_term + i_term + d_term;

    // Clamp PID adjustment to zone-specific range
    if (pid_adjustment > zone_range) {
        pid_adjustment = zone_range;
    } else if (pid_adjustment < -zone_range) {
        pid_adjustment = -zone_range;
    }

    // Combine zone setpoint with PID adjustment
    float output = zone_setpoint + pid_adjustment;

    // Clamp to valid output range
    if (output < PID_OUTPUT_MIN) {
        output = PID_OUTPUT_MIN;
    } else if (output > PID_OUTPUT_MAX) {
        output = PID_OUTPUT_MAX;
    }

    // Update state
    s_pid.prev_measurement = current_pressure;
    s_pid.last_time_us = now_us;
    s_pid.output_percent = output;

    // Set DAC output
    return set_dac_output(output);
}

/* =============================================================================
 * FLOW-RATE PID CONTROL
 * ===========================================================================*/

typedef struct {
    float prev_weight;
    uint64_t prev_time_us;
    float filtered_flow;  // Low-pass filtered flow rate
} flow_state_t;

static flow_state_t s_flow = {0};

esp_err_t pressure_controller_set_flow_pid(float target_flow_rate, float current_weight)
{
    uint64_t now_us = esp_timer_get_time();
    float dt = (now_us - s_flow.prev_time_us) / 1000000.0f;

    // Initialize on first call
    if (s_flow.prev_time_us == 0 || dt > 1.0f) {
        s_flow.prev_weight = current_weight;
        s_flow.prev_time_us = now_us;
        s_flow.filtered_flow = 0.0f;
        return ESP_OK;
    }

    // Calculate instantaneous flow rate (lbs/sec)
    float weight_delta = current_weight - s_flow.prev_weight;
    float instant_flow = weight_delta / dt;

    // Low-pass filter flow rate to reduce noise (alpha = 0.3)
    s_flow.filtered_flow = 0.3f * instant_flow + 0.7f * s_flow.filtered_flow;

    // Use standard PID computation with filtered flow
    float output = pressure_controller_compute_pid(target_flow_rate, s_flow.filtered_flow);

    // Update state
    s_flow.prev_weight = current_weight;
    s_flow.prev_time_us = now_us;

    // Set DAC output
    return set_dac_output(output);
}
