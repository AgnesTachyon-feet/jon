#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_log.h"
#include "dht11.h"
#include "lcd_hd44780.h"
#include "ultrasonic.h"

static const char *TAG = "sensor_display";

static void make_lcd_line(char line[17], const char *text)
{
    memset(line, ' ', 16);
    line[16] = '\0';

    size_t length = strlen(text);
    if (length > 16) {
        length = 16;
    }
    memcpy(line, text, length);
}

static void update_lcd_line(lcd_handle_t lcd, uint8_t row, const char line[17])
{
    esp_err_t ret = lcd_set_cursor(lcd, row, 0);
    if (ret == ESP_OK) {
        ret = lcd_write_string(lcd, line);
    }
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "LCD row %u update failed: %s", row, esp_err_to_name(ret));
    }
}

void app_main(void)
{
    lcd_config_t lcd_config = {
        .bus_mode = LCD_BUS_MODE_4BIT,
        .rs_io = GPIO_NUM_16,
        .rw_io = GPIO_NUM_NC,
        .en_io = GPIO_NUM_17,
        .data_ios = {
            GPIO_NUM_23,
            GPIO_NUM_25,
            GPIO_NUM_26,
            GPIO_NUM_27
        }
    };
    ultrasonic_config_t ultrasonic_config = {
        .trigger_gpio = GPIO_NUM_18,
        // HC-SR04 ECHO is usually 5 V. Use a divider/level shifter to GPIO19.
        .echo_gpio = GPIO_NUM_19,
    };
    dht11_config_t dht11_config = {
        .data_gpio = GPIO_NUM_21,
    };

    lcd_handle_t lcd = NULL;
    ESP_ERROR_CHECK(lcd_new_device(&lcd_config, &lcd));

    esp_err_t ultrasonic_init_result = ultrasonic_init(&ultrasonic_config);
    if (ultrasonic_init_result != ESP_OK) {
        ESP_LOGE(TAG, "Ultrasonic initialization failed: %s",
                 esp_err_to_name(ultrasonic_init_result));
    }

    esp_err_t dht11_init_result = dht11_init(&dht11_config);
    if (dht11_init_result != ESP_OK) {
        ESP_LOGE(TAG, "DHT11 initialization failed: %s",
                 esp_err_to_name(dht11_init_result));
    }

    while (1) {
        char line1[17];
        char line2[17];
        char value_text[24];
        float distance_cm = 0.0f;
        float temperature_c = 0.0f;
        float humidity_percent = 0.0f;

        esp_err_t ret = ultrasonic_init_result;
        if (ret == ESP_OK) {
            ret = ultrasonic_read_cm(&distance_cm);
        }
        if (ret == ESP_OK) {
            snprintf(value_text, sizeof(value_text), "D: %.1f cm", distance_cm);
            make_lcd_line(line1, value_text);
            ESP_LOGI(TAG, "Ultrasonic: %.1f cm", distance_cm);
        } else {
            make_lcd_line(line1, "D: ERROR");
            ESP_LOGW(TAG, "Ultrasonic read failed: %s", esp_err_to_name(ret));
        }

        ret = dht11_init_result;
        if (ret == ESP_OK) {
            ret = dht11_read(&temperature_c, &humidity_percent);
        }
        if (ret == ESP_OK) {
            snprintf(value_text, sizeof(value_text), "T:%.0fC H:%.0f%%",
                     temperature_c, humidity_percent);
            make_lcd_line(line2, value_text);
            ESP_LOGI(TAG, "Temperature: %.1f C, Humidity: %.1f %%",
                     temperature_c, humidity_percent);
        } else {
            make_lcd_line(line2, "T:ERR H:ERR");
            ESP_LOGW(TAG, "DHT11 read failed: %s", esp_err_to_name(ret));
        }

        update_lcd_line(lcd, 0, line1);
        update_lcd_line(lcd, 1, line2);

        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}
