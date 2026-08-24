#ifndef LCD_H
#define LCD_H

#include <stdint.h>

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "esp_err.h"
#include "esp_lcd_types.h"

#define LCD_HOST                    SPI2_HOST

#define LCD_PIN_D0                  GPIO_NUM_4
#define LCD_PIN_D1                  GPIO_NUM_5
#define LCD_PIN_D2                  GPIO_NUM_6
#define LCD_PIN_D3                  GPIO_NUM_7
#define LCD_PIN_SCK                 GPIO_NUM_15
#define LCD_PIN_CS                  GPIO_NUM_16
#define LCD_PIN_BL                  GPIO_NUM_17
#define LCD_PIN_RST                 GPIO_NUM_NC

#define LCD_H_RES                   360
#define LCD_V_RES                   360
#define LCD_BITS_PER_PIXEL          16
#define LCD_FB_BYTES_PER_PIXEL      2
#define LCD_PCLK_HZ                 (80 * 1000 * 1000)
#define LCD_BL_DEFAULT_PERCENT      95
#define LCD_BL_PWM_FREQ_HZ          20000
#define LCD_BL_PWM_RESOLUTION       LEDC_TIMER_10_BIT
#define LCD_BL_PWM_MAX_DUTY         1023

#define LCD_COLOR_RGB565(r, g, b) \
    ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | (((b) & 0xF8) >> 3)))

#define LCD_DRAW_ROWS_MAX           20

esp_err_t lcd_init(void);
esp_err_t lcd_set_backlight(uint8_t percent);
esp_err_t lcd_fill_rect(int x, int y, int w, int h, uint8_t r, uint8_t g, uint8_t b);
esp_err_t lcd_fill_color(uint8_t r, uint8_t g, uint8_t b);
esp_err_t lcd_prepare_1wire(void);
esp_err_t lcd_fill_1wire(uint8_t r, uint8_t g, uint8_t b);
esp_err_t lcd_draw_rgb565_1wire(int y, int rows, const uint16_t *rgb565_be);
esp_err_t lcd_draw_test_pattern(void);
esp_lcd_panel_handle_t lcd_get_panel(void);

#endif
