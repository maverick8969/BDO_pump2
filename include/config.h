/**
 * @file config.h
 * @brief System configuration and pin definitions
 */

#ifndef CONFIG_H
#define CONFIG_H

#include <stdint.h>
#include <stdbool.h>

/* =============================================================================
 * FIRMWARE VERSION
 * ===========================================================================*/
#define FIRMWARE_VERSION "1.0.0"

/* =============================================================================
 * WIFI CONFIGURATION
 * ===========================================================================*/
#define WIFI_SSID "YOUR_WIFI_SSID"
#define WIFI_PASSWORD "YOUR_WIFI_PASSWORD"

/* =============================================================================
 * MQTT CONFIGURATION
 * ===========================================================================*/
#define MQTT_BROKER_URI "mqtt://192.168.1.100:1883"
#define MQTT_DEVICE_ID "bdo_pump_01"
#define MQTT_USERNAME ""  // Leave empty if no auth required
#define MQTT_PASSWORD ""

// MQTT Topics
#define MQTT_TOPIC_FILLS "factory/pump/fills"
#define MQTT_TOPIC_EVENTS "factory/pump/events"
#define MQTT_TOPIC_STATUS "factory/pump/status"

// MQTT Publishing intervals (milliseconds)
#define MQTT_STATUS_INTERVAL_FILLING 5000   // 5 seconds during fill
#define MQTT_STATUS_INTERVAL_IDLE 30000     // 30 seconds when idle

/* =============================================================================
 * NTP TIME SYNCHRONIZATION
 * ===========================================================================*/
#define NTP_SERVER "pool.ntp.org"
#define GMT_OFFSET_SEC -18000          // EST: -5 hours
#define DAYLIGHT_OFFSET_SEC 3600       // DST: +1 hour

/* =============================================================================
 * GPIO PIN DEFINITIONS (ESP32-DevKit)
 * ===========================================================================*/

// DAC Output (0-3.3V, amplified to 0-10V for ITV2030)
#define PIN_DAC_OUTPUT 25  // GPIO25 (DAC1)

// RS232 Scale Communication (PS-IN202)
#define PIN_SCALE_TX 17    // ESP32 TX → Scale RX
#define PIN_SCALE_RX 16    // ESP32 RX → Scale TX
#define SCALE_UART_NUM UART_NUM_2

// LCD 1602 I2C
#define PIN_LCD_SDA 21     // I2C SDA
#define PIN_LCD_SCL 22     // I2C SCL
#define LCD_I2C_ADDR 0x27  // Typical I2C address for PCF8574 backpack

// Rotary Encoder
#define PIN_ENCODER_CLK 32 // Encoder CLK (A)
#define PIN_ENCODER_DT 33  // Encoder DT (B)
#define PIN_ENCODER_SW 34  // Encoder push button

// LED Status Indicator (WS2812 strip - optional)
#define PIN_LED_STRIP 27
#define LED_STRIP_COUNT 30

// ITV2030 PNP Feedback (pressure reached indicator)
#define PIN_ITV_FEEDBACK 26 // NPN/PNP switch output from ITV2030

// Note: Safety interlocks use LCD display + rotary encoder button
// No separate safety buttons required - encoder SW pin (GPIO34) is used

/* =============================================================================
 * SCALE CONFIGURATION (PS-IN202)
 * ===========================================================================*/
#define SCALE_BAUD_RATE 9600
#define SCALE_READ_INTERVAL_MS 100 // Read scale every 100ms

/* =============================================================================
 * FILL CONTROL PARAMETERS
 * ===========================================================================*/

// Zone thresholds (percentage of target weight)
#define ZONE_FAST_END 60.0f        // 0-60% of target
#define ZONE_MODERATE_END 85.0f    // 60-85% of target
#define ZONE_SLOW_END 97.5f        // 85-97.5% of target
#define ZONE_FINE_END 100.0f       // 97.5-100% of target

// Base pressure setpoints for each zone (percentage, 0-100%)
// These are used as base values for hybrid PID control
#define PRESSURE_FAST 33.0f        // ~20 PSI equivalent (aggressive)
#define PRESSURE_MODERATE 66.0f    // ~40 PSI equivalent
#define PRESSURE_SLOW 100.0f       // ~60 PSI equivalent
#define PRESSURE_FINE 83.0f        // ~50 PSI equivalent (prevent overshoot)

// PID adjustment ranges per zone (percentage points, ±)
#define PID_RANGE_FAST 8.0f        // ±5 PSI equivalent (±8%)
#define PID_RANGE_MODERATE 16.0f   // ±10 PSI equivalent (±16%)
#define PID_RANGE_SLOW 13.0f       // ±8 PSI equivalent (±13%)
#define PID_RANGE_FINE 16.0f       // ±10 PSI equivalent (±16%)

// Zone-specific PID gain multipliers (applied to base Kp, Ki, Kd)
#define PID_GAIN_MULT_FAST 1.5f    // Aggressive (fast response)
#define PID_GAIN_MULT_MODERATE 1.0f // Moderate (normal)
#define PID_GAIN_MULT_SLOW 0.7f    // Conservative
#define PID_GAIN_MULT_FINE 0.4f    // Very conservative (prevent overshoot)

// Control loop timing
#define CONTROL_LOOP_INTERVAL_MS 100  // 10 Hz control loop

/* =============================================================================
 * DISPLAY CONFIGURATION
 * ===========================================================================*/
#define DISPLAY_UPDATE_INTERVAL_MS 200  // 5 Hz display update

// Menu system
#define MENU_TIMEOUT_MS 30000  // Return to main screen after 30s inactivity

/* =============================================================================
 * SAFETY SYSTEM CONFIGURATION
 * ===========================================================================*/
#define SAFETY_CHECK_TIMEOUT_MS 30000  // 30 second timeout per check
#define SAFETY_TOTAL_CHECKS 4          // 4-stage safety system

/* =============================================================================
 * WEB SERVER CONFIGURATION
 * ===========================================================================*/
#define WEBSERVER_PORT 80
#define WEBSERVER_MAX_OPEN_SOCKETS 4

/* =============================================================================
 * DAC/AMPLIFIER CONFIGURATION
 * ===========================================================================*/
// LM358 op-amp gain: (R1 + R2) / R2 = (20k + 10k) / 10k = 3.0
// ESP32 DAC: 0-3.3V → Op-amp output: 0-9.9V (close to 10V target)
#define DAC_MAX_VALUE 255         // 8-bit DAC
#define DAC_VREF_MV 3300          // 3.3V reference
#define OPAMP_GAIN 3.0f           // Amplifier gain

/* =============================================================================
 * DEFAULT FILL PARAMETERS
 * ===========================================================================*/
#define DEFAULT_TARGET_WEIGHT_LBS 200.0f
#define MIN_TARGET_WEIGHT_LBS 10.0f
#define MAX_TARGET_WEIGHT_LBS 250.0f
#define WEIGHT_INCREMENT_LBS 5.0f    // Encoder increment

/* =============================================================================
 * PID CONTROL CONFIGURATION
 * ===========================================================================*/
// Default PID parameters (tunable via auto-tune)
#define DEFAULT_PID_KP 2.5f           // Proportional gain
#define DEFAULT_PID_KI 0.5f           // Integral gain
#define DEFAULT_PID_KD 0.1f           // Derivative gain

// PID limits
#define PID_OUTPUT_MIN 0.0f           // Minimum output (%)
#define PID_OUTPUT_MAX 100.0f         // Maximum output (%)
#define PID_INTEGRAL_MIN -50.0f       // Anti-windup minimum
#define PID_INTEGRAL_MAX 50.0f        // Anti-windup maximum

// PID sample time
#define PID_SAMPLE_TIME_MS 100        // Same as control loop (10 Hz)

// Auto-tune configuration
#define AUTOTUNE_TARGET_WEIGHT 50.0f  // 50 lb test fill
#define AUTOTUNE_SETPOINT_PCT 50.0f   // Test at 50% pressure
#define AUTOTUNE_TIMEOUT_MS 120000    // 2 minute timeout
#define AUTOTUNE_MIN_OSCILLATIONS 3   // Minimum oscillations to detect
#define AUTOTUNE_STEP_PERCENT 20.0f   // Step size for relay tuning

// NVS storage keys for PID parameters
#define NVS_NAMESPACE "pid_params"
#define NVS_KEY_KP "kp"
#define NVS_KEY_KI "ki"
#define NVS_KEY_KD "kd"
#define NVS_KEY_TUNED "tuned"         // Flag: 0 = defaults, 1 = auto-tuned

/* =============================================================================
 * POWER SYSTEM (24V)
 * ===========================================================================*/
// ESP32 powered via 5V USB or Vin (7-12V recommended, buck converted from 24V)
// Peripherals: 24V → 12V buck converter → various voltages
// DAC amp: 12V supply
// ITV2030: 24V supply

#endif // CONFIG_H
