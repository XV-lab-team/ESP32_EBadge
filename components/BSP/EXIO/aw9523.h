#ifndef __AW9523_H_
#define __AW9523_H_

#include "stdint.h"
#include "aw9523_port.h"

#define AW9523_CHIP_NUM_MAX				4

typedef enum {
	AW9523_PIN_MODE_NULL = 0,
	AW9523_PIN_MODE_IN,
	AW9523_PIN_MODE_OUT,
	AW9523_PIN_MODE_LED,
} AW9523_PIN_MODE;

typedef enum {
	AW9523_PIN_NULL = (uint32_t)0,
	AW9523_PIN_0_0  = (uint32_t)1 << 0,
	AW9523_PIN_0_1  = (uint32_t)1 << 1,
	AW9523_PIN_0_2  = (uint32_t)1 << 2,
	AW9523_PIN_0_3  = (uint32_t)1 << 3,
	AW9523_PIN_0_4  = (uint32_t)1 << 4,
	AW9523_PIN_0_5  = (uint32_t)1 << 5,
	AW9523_PIN_0_6  = (uint32_t)1 << 6,
	AW9523_PIN_0_7  = (uint32_t)1 << 7,
	AW9523_PIN_1_0  = (uint32_t)1 << 8,
	AW9523_PIN_1_1  = (uint32_t)1 << 9,
	AW9523_PIN_1_2  = (uint32_t)1 << 10,
	AW9523_PIN_1_3  = (uint32_t)1 << 11,
	AW9523_PIN_1_4  = (uint32_t)1 << 12,
	AW9523_PIN_1_5  = (uint32_t)1 << 13,
	AW9523_PIN_1_6  = (uint32_t)1 << 14,
	AW9523_PIN_1_7  = (uint32_t)1 << 15,
	AW9523_PIN_ALL  = (uint32_t)0xFFFF,
} AW9523_PIN;

typedef struct {
	uint8_t a1;
	uint8_t a0;

	uint8_t gpio_p0_mod_pp;
	uint8_t ledmode_isel;

	AW9523_PIN_MODE p0_mode[8];
	AW9523_PIN_MODE p1_mode[8];

	uint8_t p0_val[8];
	uint8_t p1_val[8];

	uint8_t p0_led_dim[8];
	uint8_t p1_led_dim[8];
} aw9523_t;

typedef struct {
	I2C_HandleTypeDef *hi2c;
	GPIO_TypeDef      *rst_gpio_port;
	uint16_t           rst_gpio_pin;

	uint32_t           chip_num;
	aw9523_t           chip[AW9523_CHIP_NUM_MAX];
} aw9523_dev;

HAL_StatusTypeDef aw9523_init(aw9523_dev *dev);

HAL_StatusTypeDef aw9523_set_pin_mode(aw9523_dev *dev, uint32_t chip, AW9523_PIN pin, AW9523_PIN_MODE mode);
HAL_StatusTypeDef aw9523_set_pin_gpioval(aw9523_dev *dev, uint32_t chip, AW9523_PIN pin, uint8_t val);
HAL_StatusTypeDef aw9523_set_pin_ledval(aw9523_dev *dev, uint32_t chip, AW9523_PIN pin, uint8_t val);

HAL_StatusTypeDef aw9523_apply_config(aw9523_dev *dev);
HAL_StatusTypeDef aw9523_apply_output(aw9523_dev *dev);

#endif
