#ifndef __IO_VIRTUAL_H_
#define __IO_VIRTUAL_H_

#include "aw9523_port.h"

/*
 * Logical IDs MUST match io_virtual.c table order:
 *   vio_gpio_id_t index == exio_gpio_pin_cfg[] index
 *   vio_led_id_t  index == exio_led_pin_cfg[] index
 *
 * This board: one AW9523 (AD1=1, AD0=1), five common-anode RGB LEDs.
 * P1.5 is unused. There are no GPIO expander outputs.
 */

typedef enum {
    VIO_GPIO_NUM = 0
} vio_gpio_id_t;

typedef enum {
    VIO_LED_R1 = 0, /* P1.7 */
    VIO_LED_G1,     /* P1.6 */
    VIO_LED_B1,     /* P1.4 */
    VIO_LED_R2,     /* P1.3 */
    VIO_LED_G2,     /* P0.0 */
    VIO_LED_B2,     /* P0.3 */
    VIO_LED_R3,     /* P0.1 */
    VIO_LED_G3,     /* P0.2 */
    VIO_LED_B3,     /* P0.4 */
    VIO_LED_R4,     /* P0.6 */
    VIO_LED_G4,     /* P0.5 */
    VIO_LED_B4,     /* P0.7 */
    VIO_LED_R5,     /* P1.0 */
    VIO_LED_G5,     /* P1.1 */
    VIO_LED_B5,     /* P1.2 */
    VIO_LED_NUM
} vio_led_id_t;

esp_err_t io_virtual_start(void);

HAL_StatusTypeDef io_virtual_gpio_set(vio_gpio_id_t id, uint8_t level);
HAL_StatusTypeDef io_virtual_led_set(vio_led_id_t id, uint8_t brightness);
HAL_StatusTypeDef io_virtual_rgb_set(uint8_t rgb_1_to_5, uint8_t r, uint8_t g, uint8_t b);

static inline HAL_StatusTypeDef io_virtual_gpio_high(vio_gpio_id_t id)
{
    return io_virtual_gpio_set(id, 1);
}

static inline HAL_StatusTypeDef io_virtual_gpio_low(vio_gpio_id_t id)
{
    return io_virtual_gpio_set(id, 0);
}

#endif
