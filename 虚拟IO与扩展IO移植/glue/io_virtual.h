#ifndef __IO_VIRTUAL_H_
#define __IO_VIRTUAL_H_

#include "main.h"

/*
 * Logical IDs MUST match glue/io_virtual.c table order:
 *   vio_gpio_id_t index == exio_gpio_pin_cfg[] index
 *   vio_led_id_t  index == exio_led_pin_cfg[] index
 *
 * Replace DEMO names with the target board names. Append new IDs before *_NUM.
 * Full trainer-board table: examples/本工程_io_virtual.h and 05_配置表与逻辑编号.md
 */
typedef enum {
	VIO_GPIO_DEMO0 = 0,
	VIO_GPIO_NUM
} vio_gpio_id_t;

typedef enum {
	VIO_LED_DEMO0 = 0,
	VIO_LED_NUM
} vio_led_id_t;

void io_virtual_task(void *arg);

HAL_StatusTypeDef io_virtual_gpio_set(vio_gpio_id_t id, uint8_t level);
HAL_StatusTypeDef io_virtual_led_set(vio_led_id_t id, uint8_t brightness);

static inline HAL_StatusTypeDef io_virtual_gpio_high(vio_gpio_id_t id)
{
	return io_virtual_gpio_set(id, 1);
}

static inline HAL_StatusTypeDef io_virtual_gpio_low(vio_gpio_id_t id)
{
	return io_virtual_gpio_set(id, 0);
}

#endif
