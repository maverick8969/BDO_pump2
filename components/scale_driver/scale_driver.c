/**
 * @file scale_driver.c
 * @brief PS-IN202 Scale driver implementation (RS232 UART)
 *
 * Communicates with PS-IN202 scale via RS232 (UART2).
 * Protocol: 9600 baud, 8N1
 *
 * Expected response format from scale:
 *   "WT:+000.00 LBS\r\n" or similar
 */

#include "scale_driver.h"
#include "config.h"
#include "esp_log.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "SCALE";

#define UART_BUF_SIZE (256)
#define SCALE_RESPONSE_TIMEOUT_MS 100

/**
 * @brief Initialize UART for scale communication
 */
esp_err_t scale_init(void)
{
    ESP_LOGI(TAG, "Initializing PS-IN202 scale driver on UART%d", SCALE_UART_NUM);

    // Configure UART parameters
    uart_config_t uart_config = {
        .baud_rate = SCALE_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    // Install UART driver
    esp_err_t ret = uart_driver_install(SCALE_UART_NUM, UART_BUF_SIZE * 2, 0, 0, NULL, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to install UART driver: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = uart_param_config(SCALE_UART_NUM, &uart_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure UART: %s", esp_err_to_name(ret));
        return ret;
    }

    // Set UART pins
    ret = uart_set_pin(SCALE_UART_NUM, PIN_SCALE_TX, PIN_SCALE_RX,
                       UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set UART pins: %s", esp_err_to_name(ret));
        return ret;
    }

    // Flush UART buffers
    uart_flush(SCALE_UART_NUM);

    ESP_LOGI(TAG, "Scale driver initialized successfully");
    ESP_LOGI(TAG, "  TX: GPIO%d, RX: GPIO%d, Baud: %d",
             PIN_SCALE_TX, PIN_SCALE_RX, SCALE_BAUD_RATE);

    return ESP_OK;
}

/**
 * @brief Parse weight value from scale response string
 *
 * Expected formats:
 *   "WT:+000.00 LBS\r\n"
 *   "+000.00 LBS"
 *   "000.00"
 *
 * @param response Response string from scale
 * @param weight Pointer to store parsed weight
 * @return ESP_OK on success, ESP_FAIL on parse error
 */
static esp_err_t parse_weight_response(const char *response, float *weight)
{
    if (response == NULL || weight == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    // Look for numeric value (handle +/- signs and decimal point)
    const char *ptr = response;

    // Skip to first digit, +, or -
    while (*ptr != '\0' && *ptr != '+' && *ptr != '-' && !(*ptr >= '0' && *ptr <= '9')) {
        ptr++;
    }

    if (*ptr == '\0') {
        ESP_LOGW(TAG, "No numeric value found in response: %s", response);
        return ESP_FAIL;
    }

    // Parse the float value
    char *endptr = NULL;
    float parsed_value = strtof(ptr, &endptr);

    // Check if parsing was successful
    if (endptr == ptr) {
        ESP_LOGW(TAG, "Failed to parse weight value from: %s", response);
        return ESP_FAIL;
    }

    // Sanity check the value (weight should be reasonable)
    if (parsed_value < -10.0f || parsed_value > 500.0f) {
        ESP_LOGW(TAG, "Weight value out of range: %.2f", parsed_value);
        return ESP_FAIL;
    }

    *weight = parsed_value;
    return ESP_OK;
}

/**
 * @brief Read weight from scale
 *
 * Sends request to scale (if needed) and reads response.
 * Some scales continuously output data, others require polling.
 *
 * @param weight Pointer to store weight value (lbs)
 * @return ESP_OK on success, ESP_FAIL on error
 */
esp_err_t scale_read_weight(float *weight)
{
    if (weight == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t rx_buffer[UART_BUF_SIZE];
    int len = 0;

    // Some scales require a command to trigger a reading
    // Common commands: "R\r\n" or "P\r\n" or "W\r\n"
    // If your scale continuously outputs data, you can skip this

    // Option 1: Request weight (uncomment if your scale needs this)
    // const char *cmd = "R\r\n";
    // uart_write_bytes(SCALE_UART_NUM, cmd, strlen(cmd));
    // uart_wait_tx_done(SCALE_UART_NUM, pdMS_TO_TICKS(50));

    // Read response from UART
    len = uart_read_bytes(SCALE_UART_NUM, rx_buffer, UART_BUF_SIZE - 1,
                          pdMS_TO_TICKS(SCALE_RESPONSE_TIMEOUT_MS));

    if (len <= 0) {
        // No data available - this is common if scale is not connected
        return ESP_FAIL;
    }

    // Null terminate the response
    rx_buffer[len] = '\0';

    // Debug log (uncomment for troubleshooting)
    // ESP_LOGD(TAG, "Scale response (%d bytes): %s", len, rx_buffer);

    // Parse the weight value
    esp_err_t ret = parse_weight_response((char *)rx_buffer, weight);

    if (ret == ESP_OK) {
        ESP_LOGD(TAG, "Weight read: %.2f lbs", *weight);
    }

    return ret;
}

/**
 * @brief Tare the scale (zero the weight)
 *
 * Sends tare command to scale (if supported).
 *
 * @return ESP_OK on success
 */
esp_err_t scale_tare(void)
{
    ESP_LOGI(TAG, "Sending tare command to scale");

    // Common tare commands: "T\r\n" or "Z\r\n"
    const char *cmd = "T\r\n";

    int len = uart_write_bytes(SCALE_UART_NUM, cmd, strlen(cmd));
    if (len < 0) {
        ESP_LOGE(TAG, "Failed to send tare command");
        return ESP_FAIL;
    }

    uart_wait_tx_done(SCALE_UART_NUM, pdMS_TO_TICKS(100));

    // Flush any response
    uart_flush_input(SCALE_UART_NUM);

    ESP_LOGI(TAG, "Tare command sent successfully");
    return ESP_OK;
}
