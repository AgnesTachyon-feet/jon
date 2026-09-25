#include <stdbool.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "dht11.h"

#define DHT11_MIN_READ_INTERVAL_US 2000000
#define DHT11_START_LOW_MS 20
#define DHT11_PHASE_TIMEOUT_US 120
#define DHT11_ONE_THRESHOLD_US 50
#define DHT11_RESPONSE_MIN_US 50
#define DHT11_BIT_LOW_MIN_US 30
#define DHT11_BIT_HIGH_MIN_US 10

static dht11_config_t s_config;
static bool s_initialized;
static int64_t s_last_read_us;
static portMUX_TYPE s_timing_lock = portMUX_INITIALIZER_UNLOCKED;

static esp_err_t wait_while_level(int level, int64_t timeout_us, int64_t *duration_us)
{
    const int64_t start_us = esp_timer_get_time();

    while (gpio_get_level(s_config.data_gpio) == level) {
        if (esp_timer_get_time() - start_us >= timeout_us) {
            return ESP_ERR_TIMEOUT;
        }
    }

    if (duration_us != NULL) {
        *duration_us = esp_timer_get_time() - start_us;
    }
    return ESP_OK;
}

static bool duration_is_valid(int64_t duration_us, int64_t minimum_us)
{
    return duration_us >= minimum_us && duration_us < DHT11_PHASE_TIMEOUT_US;
}

esp_err_t dht11_init(const dht11_config_t *config)
{
    if (config == NULL || !GPIO_IS_VALID_OUTPUT_GPIO(config->data_gpio)) {
        return ESP_ERR_INVALID_ARG;
    }

    gpio_config_t io_config = {
        .pin_bit_mask = 1ULL << config->data_gpio,
        .mode = GPIO_MODE_INPUT_OUTPUT_OD,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t ret = gpio_config(&io_config);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = gpio_set_level(config->data_gpio, 1); // Release the open-drain bus.
    if (ret != ESP_OK) {
        return ret;
    }

    s_config = *config;
    s_last_read_us = -DHT11_MIN_READ_INTERVAL_US;
    s_initialized = true;
    return ESP_OK;
}

esp_err_t dht11_read(float *temperature_c, float *humidity_percent)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (temperature_c == NULL || humidity_percent == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    const int64_t now_us = esp_timer_get_time();
    if (now_us - s_last_read_us < DHT11_MIN_READ_INTERVAL_US) {
        return ESP_ERR_INVALID_STATE;
    }
    // Count failed attempts too, so a disconnected/bad sensor is not over-polled.
    s_last_read_us = now_us;

    esp_err_t ret = gpio_set_level(s_config.data_gpio, 0);
    if (ret != ESP_OK) {
        return ret;
    }
    // The extra tick guarantees at least 20 ms even when called just before a
    // tick boundary.
    vTaskDelay(pdMS_TO_TICKS(DHT11_START_LOW_MS) + 1);

    uint8_t bytes[5] = {0};
    portENTER_CRITICAL(&s_timing_lock);

    ret = gpio_set_level(s_config.data_gpio, 1); // Release bus for sensor reply.
    if (ret != ESP_OK) {
        goto timing_done;
    }
    esp_rom_delay_us(30);

    // Sensor response: about 80 us LOW followed by about 80 us HIGH.
    int64_t response_low_us = 0;
    int64_t response_high_us = 0;
    ret = wait_while_level(1, DHT11_PHASE_TIMEOUT_US, NULL);
    if (ret != ESP_OK) goto timing_done;
    ret = wait_while_level(0, DHT11_PHASE_TIMEOUT_US, &response_low_us);
    if (ret != ESP_OK) goto timing_done;
    ret = wait_while_level(1, DHT11_PHASE_TIMEOUT_US, &response_high_us);
    if (ret != ESP_OK) goto timing_done;
    if (!duration_is_valid(response_low_us, DHT11_RESPONSE_MIN_US) ||
        !duration_is_valid(response_high_us, DHT11_RESPONSE_MIN_US)) {
        ret = ESP_ERR_INVALID_RESPONSE;
        goto timing_done;
    }

    for (int bit = 0; bit < 40; bit++) {
        int64_t low_time_us = 0;
        int64_t high_time_us = 0;

        // Every bit starts with an approximately 50 us LOW preamble.
        ret = wait_while_level(0, DHT11_PHASE_TIMEOUT_US, &low_time_us);
        if (ret != ESP_OK) goto timing_done;
        ret = wait_while_level(1, DHT11_PHASE_TIMEOUT_US, &high_time_us);
        if (ret != ESP_OK) goto timing_done;
        if (!duration_is_valid(low_time_us, DHT11_BIT_LOW_MIN_US) ||
            !duration_is_valid(high_time_us, DHT11_BIT_HIGH_MIN_US)) {
            ret = ESP_ERR_INVALID_RESPONSE;
            goto timing_done;
        }

        bytes[bit / 8] <<= 1;
        if (high_time_us > DHT11_ONE_THRESHOLD_US) {
            bytes[bit / 8] |= 1;
        }
    }

timing_done:
    portEXIT_CRITICAL(&s_timing_lock);
    if (ret != ESP_OK) {
        return ret;
    }

    const uint8_t checksum = (uint8_t)(bytes[0] + bytes[1] + bytes[2] + bytes[3]);
    if (checksum != bytes[4]) {
        return ESP_ERR_INVALID_CRC;
    }

    *humidity_percent = (float)bytes[0] + ((float)bytes[1] / 10.0f);
    *temperature_c = (float)(bytes[2] & 0x7F) + ((float)bytes[3] / 10.0f);
    if ((bytes[2] & 0x80) != 0) {
        *temperature_c = -*temperature_c;
    }

    return ESP_OK;
}
