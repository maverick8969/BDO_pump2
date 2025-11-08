/**
 * @file mqtt_client_app.c
 * @brief MQTT client implementation for factory integration
 *
 * Publishes telemetry data to MQTT broker for Telegraf/TimescaleDB/Grafana pipeline
 */

#include "mqtt_client_app.h"
#include "config.h"
#include "esp_log.h"
#include "mqtt_client.h"
#include "cJSON.h"
#include "esp_timer.h"
#include <string.h>

static const char *TAG = "MQTT";

static esp_mqtt_client_handle_t mqtt_client = NULL;
static bool mqtt_connected = false;

/* External reference to global state */
extern system_state_t g_system_state;

/**
 * @brief MQTT event handler
 */
static void mqtt_event_handler(void *handler_args, esp_event_base_t base,
                               int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;

    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT connected to broker");
            mqtt_connected = true;
            g_system_state.mqtt_connected = true;

            // Subscribe to command topics if needed
            // esp_mqtt_client_subscribe(mqtt_client, "factory/pump/cmd/#", 0);
            break;

        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "MQTT disconnected from broker");
            mqtt_connected = false;
            g_system_state.mqtt_connected = false;
            break;

        case MQTT_EVENT_PUBLISHED:
            ESP_LOGD(TAG, "MQTT message published, msg_id=%d", event->msg_id);
            break;

        case MQTT_EVENT_DATA:
            ESP_LOGI(TAG, "MQTT data received: topic=%.*s, data=%.*s",
                     event->topic_len, event->topic,
                     event->data_len, event->data);
            // Handle incoming commands here if needed
            break;

        case MQTT_EVENT_ERROR:
            ESP_LOGE(TAG, "MQTT error event");
            if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
                ESP_LOGE(TAG, "Transport error: 0x%x", event->error_handle->esp_transport_sock_errno);
            }
            break;

        default:
            ESP_LOGD(TAG, "MQTT event: %d", event_id);
            break;
    }
}

/**
 * @brief Start MQTT client
 */
esp_err_t mqtt_app_start(void)
{
    ESP_LOGI(TAG, "Starting MQTT client");
    ESP_LOGI(TAG, "Broker URI: %s", MQTT_BROKER_URI);
    ESP_LOGI(TAG, "Device ID: %s", MQTT_DEVICE_ID);

    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = MQTT_BROKER_URI,
        .credentials.client_id = MQTT_DEVICE_ID,
    };

    // Set username and password if configured
    if (strlen(MQTT_USERNAME) > 0) {
        mqtt_cfg.credentials.username = MQTT_USERNAME;
        mqtt_cfg.credentials.authentication.password = MQTT_PASSWORD;
    }

    mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    if (mqtt_client == NULL) {
        ESP_LOGE(TAG, "Failed to initialize MQTT client");
        return ESP_FAIL;
    }

    // Register event handler
    esp_err_t ret = esp_mqtt_client_register_event(mqtt_client,
                                                     ESP_EVENT_ANY_ID,
                                                     mqtt_event_handler,
                                                     NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register MQTT event handler: %s", esp_err_to_name(ret));
        return ret;
    }

    // Start MQTT client
    ret = esp_mqtt_client_start(mqtt_client);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start MQTT client: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "MQTT client started successfully");
    return ESP_OK;
}

/**
 * @brief Publish system status to MQTT
 *
 * Publishes current system state as JSON to status topic
 */
esp_err_t mqtt_publish_status(system_state_t *state)
{
    if (!mqtt_connected || mqtt_client == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    if (state == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    // Create JSON object
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        ESP_LOGE(TAG, "Failed to create JSON object");
        return ESP_ERR_NO_MEM;
    }

    // Add fields
    cJSON_AddStringToObject(root, "device_id", MQTT_DEVICE_ID);
    cJSON_AddNumberToObject(root, "timestamp", esp_timer_get_time() / 1000); // milliseconds
    cJSON_AddStringToObject(root, "state", state_to_string(state->state));
    cJSON_AddStringToObject(root, "zone", zone_to_string(state->active_zone));
    cJSON_AddNumberToObject(root, "current_weight_lbs", state->current_weight_lbs);
    cJSON_AddNumberToObject(root, "target_weight_lbs", state->target_weight_lbs);
    cJSON_AddNumberToObject(root, "pressure_pct", state->pressure_setpoint_pct);
    cJSON_AddNumberToObject(root, "fill_number", state->fill_number);
    cJSON_AddNumberToObject(root, "fills_today", state->fills_today);
    cJSON_AddNumberToObject(root, "total_lbs_today", state->total_lbs_today);
    cJSON_AddBoolToObject(root, "scale_online", state->scale_online);
    cJSON_AddNumberToObject(root, "uptime_seconds", state->uptime_seconds);

    // Convert to string
    char *json_str = cJSON_PrintUnformatted(root);
    if (json_str == NULL) {
        ESP_LOGE(TAG, "Failed to serialize JSON");
        cJSON_Delete(root);
        return ESP_ERR_NO_MEM;
    }

    // Publish to MQTT
    int msg_id = esp_mqtt_client_publish(mqtt_client,
                                          MQTT_TOPIC_STATUS,
                                          json_str,
                                          0, // length (0 = use strlen)
                                          0, // QoS
                                          0); // retain

    ESP_LOGD(TAG, "Published status: %s", json_str);

    // Cleanup
    free(json_str);
    cJSON_Delete(root);

    if (msg_id < 0) {
        ESP_LOGE(TAG, "Failed to publish MQTT message");
        return ESP_FAIL;
    }

    return ESP_OK;
}

/**
 * @brief Publish fill completion event
 */
esp_err_t mqtt_publish_fill_complete(void)
{
    if (!mqtt_connected || mqtt_client == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    // Create JSON object
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        ESP_LOGE(TAG, "Failed to create JSON object");
        return ESP_ERR_NO_MEM;
    }

    cJSON_AddStringToObject(root, "device_id", MQTT_DEVICE_ID);
    cJSON_AddNumberToObject(root, "timestamp", esp_timer_get_time() / 1000);
    cJSON_AddStringToObject(root, "event", "fill_complete");
    cJSON_AddNumberToObject(root, "fill_number", g_system_state.fill_number);
    cJSON_AddNumberToObject(root, "target_weight_lbs", g_system_state.target_weight_lbs);
    cJSON_AddNumberToObject(root, "actual_weight_lbs", g_system_state.current_weight_lbs);
    cJSON_AddNumberToObject(root, "fill_time_ms", g_system_state.fill_elapsed_ms);

    float error = g_system_state.current_weight_lbs - g_system_state.target_weight_lbs;
    cJSON_AddNumberToObject(root, "error_lbs", error);

    // Convert to string
    char *json_str = cJSON_PrintUnformatted(root);
    if (json_str == NULL) {
        ESP_LOGE(TAG, "Failed to serialize JSON");
        cJSON_Delete(root);
        return ESP_ERR_NO_MEM;
    }

    // Publish to fills topic
    int msg_id = esp_mqtt_client_publish(mqtt_client,
                                          MQTT_TOPIC_FILLS,
                                          json_str,
                                          0, // length
                                          1, // QoS 1 (at least once delivery)
                                          0); // no retain

    ESP_LOGI(TAG, "Fill complete published: fill #%d, %.1f lbs (target: %.1f)",
             g_system_state.fill_number,
             g_system_state.current_weight_lbs,
             g_system_state.target_weight_lbs);

    // Cleanup
    free(json_str);
    cJSON_Delete(root);

    if (msg_id < 0) {
        ESP_LOGE(TAG, "Failed to publish fill complete event");
        return ESP_FAIL;
    }

    return ESP_OK;
}

/**
 * @brief Publish system event
 */
esp_err_t mqtt_publish_event(const char *event, const char *details)
{
    if (!mqtt_connected || mqtt_client == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    if (event == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    // Create JSON object
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        ESP_LOGE(TAG, "Failed to create JSON object");
        return ESP_ERR_NO_MEM;
    }

    cJSON_AddStringToObject(root, "device_id", MQTT_DEVICE_ID);
    cJSON_AddNumberToObject(root, "timestamp", esp_timer_get_time() / 1000);
    cJSON_AddStringToObject(root, "event", event);

    if (details != NULL) {
        cJSON_AddStringToObject(root, "details", details);
    }

    // Convert to string
    char *json_str = cJSON_PrintUnformatted(root);
    if (json_str == NULL) {
        ESP_LOGE(TAG, "Failed to serialize JSON");
        cJSON_Delete(root);
        return ESP_ERR_NO_MEM;
    }

    // Publish to events topic
    int msg_id = esp_mqtt_client_publish(mqtt_client,
                                          MQTT_TOPIC_EVENTS,
                                          json_str,
                                          0,
                                          0,
                                          0);

    ESP_LOGI(TAG, "Event published: %s - %s", event, details ? details : "");

    // Cleanup
    free(json_str);
    cJSON_Delete(root);

    if (msg_id < 0) {
        ESP_LOGE(TAG, "Failed to publish event");
        return ESP_FAIL;
    }

    return ESP_OK;
}
