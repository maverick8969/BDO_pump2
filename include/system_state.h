/**
 * @file system_state.h
 * @brief Global system state definitions
 */

#ifndef SYSTEM_STATE_H
#define SYSTEM_STATE_H

#include <stdint.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

/* =============================================================================
 * SYSTEM STATE MACHINE
 * ===========================================================================*/

typedef enum {
    STATE_IDLE = 0,
    STATE_SAFETY_CHECK,
    STATE_FILLING,
    STATE_COMPLETED,
    STATE_ERROR,
    STATE_CANCELLED
} system_state_enum_t;

/* =============================================================================
 * FILL ZONE DEFINITIONS
 * ===========================================================================*/

typedef enum {
    ZONE_IDLE = 0,
    ZONE_FAST,        // 0-40% of target
    ZONE_MODERATE,    // 40-70% of target
    ZONE_SLOW,        // 70-90% of target
    ZONE_FINE         // 90-98% of target
} fill_zone_t;

/* =============================================================================
 * SAFETY CHECK STATE
 * ===========================================================================*/

typedef enum {
    SAFETY_IDLE = 0,
    SAFETY_AIR_CHECK,
    SAFETY_HOSE_CHECK,
    SAFETY_POSITION_CHECK,
    SAFETY_START_CHECK,
    SAFETY_COMPLETE,
    SAFETY_TIMEOUT,
    SAFETY_CANCELLED
} safety_state_t;

/* =============================================================================
 * ERROR CODES
 * ===========================================================================*/

typedef enum {
    ERROR_NONE = 0,
    ERROR_SCALE_OFFLINE,
    ERROR_SCALE_TIMEOUT,
    ERROR_WEIGHT_STUCK,
    ERROR_PRESSURE_FAULT,
    ERROR_SAFETY_TIMEOUT,
    ERROR_OVERFILL,
    ERROR_WIFI_DISCONNECTED
} error_code_t;

/* =============================================================================
 * SYSTEM STATE STRUCTURE
 * ===========================================================================*/

typedef struct {
    // State machine
    system_state_enum_t state;
    safety_state_t safety_state;
    fill_zone_t active_zone;
    error_code_t error;

    // Fill parameters
    float target_weight_lbs;
    float current_weight_lbs;
    float start_weight_lbs;
    float actual_dispensed_lbs;
    float pressure_setpoint_pct;

    // Fill tracking
    uint32_t fill_number;           // Lifetime fill counter
    uint32_t fills_today;           // Resets at midnight
    float total_lbs_today;          // Resets at midnight
    uint32_t fill_start_time_ms;    // Timestamp when fill started
    uint32_t fill_elapsed_ms;       // Time since fill started
    uint32_t zone_transitions;      // Number of zone changes this fill

    // System status
    bool scale_online;
    bool mqtt_connected;
    bool wifi_connected;
    bool itv_feedback_active;       // PNP switch state from ITV2030
    uint32_t uptime_seconds;

    // Menu/UI state
    uint8_t menu_page;
    uint8_t menu_item;
    bool menu_active;
    uint32_t last_interaction_ms;

    // Statistics (runtime averages)
    float avg_fill_time_ms;
    float avg_error_lbs;
    float avg_pressure_pct;

} system_state_t;

/* =============================================================================
 * EVENT GROUP BITS
 * ===========================================================================*/

#define EVENT_WIFI_CONNECTED (1 << 0)
#define EVENT_MQTT_CONNECTED (1 << 1)
#define EVENT_SCALE_READY (1 << 2)
#define EVENT_FILL_START (1 << 3)
#define EVENT_FILL_COMPLETE (1 << 4)
#define EVENT_SAFETY_COMPLETE (1 << 5)
#define EVENT_ERROR (1 << 6)

/* =============================================================================
 * GLOBAL VARIABLES (extern declarations)
 * ===========================================================================*/

extern system_state_t g_system_state;
extern EventGroupHandle_t g_system_events;

/* =============================================================================
 * HELPER FUNCTIONS
 * ===========================================================================*/

/**
 * @brief Convert state enum to string
 */
static inline const char* state_to_string(system_state_enum_t state)
{
    switch (state) {
        case STATE_IDLE: return "IDLE";
        case STATE_SAFETY_CHECK: return "SAFETY_CHECK";
        case STATE_FILLING: return "FILLING";
        case STATE_COMPLETED: return "COMPLETED";
        case STATE_ERROR: return "ERROR";
        case STATE_CANCELLED: return "CANCELLED";
        default: return "UNKNOWN";
    }
}

/**
 * @brief Convert zone enum to string
 */
static inline const char* zone_to_string(fill_zone_t zone)
{
    switch (zone) {
        case ZONE_IDLE: return "IDLE";
        case ZONE_FAST: return "FAST";
        case ZONE_MODERATE: return "MODERATE";
        case ZONE_SLOW: return "SLOW";
        case ZONE_FINE: return "FINE";
        default: return "UNKNOWN";
    }
}

/**
 * @brief Convert error code to string
 */
static inline const char* error_to_string(error_code_t error)
{
    switch (error) {
        case ERROR_NONE: return "NONE";
        case ERROR_SCALE_OFFLINE: return "SCALE_OFFLINE";
        case ERROR_SCALE_TIMEOUT: return "SCALE_TIMEOUT";
        case ERROR_WEIGHT_STUCK: return "WEIGHT_STUCK";
        case ERROR_PRESSURE_FAULT: return "PRESSURE_FAULT";
        case ERROR_SAFETY_TIMEOUT: return "SAFETY_TIMEOUT";
        case ERROR_OVERFILL: return "OVERFILL";
        case ERROR_WIFI_DISCONNECTED: return "WIFI_DISCONNECTED";
        default: return "UNKNOWN";
    }
}

#endif // SYSTEM_STATE_H
