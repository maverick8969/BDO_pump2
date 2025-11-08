/**
 * @file display_driver.c
 * @brief LCD1602 I2C display and rotary encoder driver
 *
 * LCD1602 with PCF8574 I2C backpack
 * Rotary encoder with push button
 */

#include "display_driver.h"
#include "config.h"
#include "safety_system.h"
#include "esp_log.h"
#include "driver/i2c.h"
#include "driver/gpio.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "DISPLAY";

/* LCD1602 I2C Commands (PCF8574 backpack) */
#define LCD_BACKLIGHT 0x08
#define LCD_ENABLE    0x04
#define LCD_RW        0x02
#define LCD_RS        0x01

#define LCD_CMD_CLEAR_DISPLAY   0x01
#define LCD_CMD_RETURN_HOME     0x02
#define LCD_CMD_ENTRY_MODE      0x04
#define LCD_CMD_DISPLAY_CONTROL 0x08
#define LCD_CMD_FUNCTION_SET    0x20
#define LCD_CMD_SET_DDRAM_ADDR  0x80

/* Display control flags */
#define LCD_DISPLAY_ON  0x04
#define LCD_CURSOR_OFF  0x00
#define LCD_BLINK_OFF   0x00

/* Function set flags */
#define LCD_4BIT_MODE   0x00
#define LCD_2LINE       0x08
#define LCD_5X8_DOTS    0x00

/* Entry mode flags */
#define LCD_ENTRY_LEFT  0x02
#define LCD_ENTRY_SHIFT_DEC 0x00

/* I2C timeout */
#define I2C_TIMEOUT_MS 1000

/* Encoder state */
static int encoder_position = 0;
static bool encoder_button_pressed = false;

/* I2C LCD state */
static uint8_t lcd_backlight_state = LCD_BACKLIGHT;

/**
 * @brief Send byte to LCD via I2C (low-level)
 */
static esp_err_t lcd_write_byte(uint8_t data)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (LCD_I2C_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, data, true);
    i2c_master_stop(cmd);

    esp_err_t ret = i2c_master_cmd_begin(I2C_NUM_0, cmd, pdMS_TO_TICKS(I2C_TIMEOUT_MS));
    i2c_cmd_link_delete(cmd);

    return ret;
}

/**
 * @brief Send 4-bit nibble to LCD
 */
static esp_err_t lcd_write_nibble(uint8_t nibble, uint8_t mode)
{
    uint8_t data = (nibble & 0xF0) | mode | lcd_backlight_state;

    // Send data with EN high
    lcd_write_byte(data | LCD_ENABLE);
    vTaskDelay(pdMS_TO_TICKS(1));

    // Send data with EN low
    lcd_write_byte(data & ~LCD_ENABLE);
    vTaskDelay(pdMS_TO_TICKS(1));

    return ESP_OK;
}

/**
 * @brief Send byte to LCD (4-bit mode)
 */
static esp_err_t lcd_send_byte(uint8_t data, uint8_t mode)
{
    // Send upper nibble
    lcd_write_nibble(data & 0xF0, mode);

    // Send lower nibble
    lcd_write_nibble((data << 4) & 0xF0, mode);

    return ESP_OK;
}

/**
 * @brief Send command to LCD
 */
static esp_err_t lcd_send_cmd(uint8_t cmd)
{
    return lcd_send_byte(cmd, 0);
}

/**
 * @brief Send data (character) to LCD
 */
static esp_err_t lcd_send_data(uint8_t data)
{
    return lcd_send_byte(data, LCD_RS);
}

/**
 * @brief Clear LCD display
 */
static esp_err_t lcd_clear(void)
{
    lcd_send_cmd(LCD_CMD_CLEAR_DISPLAY);
    vTaskDelay(pdMS_TO_TICKS(2));
    return ESP_OK;
}

/**
 * @brief Set cursor position
 */
static esp_err_t lcd_set_cursor(uint8_t col, uint8_t row)
{
    uint8_t row_offsets[] = {0x00, 0x40};
    if (row > 1) row = 1;
    if (col > 15) col = 15;

    lcd_send_cmd(LCD_CMD_SET_DDRAM_ADDR | (col + row_offsets[row]));
    return ESP_OK;
}

/**
 * @brief Print string to LCD
 */
static esp_err_t lcd_print(const char *str)
{
    while (*str) {
        lcd_send_data((uint8_t)(*str));
        str++;
    }
    return ESP_OK;
}

/**
 * @brief Print string with padding to fill line
 */
static void lcd_print_line(uint8_t row, const char *str)
{
    char buffer[17];
    snprintf(buffer, sizeof(buffer), "%-16s", str);
    lcd_set_cursor(0, row);
    lcd_print(buffer);
}

/**
 * @brief Initialize I2C master
 */
static esp_err_t i2c_master_init(void)
{
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = PIN_LCD_SDA,
        .scl_io_num = PIN_LCD_SCL,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 100000, // 100 kHz
    };

    esp_err_t ret = i2c_param_config(I2C_NUM_0, &conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C param config failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = i2c_driver_install(I2C_NUM_0, conf.mode, 0, 0, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C driver install failed: %s", esp_err_to_name(ret));
        return ret;
    }

    return ESP_OK;
}

/**
 * @brief Initialize LCD1602 in 4-bit mode
 */
static esp_err_t lcd_init_device(void)
{
    vTaskDelay(pdMS_TO_TICKS(50)); // Wait for LCD to power up

    // Initialize in 4-bit mode (sequence from datasheet)
    lcd_write_nibble(0x30, 0);
    vTaskDelay(pdMS_TO_TICKS(5));

    lcd_write_nibble(0x30, 0);
    vTaskDelay(pdMS_TO_TICKS(1));

    lcd_write_nibble(0x30, 0);
    vTaskDelay(pdMS_TO_TICKS(1));

    lcd_write_nibble(0x20, 0); // Set to 4-bit mode
    vTaskDelay(pdMS_TO_TICKS(1));

    // Function set: 4-bit, 2 lines, 5x8 font
    lcd_send_cmd(LCD_CMD_FUNCTION_SET | LCD_4BIT_MODE | LCD_2LINE | LCD_5X8_DOTS);

    // Display control: display on, cursor off, blink off
    lcd_send_cmd(LCD_CMD_DISPLAY_CONTROL | LCD_DISPLAY_ON | LCD_CURSOR_OFF | LCD_BLINK_OFF);

    // Clear display
    lcd_clear();

    // Entry mode: left to right, no shift
    lcd_send_cmd(LCD_CMD_ENTRY_MODE | LCD_ENTRY_LEFT | LCD_ENTRY_SHIFT_DEC);

    return ESP_OK;
}

/**
 * @brief Initialize rotary encoder GPIOs
 */
static esp_err_t encoder_init(void)
{
    // Configure encoder CLK and DT pins
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << PIN_ENCODER_CLK) | (1ULL << PIN_ENCODER_DT),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf);

    // SW pin is configured by safety_system
    // (shared button for safety checks)

    return ESP_OK;
}

/**
 * @brief Read rotary encoder state (polling)
 *
 * Returns change in position since last call
 */
static int encoder_read(void)
{
    static uint8_t last_state = 0;
    static int position = 0;

    uint8_t clk = gpio_get_level(PIN_ENCODER_CLK);
    uint8_t dt = gpio_get_level(PIN_ENCODER_DT);
    uint8_t state = (clk << 1) | dt;

    // Simple encoder state machine
    // Full step encoder: 4 states per detent
    if (state != last_state) {
        if ((last_state == 0b00 && state == 0b01) ||
            (last_state == 0b01 && state == 0b11) ||
            (last_state == 0b11 && state == 0b10) ||
            (last_state == 0b10 && state == 0b00)) {
            position++;
        } else if ((last_state == 0b00 && state == 0b10) ||
                   (last_state == 0b10 && state == 0b11) ||
                   (last_state == 0b11 && state == 0b01) ||
                   (last_state == 0b01 && state == 0b00)) {
            position--;
        }
        last_state = state;
    }

    int delta = position / 4; // Convert to full steps
    if (delta != 0) {
        position = 0; // Reset position after reporting
    }

    return delta;
}

/**
 * @brief Initialize display and encoder
 */
esp_err_t display_init(void)
{
    ESP_LOGI(TAG, "Initializing LCD1602 display and rotary encoder");

    // Initialize I2C
    esp_err_t ret = i2c_master_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C initialization failed");
        return ret;
    }

    // Initialize LCD
    ret = lcd_init_device();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "LCD initialization failed");
        return ret;
    }

    // Initialize rotary encoder
    ret = encoder_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Encoder initialization failed");
        return ret;
    }

    // Display startup message
    lcd_clear();
    lcd_print_line(0, "BDO Pump v1.0");
    lcd_print_line(1, "Initializing...");

    ESP_LOGI(TAG, "Display initialized successfully");
    return ESP_OK;
}

/**
 * @brief Update display with current system state
 */
esp_err_t display_update(system_state_t *state)
{
    if (state == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    char line1[17];
    char line2[17];

    // Display different screens based on system state
    switch (state->state) {
        case STATE_IDLE:
            snprintf(line1, sizeof(line1), "IDLE  Target:%3.0f", state->target_weight_lbs);
            snprintf(line2, sizeof(line2), "Weight: %6.1f", state->current_weight_lbs);
            break;

        case STATE_SAFETY_CHECK:
            // Get safety check prompt
            safety_get_prompt(line1, line2);
            break;

        case STATE_FILLING:
            {
                float progress = (state->current_weight_lbs / state->target_weight_lbs) * 100.0f;
                if (progress > 100.0f) progress = 100.0f;

                snprintf(line1, sizeof(line1), "FILL %s%3.0f%%",
                         zone_to_string(state->active_zone), progress);
                snprintf(line2, sizeof(line2), "%6.1f/%3.0f P:%2.0f%%",
                         state->current_weight_lbs,
                         state->target_weight_lbs,
                         state->pressure_setpoint_pct);
            }
            break;

        case STATE_COMPLETED:
            snprintf(line1, sizeof(line1), "COMPLETE!");
            snprintf(line2, sizeof(line2), "Filled: %6.1f", state->current_weight_lbs);
            break;

        case STATE_ERROR:
            snprintf(line1, sizeof(line1), "ERROR!");
            snprintf(line2, sizeof(line2), "%s", error_to_string(state->error));
            break;

        case STATE_CANCELLED:
            snprintf(line1, sizeof(line1), "CANCELLED");
            snprintf(line2, sizeof(line2), "Press to reset");
            break;

        default:
            snprintf(line1, sizeof(line1), "UNKNOWN STATE");
            snprintf(line2, sizeof(line2), "Code: %d", state->state);
            break;
    }

    lcd_print_line(0, line1);
    lcd_print_line(1, line2);

    return ESP_OK;
}

/**
 * @brief Handle rotary encoder input
 *
 * Updates target weight when in IDLE state
 */
esp_err_t display_handle_encoder(void)
{
    extern system_state_t g_system_state;

    // Read encoder position change
    int delta = encoder_read();

    if (delta != 0 && g_system_state.state == STATE_IDLE) {
        // Adjust target weight
        float new_target = g_system_state.target_weight_lbs + (delta * WEIGHT_INCREMENT_LBS);

        // Clamp to valid range
        if (new_target < MIN_TARGET_WEIGHT_LBS) {
            new_target = MIN_TARGET_WEIGHT_LBS;
        } else if (new_target > MAX_TARGET_WEIGHT_LBS) {
            new_target = MAX_TARGET_WEIGHT_LBS;
        }

        g_system_state.target_weight_lbs = new_target;

        ESP_LOGI(TAG, "Target weight adjusted to %.1f lbs", new_target);
    }

    return ESP_OK;
}
