#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "dht11.h"
#include "lcd_hd44780.h"
#include "ultrasonic.h"

#define SENSOR_QUEUE_LENGTH 10
#define ULTRASONIC_INTERVAL_MS 500
#define DHT11_INTERVAL_MS 2000
#define DISPLAY_INTERVAL_MS 500

#define ULTRASONIC_TASK_PRIORITY 5
#define DHT11_TASK_PRIORITY 5
#define DISPLAY_TASK_PRIORITY 4

typedef struct {
    float distance_cm;
    int64_t timestamp_us;
    esp_err_t status;
} ultrasonic_sample_t;

typedef struct {
    float temperature_c;
    float humidity_percent;
    int64_t timestamp_us;
    esp_err_t status;
} dht11_sample_t;

static const char *TAG = "sensor_display";

static QueueHandle_t ultrasonic_queue;
static QueueHandle_t dht11_queue;
static esp_err_t ultrasonic_init_status = ESP_ERR_INVALID_STATE;
static esp_err_t dht11_init_status = ESP_ERR_INVALID_STATE;

static esp_err_t push_ultrasonic_sample(const ultrasonic_sample_t *sample)
{
    if (xQueueSend(ultrasonic_queue, sample, 0) == pdPASS) {
        return ESP_OK;
    }

    ultrasonic_sample_t discarded;
    (void)xQueueReceive(ultrasonic_queue, &discarded, 0);
    return xQueueSend(ultrasonic_queue, sample, 0) == pdPASS ? ESP_OK : ESP_FAIL;
}

static esp_err_t push_dht11_sample(const dht11_sample_t *sample)
{
    if (xQueueSend(dht11_queue, sample, 0) == pdPASS) {
        return ESP_OK;
    }

    dht11_sample_t discarded;
    (void)xQueueReceive(dht11_queue, &discarded, 0);
    return xQueueSend(dht11_queue, sample, 0) == pdPASS ? ESP_OK : ESP_FAIL;
}

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

static void ultrasonic_task(void *pvParameters)
{
    (void)pvParameters;

    while (1) {
        ultrasonic_sample_t sample = {
            .timestamp_us = esp_timer_get_time(),
            .status = ultrasonic_init_status,
        };

        if (sample.status == ESP_OK) {
            sample.status = ultrasonic_read_cm(&sample.distance_cm);
        }

        if (sample.status == ESP_OK) {
            ESP_LOGI("ULTRASONIC", "distance=%.1f cm", sample.distance_cm);
        } else {
            ESP_LOGW("ULTRASONIC", "measurement failed: %s",
                     esp_err_to_name(sample.status));
        }

        if (push_ultrasonic_sample(&sample) != ESP_OK) {
            ESP_LOGE("ULTRASONIC", "failed to store sample in queue");
        }

        vTaskDelay(pdMS_TO_TICKS(ULTRASONIC_INTERVAL_MS));
    }
}

static void dht11_task(void *pvParameters)
{
    (void)pvParameters;

    while (1) {
        dht11_sample_t sample = {
            .timestamp_us = esp_timer_get_time(),
            .status = dht11_init_status,
        };

        if (sample.status == ESP_OK) {
            sample.status = dht11_read(&sample.temperature_c,
                                       &sample.humidity_percent);
        }

        if (sample.status == ESP_OK) {
            ESP_LOGI("DHT11", "temp=%.1f C humidity=%.1f %%",
                     sample.temperature_c, sample.humidity_percent);
        } else {
            ESP_LOGW("DHT11", "measurement failed: %s",
                     esp_err_to_name(sample.status));
        }

        if (push_dht11_sample(&sample) != ESP_OK) {
            ESP_LOGE("DHT11", "failed to store sample in queue");
        }

        vTaskDelay(pdMS_TO_TICKS(DHT11_INTERVAL_MS));
    }
}

static void display_task(void *pvParameters)
{
    lcd_handle_t lcd = (lcd_handle_t)pvParameters;
    ultrasonic_sample_t latest_ultrasonic = {
        .status = ESP_ERR_NOT_FOUND,
    };
    dht11_sample_t latest_dht11 = {
        .status = ESP_ERR_NOT_FOUND,
    };
    bool have_ultrasonic_sample = false;
    bool have_dht11_sample = false;

    while (1) {
        ultrasonic_sample_t ultrasonic_sample;
        while (xQueueReceive(ultrasonic_queue, &ultrasonic_sample, 0) == pdPASS) {
            latest_ultrasonic = ultrasonic_sample;
            have_ultrasonic_sample = true;
        }

        dht11_sample_t dht11_sample;
        while (xQueueReceive(dht11_queue, &dht11_sample, 0) == pdPASS) {
            latest_dht11 = dht11_sample;
            have_dht11_sample = true;
        }

        char line1[17];
        char line2[17];
        char value_text[24];

        if (have_ultrasonic_sample && latest_ultrasonic.status == ESP_OK) {
            snprintf(value_text, sizeof(value_text), "D: %.1f cm",
                     latest_ultrasonic.distance_cm);
            make_lcd_line(line1, value_text);
        } else {
            make_lcd_line(line1, "D: ERROR");
        }

        if (have_dht11_sample && latest_dht11.status == ESP_OK) {
            snprintf(value_text, sizeof(value_text), "T:%.0fC H:%.0f%%",
                     latest_dht11.temperature_c,
                     latest_dht11.humidity_percent);
            make_lcd_line(line2, value_text);
        } else {
            make_lcd_line(line2, "T:ERR H:ERR");
        }

        update_lcd_line(lcd, 0, line1);
        update_lcd_line(lcd, 1, line2);

        vTaskDelay(pdMS_TO_TICKS(DISPLAY_INTERVAL_MS));
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
        /*
         * HC-SR04 ECHO is normally 5 V and must not connect directly to GPIO19.
         * Example divider: ECHO -- 1 kOhm --+-- GPIO19
         *                                    +-- 2 kOhm -- GND
         */
        .echo_gpio = GPIO_NUM_19,
    };
    dht11_config_t dht11_config = {
        .data_gpio = GPIO_NUM_21,
    };

    lcd_handle_t lcd = NULL;
    ESP_ERROR_CHECK(lcd_new_device(&lcd_config, &lcd));

    ultrasonic_init_status = ultrasonic_init(&ultrasonic_config);
    if (ultrasonic_init_status != ESP_OK) {
        ESP_LOGE(TAG, "Ultrasonic initialization failed: %s",
                 esp_err_to_name(ultrasonic_init_status));
    }

    dht11_init_status = dht11_init(&dht11_config);
    if (dht11_init_status != ESP_OK) {
        ESP_LOGE(TAG, "DHT11 initialization failed: %s",
                 esp_err_to_name(dht11_init_status));
    }

    ultrasonic_queue = xQueueCreate(SENSOR_QUEUE_LENGTH,
                                    sizeof(ultrasonic_sample_t));
    dht11_queue = xQueueCreate(SENSOR_QUEUE_LENGTH, sizeof(dht11_sample_t));
    if (ultrasonic_queue == NULL || dht11_queue == NULL) {
        ESP_LOGE(TAG, "Failed to allocate sensor queues");
        if (ultrasonic_queue != NULL) {
            vQueueDelete(ultrasonic_queue);
        }
        if (dht11_queue != NULL) {
            vQueueDelete(dht11_queue);
        }
        return;
    }

    TaskHandle_t ultrasonic_task_handle = NULL;
    TaskHandle_t dht11_task_handle = NULL;
    TaskHandle_t display_task_handle = NULL;

    if (xTaskCreate(ultrasonic_task, "ultrasonic_task", 3072, NULL,
                    ULTRASONIC_TASK_PRIORITY,
                    &ultrasonic_task_handle) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create ultrasonic task");
        goto task_creation_failed;
    }

    if (xTaskCreate(dht11_task, "dht11_task", 3072, NULL,
                    DHT11_TASK_PRIORITY, &dht11_task_handle) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create DHT11 task");
        goto task_creation_failed;
    }

    if (xTaskCreate(display_task, "display_task", 4096, lcd,
                    DISPLAY_TASK_PRIORITY, &display_task_handle) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create display task");
        goto task_creation_failed;
    }

    ESP_LOGI(TAG, "Sensor and display tasks started");
    return;

task_creation_failed:
    if (ultrasonic_task_handle != NULL) {
        vTaskDelete(ultrasonic_task_handle);
    }
    if (dht11_task_handle != NULL) {
        vTaskDelete(dht11_task_handle);
    }
    if (display_task_handle != NULL) {
        vTaskDelete(display_task_handle);
    }
    vQueueDelete(ultrasonic_queue);
    vQueueDelete(dht11_queue);
}
