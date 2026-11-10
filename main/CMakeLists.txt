idf_component_register(
    SRCS "main.c" "sensors.c"
    INCLUDE_DIRS "."
    REQUIRES lcd_hd44780 esp_adc esp_driver_gpio esp_timer esp_rom freertos log esp_system
)
