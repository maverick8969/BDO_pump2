/**
 * @file main.c
 * @brief BDO Pneumatic Pump Controller - Main Application
 *
 * ESP32-based pneumatic pump controller with WebUI and MQTT integration
 *
 * Features:
 * - 4-stage safety interlock system
 * - Multi-zone speed control (Fast, Moderate, Slow, Fine)
 * - RS232 scale communication (PS-IN202)
 * - 1602 LCD display with rotary encoder menu
 * - WebUI for remote monitoring and control
 * - MQTT integration (Telegraf/TimescaleDB/Grafana)
 * - ITV2030 pressure control (0-10V DAC output)
 * - PNP feedback monitoring
 *
 * @author BDO Pump Project
 * @date 2025-11-06
 * @version 1.0.0
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_event.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_wifi.h"

#include "config.h"
#include "system_state.h"
#include "scale_driver.h"
#include "display_driver.h"
#include "pressure_controller.h"
#include "safety_system.h"
#include "webserver.h"
#include "mqtt_client_app.h"

static const char *TAG = "MAIN";

/* Global system state */
system_state_t g_system_state = {
    .state = STATE_IDLE,
    .target_weight_lbs = 200.0f,
    .current_weight_lbs = 0.0f,
    .pressure_setpoint_pct = 0.0f,
    .fill_number = 0,
    .fills_today = 0,
    .total_lbs_today = 0.0f,
    .uptime_seconds = 0
};

/* Event group for system coordination */
EventGroupHandle_t g_system_events;

/* Task handles */
static TaskHandle_t task_scale = NULL;
static TaskHandle_t task_control = NULL;
static TaskHandle_t task_display = NULL;
static TaskHandle_t task_webserver = NULL;
static TaskHandle_t task_mqtt = NULL;

/**
 * @brief Scale reading task
 *
 * Continuously reads weight from PS-IN202 scale via RS232
 * Updates g_system_state.current_weight_lbs
 */
static void scale_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Scale task started");

    scale_init();

    while (1) {
        float weight = 0.0f;

        if (scale_read_weight(&weight) == ESP_OK) {
            g_system_state.current_weight_lbs = weight;
            g_system_state.scale_online = true;
        } else {
            g_system_state.scale_online = false;
            ESP_LOGW(TAG, "Scale read error");
        }

        vTaskDelay(pdMS_TO_TICKS(SCALE_READ_INTERVAL_MS));
    }
}

/**
 * @brief Main control task
 *
 * Implements fill state machine and multi-zone control
 */
static void control_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Control task started");

    pressure_controller_init();

    TickType_t last_wake_time = xTaskGetTickCount();

    while (1) {
        // Update uptime
        g_system_state.uptime_seconds = esp_timer_get_time() / 1000000;

        // State machine
        switch (g_system_state.state) {
            case STATE_IDLE:
                // Wait for start command
                pressure_controller_set_percent(0.0f);
                break;

            case STATE_SAFETY_CHECK:
                // Safety system handles this
                break;

            case STATE_FILLING:
                // Check if auto-tune is active
                if (pressure_controller_is_autotuning()) {
                    // Run auto-tune state machine
                    esp_err_t result = pressure_controller_run_autotune(g_system_state.current_weight_lbs);

                    if (result == ESP_OK) {
                        // Auto-tune complete
                        ESP_LOGI(TAG, "Auto-tune completed successfully");
                        pressure_controller_set_percent(0.0f);
                        g_system_state.state = STATE_IDLE;

                        // Results are stored in g_system_state.autotune_kp/ki/kd
                        // User can save via menu
                    } else if (result == ESP_FAIL) {
                        // Auto-tune failed
                        ESP_LOGE(TAG, "Auto-tune failed");
                        pressure_controller_set_percent(0.0f);
                        g_system_state.state = STATE_ERROR;
                    }
                    // ESP_ERR_INVALID_STATE means still in progress
                } else {
                    // Normal fill - multi-zone control logic
                    control_task_fill_logic();
                }
                break;

            case STATE_COMPLETED:
                // Hold briefly, then return to idle
                vTaskDelay(pdMS_TO_TICKS(2000));
                g_system_state.state = STATE_IDLE;
                break;

            case STATE_ERROR:
                // Stop pump and wait for manual reset
                pressure_controller_set_percent(0.0f);
                break;

            default:
                break;
        }

        vTaskDelayUntil(&last_wake_time, pdMS_TO_TICKS(CONTROL_LOOP_INTERVAL_MS));
    }
}

/**
 * @brief Fill control logic (hybrid zone/PID or simple zone control)
 */
static void control_task_fill_logic(void)
{
    static float prev_weight = 0.0f;
    static uint64_t prev_time_us = 0;

    float remaining = g_system_state.target_weight_lbs - g_system_state.current_weight_lbs;
    float percent_complete = (g_system_state.current_weight_lbs / g_system_state.target_weight_lbs) * 100.0f;

    // Determine zone based on updated thresholds
    fill_zone_t new_zone;
    float zone_setpoint;

    if (percent_complete < ZONE_FAST_END) {
        // FAST ZONE (0-60%)
        new_zone = ZONE_FAST;
        zone_setpoint = PRESSURE_FAST;
    } else if (percent_complete < ZONE_MODERATE_END) {
        // MODERATE ZONE (60-85%)
        new_zone = ZONE_MODERATE;
        zone_setpoint = PRESSURE_MODERATE;
    } else if (percent_complete < ZONE_SLOW_END) {
        // SLOW ZONE (85-97.5%)
        new_zone = ZONE_SLOW;
        zone_setpoint = PRESSURE_SLOW;
    } else if (percent_complete < ZONE_FINE_END) {
        // FINE ZONE (97.5-100%)
        new_zone = ZONE_FINE;
        zone_setpoint = PRESSURE_FINE;
    } else {
        // COMPLETE
        pressure_controller_set_percent(0.0f);
        g_system_state.state = STATE_COMPLETED;
        g_system_state.fill_number++;
        g_system_state.fills_today++;
        g_system_state.total_lbs_today += g_system_state.current_weight_lbs;

        // Publish fill complete event to MQTT
        mqtt_publish_fill_complete();
        return;
    }

    // Track zone transitions
    if (new_zone != g_system_state.active_zone) {
        g_system_state.zone_transitions++;
        ESP_LOGI(TAG, "Zone transition: %s -> %s",
                 zone_to_string(g_system_state.active_zone),
                 zone_to_string(new_zone));

        // Reset PID on zone change for hybrid mode
        if (g_system_state.pid_enabled) {
            pressure_controller_reset_pid();
        }
    }

    g_system_state.active_zone = new_zone;
    g_system_state.pressure_setpoint_pct = zone_setpoint;

    // Choose control mode
    if (g_system_state.pid_enabled) {
        // HYBRID MODE: Zone setpoint + PID smoothing
        // Use weight error as feedback for PID

        // Calculate ideal weight trajectory for this zone
        // (Simple approach: assume linear fill within each zone)
        uint64_t now_us = esp_timer_get_time();
        float dt = (now_us - prev_time_us) / 1000000.0f;

        if (dt > 0.001f && dt < 1.0f) {
            // Calculate current flow rate
            float weight_delta = g_system_state.current_weight_lbs - prev_weight;
            float flow_rate = weight_delta / dt;  // lbs/sec

            // Estimate ideal flow rate based on zone
            // These are rough estimates - auto-tune will optimize
            float target_flow;
            switch (new_zone) {
                case ZONE_FAST:     target_flow = 3.0f; break;  // Fast fill
                case ZONE_MODERATE: target_flow = 2.0f; break;  // Moderate
                case ZONE_SLOW:     target_flow = 1.0f; break;  // Slow
                case ZONE_FINE:     target_flow = 0.3f; break;  // Very slow/precise
                default:            target_flow = 1.0f; break;
            }

            // Use hybrid control: zone setpoint modulated by flow rate error
            // Convert flow error to pressure adjustment
            float flow_error = target_flow - flow_rate;
            float pressure_adjustment = pressure_controller_compute_pid(target_flow, flow_rate);

            // Apply adjustment to zone setpoint
            float output = zone_setpoint + (pressure_adjustment - zone_setpoint);

            // Clamp to reasonable bounds
            if (output < 0.0f) output = 0.0f;
            if (output > 100.0f) output = 100.0f;

            pressure_controller_set_percent(output);
        } else {
            // First iteration or timeout - use zone setpoint
            pressure_controller_set_percent(zone_setpoint);
        }

        prev_weight = g_system_state.current_weight_lbs;
        prev_time_us = now_us;

    } else {
        // SIMPLE ZONE CONTROL (original behavior)
        pressure_controller_set_percent(zone_setpoint);
    }
}

/**
 * @brief Display task
 *
 * Updates LCD and handles rotary encoder input
 * Also manages safety check sequence with LCD prompts
 */
static void display_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Display task started");

    display_init();
    safety_init();

    while (1) {
        // If in safety check mode, run safety sequence
        if (g_system_state.state == STATE_SAFETY_CHECK) {
            esp_err_t result = safety_run_checks();

            if (result == ESP_OK) {
                // All safety checks passed, proceed to filling
                g_system_state.state = STATE_FILLING;
                g_system_state.fill_start_time_ms = esp_timer_get_time() / 1000;
                mqtt_publish_event("fill_start", "Safety checks passed, fill starting");
            } else if (result == ESP_FAIL) {
                // Safety checks failed or cancelled
                g_system_state.state = STATE_CANCELLED;
                mqtt_publish_event("safety_check_failed", "Safety checks cancelled or timeout");
            }
            // ESP_ERR_INVALID_STATE means checks still in progress
        }

        // Update LCD display based on current state
        display_update(&g_system_state);

        // Handle rotary encoder input
        display_handle_encoder();

        vTaskDelay(pdMS_TO_TICKS(DISPLAY_UPDATE_INTERVAL_MS));
    }
}

/**
 * @brief Web server task
 *
 * Serves WebUI and handles HTTP API requests
 */
static void webserver_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Web server task started");

    webserver_init();

    // Web server runs its own event loop
    vTaskDelete(NULL);
}

/**
 * @brief MQTT client task
 *
 * Maintains MQTT connection and publishes telemetry
 */
static void mqtt_task(void *pvParameters)
{
    ESP_LOGI(TAG, "MQTT task started");

    mqtt_app_start();

    uint32_t last_status_publish = 0;

    while (1) {
        uint32_t now = esp_timer_get_time() / 1000; // milliseconds

        // Publish status at appropriate interval
        uint32_t interval = (g_system_state.state == STATE_FILLING) ?
                           MQTT_STATUS_INTERVAL_FILLING :
                           MQTT_STATUS_INTERVAL_IDLE;

        if (now - last_status_publish >= interval) {
            mqtt_publish_status(&g_system_state);
            last_status_publish = now;
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

/**
 * @brief WiFi initialization
 */
static void wifi_init(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASSWORD,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "WiFi connecting to %s...", WIFI_SSID);
}

/**
 * @brief Main application entry point
 */
void app_main(void)
{
    ESP_LOGI(TAG, "===========================================");
    ESP_LOGI(TAG, " BDO Pneumatic Pump Controller");
    ESP_LOGI(TAG, " Version: %s", FIRMWARE_VERSION);
    ESP_LOGI(TAG, " Build: %s %s", __DATE__, __TIME__);
    ESP_LOGI(TAG, "===========================================");

    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Create event group
    g_system_events = xEventGroupCreate();

    // Initialize WiFi
    wifi_init();

    // Create FreeRTOS tasks
    xTaskCreatePinnedToCore(scale_task, "scale_task", 4096, NULL, 5, &task_scale, 0);
    xTaskCreatePinnedToCore(control_task, "control_task", 4096, NULL, 5, &task_control, 0);
    xTaskCreatePinnedToCore(display_task, "display_task", 4096, NULL, 4, &task_display, 1);
    xTaskCreatePinnedToCore(webserver_task, "webserver_task", 8192, NULL, 3, &task_webserver, 1);
    xTaskCreatePinnedToCore(mqtt_task, "mqtt_task", 6144, NULL, 3, &task_mqtt, 1);

    ESP_LOGI(TAG, "All tasks created successfully");
    ESP_LOGI(TAG, "System initialized and running");
}
