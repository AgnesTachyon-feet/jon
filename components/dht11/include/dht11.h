#ifndef DHT11_H
#define DHT11_H

#include "driver/gpio.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    gpio_num_t data_gpio;
} dht11_config_t;

/**
 * @note Use a suitable pull-up (commonly 4.7-10 kOhm) from DATA to 3.3 V.
 */
esp_err_t dht11_init(const dht11_config_t *config);

/**
 * @brief Read and checksum one DHT11 sample.
 *
 * Calls must be spaced at least two seconds apart. ESP_ERR_INVALID_STATE is
 * returned if called sooner, ESP_ERR_TIMEOUT for an invalid/missing waveform,
 * and ESP_ERR_INVALID_CRC for a checksum mismatch.
 */
esp_err_t dht11_read(float *temperature_c, float *humidity_percent);

#ifdef __cplusplus
}
#endif

#endif
