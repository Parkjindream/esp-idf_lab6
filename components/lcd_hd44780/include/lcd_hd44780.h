#ifndef LCD_HD44780_H
#define LCD_HD44780_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "driver/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    LCD_BUS_MODE_4BIT = 0, /*!< 4-bit parallel bus (D4-D7) */
    LCD_BUS_MODE_8BIT = 1  /*!< 8-bit parallel bus (D0-D7) */
} lcd_bus_mode_t;

/**
 * @brief Opaque handle to an LCD instance
 */
typedef struct lcd_hd44780_t *lcd_handle_t;

/**
 * @brief LCD device configuration
 */
typedef struct {
    lcd_bus_mode_t bus_mode;  /*!< Bus mode */
    gpio_num_t rs_io;         /*!< RS pin */
    gpio_num_t rw_io;         /*!< RW pin (GPIO_NUM_NC if tied to GND) */
    gpio_num_t en_io;         /*!< EN pin */
    gpio_num_t data_ios[8];   /*!< Data pins (4-bit: [0..3] = D4..D7, 8-bit: [0..7] = D0..D7) */
} lcd_config_t;

// API prototypes (as specified in the lab sheet)
esp_err_t lcd_new_device(const lcd_config_t *config, lcd_handle_t *ret_handle);
esp_err_t lcd_del_device(lcd_handle_t handle);
esp_err_t lcd_send_cmd(lcd_handle_t handle, uint8_t cmd);
esp_err_t lcd_send_data(lcd_handle_t handle, uint8_t data);
esp_err_t lcd_set_cursor(lcd_handle_t handle, uint8_t line, uint8_t column);
esp_err_t lcd_write_string(lcd_handle_t handle, const char *str);
esp_err_t lcd_clear(lcd_handle_t handle);
esp_err_t lcd_create_custom_char(lcd_handle_t handle, uint8_t location, const uint8_t charmap[8]);
esp_err_t lcd_init_bargraph_glyphs(lcd_handle_t handle);
esp_err_t lcd_draw_bargraph(lcd_handle_t handle, uint8_t row, uint8_t start_col, uint8_t max_cols, uint8_t percentage);

// Extension: set cursor + write string under ONE mutex hold (atomic across tasks)
esp_err_t lcd_write_string_at(lcd_handle_t handle, uint8_t line, uint8_t column, const char *str);

#ifdef __cplusplus
}
#endif

#endif // LCD_HD44780_H
