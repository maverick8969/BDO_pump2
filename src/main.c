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
                // Multi-zone control logic
                control_task_fill_logic();
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
 * @brief Fill control logic (multi-zone)
 */
static void control_task_fill_logic(void)
{
    float remaining = g_system_state.target_weight_lbs - g_system_state.current_weight_lbs;
    float percent_complete = (g_system_state.current_weight_lbs / g_system_state.target_weight_lbs) * 100.0f;

    // Determine zone and set pressure
    if (percent_complete < 40.0f) {
        // FAST ZONE (0-40%)
        g_system_state.active_zone = ZONE_FAST;
        pressure_controller_set_percent(100.0f);
    } else if (percent_complete < 70.0f) {
        // MODERATE ZONE (40-70%)
        g_system_state.active_zone = ZONE_MODERATE;
        pressure_controller_set_percent(70.0f);
    } else if (percent_complete < 90.0f) {
        // SLOW ZONE (70-90%)
        g_system_state.active_zone = ZONE_SLOW;
        pressure_controller_set_percent(40.0f);
    } else if (percent_complete < 98.0f) {
        // FINE ZONE (90-98%)
        g_system_state.active_zone = ZONE_FINE;
        pressure_controller_set_percent(20.0f);
    } else {
        // COMPLETE
        pressure_controller_set_percent(0.0f);
        g_system_state.state = STATE_COMPLETED;
        g_system_state.fill_number++;
        g_system_state.fills_today++;
        g_system_state.total_lbs_today += g_system_state.current_weight_lbs;

        // Publish fill complete event to MQTT
        mqtt_publish_fill_complete();
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
