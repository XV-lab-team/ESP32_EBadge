#ifndef __ESIO_H_
#define __ESIO_H_

#include "aw9523.h"

typedef struct {
	AW9523_PIN      pin;
	AW9523_PIN_MODE mode;
	uint8_t         val_default;
	uint32_t        chip_num;
	const char     *desc;
} exio_gpio_pin_t;

typedef struct {
	AW9523_PIN  pin;
	uint8_t     val_default;
	uint32_t    chip_num;
	const char *desc;
} exio_led_pin_t;

typedef struct exio_t {
	aw9523_dev      aw9523_dev;
	exio_gpio_pin_t exio_gpio_pin[64];
	exio_led_pin_t  exio_led_pin[64];
} exio_t;

HAL_StatusTypeDef exio_init(exio_t *dev);
HAL_StatusTypeDef exio_apply(exio_t *dev);

#endif
