#ifndef ULTRASONIC_H
#define ULTRASONIC_H

#include "driver/gpio.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    gpio_num_t trigger_gpio;
    gpio_num_t echo_gpio;
} ultrasonic_config_t;

/**
 * @brief Initialize one HC-SR04 ultrasonic sensor.
 *
 * @note HC-SR04 ECHO is normally a 5 V signal. Use a voltage divider or
 *       level shifter between ECHO and the configured ESP32 GPIO.
 */
esp_err_t ultrasonic_init(const ultrasonic_config_t *config);

/**
 * @brief Perform a bounded HC-SR04 measurement.
 *
 * @return ESP_OK on success, ESP_ERR_TIMEOUT if either ECHO edge is absent.
 */
esp_err_t ultrasonic_read_cm(float *distance_cm);

#ifdef __cplusplus
}
#endif

#endif
