idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES
        dht11
        esp_driver_gpio
        esp_timer
        freertos
        lcd_hd44780
        log
        ultrasonic
)
