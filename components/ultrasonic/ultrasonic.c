#include <stdbool.h>
#include <stdint.h>

#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "ultrasonic.h"

#define ULTRASONIC_EDGE_TIMEOUT_US 30000
#define ULTRASONIC_TRIGGER_PULSE_US 10

static ultrasonic_config_t s_config;
static bool s_initialized;

static esp_err_t wait_for_level(int level, int64_t timeout_us)
{
    const int64_t deadline = esp_timer_get_time() + timeout_us;

    while (gpio_get_level(s_config.echo_gpio) != level) {
        if (esp_timer_get_time() >= deadline) {
            return ESP_ERR_TIMEOUT;
        }
    }

    return ESP_OK;
}

esp_err_t ultrasonic_init(const ultrasonic_config_t *config)
{
    if (config == NULL ||
        !GPIO_IS_VALID_OUTPUT_GPIO(config->trigger_gpio) ||
        !GPIO_IS_VALID_GPIO(config->echo_gpio) ||
        config->trigger_gpio == config->echo_gpio) {
        return ESP_ERR_INVALID_ARG;
    }

    gpio_config_t trigger_config = {
        .pin_bit_mask = 1ULL << config->trigger_gpio,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t ret = gpio_config(&trigger_config);
    if (ret != ESP_OK) {
        return ret;
    }

    gpio_config_t echo_config = {
        .pin_bit_mask = 1ULL << config->echo_gpio,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ret = gpio_config(&echo_config);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = gpio_set_level(config->trigger_gpio, 0);
    if (ret != ESP_OK) {
        return ret;
    }

    s_config = *config;
    s_initialized = true;
    return ESP_OK;
}

esp_err_t ultrasonic_read_cm(float *distance_cm)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (distance_cm == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    // Reject a stuck-high/stale ECHO before issuing a new trigger pulse.
    esp_err_t ret = wait_for_level(0, ULTRASONIC_EDGE_TIMEOUT_US);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = gpio_set_level(s_config.trigger_gpio, 0);
    if (ret != ESP_OK) {
        return ret;
    }
    esp_rom_delay_us(2);

    ret = gpio_set_level(s_config.trigger_gpio, 1);
    if (ret != ESP_OK) {
        return ret;
    }
    esp_rom_delay_us(ULTRASONIC_TRIGGER_PULSE_US);

    ret = gpio_set_level(s_config.trigger_gpio, 0);
    if (ret != ESP_OK) {
        return ret;
    }

    // These timing loops are deliberately bounded; neither missing edge can
    // hold the application forever.
    ret = wait_for_level(1, ULTRASONIC_EDGE_TIMEOUT_US);
    if (ret != ESP_OK) {
        return ret;
    }

    const int64_t echo_start_us = esp_timer_get_time();
    ret = wait_for_level(0, ULTRASONIC_EDGE_TIMEOUT_US);
    if (ret != ESP_OK) {
        return ret;
    }

    const int64_t echo_time_us = esp_timer_get_time() - echo_start_us;
    *distance_cm = (float)echo_time_us / 58.0f;
    return ESP_OK;
}
