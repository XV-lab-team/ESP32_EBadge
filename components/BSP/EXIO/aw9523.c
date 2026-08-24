#include "aw9523.h"

#define I2C_TIMOUT_MS  (10)

#define AW9523_ADDRESS  (0xB0)

#define INPUT_PORT0           (0x00)
#define INPUT_PORT1           (0x01)
#define OUTPUT_PORT0          (0x02)
#define OUTPUT_PORT1          (0x03)
#define CONFIG_PORT0          (0x04)
#define CONFIG_PORT1          (0x05)
#define INT_PORT0             (0x06)
#define INT_PORT1             (0x07)
#define ID                    (0x10)
#define GCR                   (0x11)
#define LED_MODE_SWITCH_P0    (0x12)
#define LED_MODE_SWITCH_P1    (0x13)
#define DIM0                  (0x20)
#define DIM1                  (0x21)
#define DIM2                  (0x22)
#define DIM3                  (0x23)
#define DIM4                  (0x24)
#define DIM5                  (0x25)
#define DIM6                  (0x26)
#define DIM7                  (0x27)
#define DIM8                  (0x28)
#define DIM9                  (0x29)
#define DIM10                 (0x2A)
#define DIM11                 (0x2B)
#define DIM12                 (0x2C)
#define DIM13                 (0x2D)
#define DIM14                 (0x2E)
#define DIM15                 (0x2F)
#define SOFT_RESET            (0x7F)

static inline HAL_StatusTypeDef aw9523_i2c_transmit(I2C_HandleTypeDef *hi2c, uint16_t DevAddress, uint8_t *pData, uint16_t Size);
static HAL_StatusTypeDef aw9523_SetAllIo_Hiz(aw9523_dev *dev, uint32_t chip);
static HAL_StatusTypeDef aw9523_set_reg(I2C_HandleTypeDef *hi2c, uint16_t DevAddress, uint8_t RegAddress, uint8_t *pData, uint16_t Size);

HAL_StatusTypeDef aw9523_init(aw9523_dev *dev)
{
	if (dev == NULL)
		return HAL_ERROR;
	if (dev->hi2c == NULL)
		return HAL_ERROR;
	if (dev->chip_num > AW9523_CHIP_NUM_MAX)
		return HAL_ERROR;
	if (dev->rst_gpio_port == NULL)
		return HAL_ERROR;

	HAL_StatusTypeDef res = HAL_OK;

	AW9523_MCU_GPIO_OUT_RESET(dev->rst_gpio_port, dev->rst_gpio_pin);
	AW9523_MCU_GPIO_OUT_SET(dev->rst_gpio_port, dev->rst_gpio_pin);

	for (uint32_t num = 0; num < dev->chip_num; num++)
		res |= aw9523_SetAllIo_Hiz(dev, num);

	for (uint32_t num = 0; num < dev->chip_num; num++) {
		uint8_t reg_ctl = 0;
		if (dev->chip[num].gpio_p0_mod_pp)
			reg_ctl |= (0x01 << 4);

		if (dev->chip[num].ledmode_isel != 0) {
			if (dev->chip[num].ledmode_isel > 3)
				dev->chip[num].ledmode_isel = 3;
			reg_ctl |= (dev->chip[num].ledmode_isel & 0x03);

			uint8_t chip_addr = AW9523_ADDRESS;
			if (dev->chip[num].a0)
				chip_addr |= 0x02;
			if (dev->chip[num].a1)
				chip_addr |= 0x04;

			res |= aw9523_set_reg(dev->hi2c, chip_addr, GCR, &reg_ctl, 1);
		}
	}

	return res;
}

HAL_StatusTypeDef aw9523_set_pin_mode(aw9523_dev *dev, uint32_t chip, AW9523_PIN pin, AW9523_PIN_MODE mode)
{
	if (dev == NULL)
		return HAL_ERROR;
	if (mode != AW9523_PIN_MODE_IN && mode != AW9523_PIN_MODE_OUT && mode != AW9523_PIN_MODE_LED)
		return HAL_ERROR;
	if (chip >= dev->chip_num || chip >= AW9523_CHIP_NUM_MAX)
		return HAL_ERROR;

	if (mode == AW9523_PIN_MODE_IN) {
		AW9523_LOG_E("AW9523_PIN_MODE_IN is not implemented");
		return HAL_ERROR;
	}

	if (pin == AW9523_PIN_NULL)
		return HAL_OK;

	for (uint32_t i = 0; i < 16; i++) {
		if ((pin & (0x01 << i)) != 0) {
			switch (0x01 << i) {
			case AW9523_PIN_0_0: dev->chip[chip].p0_mode[0] = mode; break;
			case AW9523_PIN_0_1: dev->chip[chip].p0_mode[1] = mode; break;
			case AW9523_PIN_0_2: dev->chip[chip].p0_mode[2] = mode; break;
			case AW9523_PIN_0_3: dev->chip[chip].p0_mode[3] = mode; break;
			case AW9523_PIN_0_4: dev->chip[chip].p0_mode[4] = mode; break;
			case AW9523_PIN_0_5: dev->chip[chip].p0_mode[5] = mode; break;
			case AW9523_PIN_0_6: dev->chip[chip].p0_mode[6] = mode; break;
			case AW9523_PIN_0_7: dev->chip[chip].p0_mode[7] = mode; break;
			case AW9523_PIN_1_0: dev->chip[chip].p1_mode[0] = mode; break;
			case AW9523_PIN_1_1: dev->chip[chip].p1_mode[1] = mode; break;
			case AW9523_PIN_1_2: dev->chip[chip].p1_mode[2] = mode; break;
			case AW9523_PIN_1_3: dev->chip[chip].p1_mode[3] = mode; break;
			case AW9523_PIN_1_4: dev->chip[chip].p1_mode[4] = mode; break;
			case AW9523_PIN_1_5: dev->chip[chip].p1_mode[5] = mode; break;
			case AW9523_PIN_1_6: dev->chip[chip].p1_mode[6] = mode; break;
			case AW9523_PIN_1_7: dev->chip[chip].p1_mode[7] = mode; break;
			default: break;
			}
		}
	}

	return HAL_OK;
}

HAL_StatusTypeDef aw9523_apply_config(aw9523_dev *dev)
{
	if (dev == NULL)
		return HAL_ERROR;
	if (dev->hi2c == NULL)
		return HAL_ERROR;
	if (dev->chip_num > AW9523_CHIP_NUM_MAX)
		return HAL_ERROR;
	if (dev->rst_gpio_port == NULL)
		return HAL_ERROR;

	HAL_StatusTypeDef res = HAL_OK;

	for (uint32_t chip = 0; chip < dev->chip_num; chip++) {
		uint8_t chip_addr = AW9523_ADDRESS;
		if (dev->chip[chip].a0)
			chip_addr |= 0x02;
		if (dev->chip[chip].a1)
			chip_addr |= 0x04;
		uint8_t reg_cfg[2] = {0};
		uint8_t reg_led_mod_sw_p[2] = {0};

		for (uint8_t i = 0; i < 8; i++) {
			if (dev->chip[chip].p0_mode[i] == AW9523_PIN_MODE_IN) {
				reg_cfg[0] |= (0x01 << i);
				AW9523_LOG_E("aw9523_apply_config: input mode not implemented");
				res |= HAL_ERROR;
			}
			if (dev->chip[chip].p1_mode[i] == AW9523_PIN_MODE_IN) {
				reg_cfg[1] |= (0x01 << i);
				AW9523_LOG_E("aw9523_apply_config: input mode not implemented");
				res |= HAL_ERROR;
			}
		}

		for (uint8_t i = 0; i < 8; i++) {
			if (dev->chip[chip].p0_mode[i] == AW9523_PIN_MODE_OUT)
				reg_led_mod_sw_p[0] |= (0x01 << i);
			if (dev->chip[chip].p1_mode[i] == AW9523_PIN_MODE_OUT)
				reg_led_mod_sw_p[1] |= (0x01 << i);
		}

		res |= aw9523_set_reg(dev->hi2c, chip_addr, CONFIG_PORT0, reg_cfg, 2);
		res |= aw9523_set_reg(dev->hi2c, chip_addr, LED_MODE_SWITCH_P0, reg_led_mod_sw_p, 2);
	}

	return res;
}

HAL_StatusTypeDef aw9523_apply_output(aw9523_dev *dev)
{
	if (dev == NULL)
		return HAL_ERROR;
	if (dev->hi2c == NULL)
		return HAL_ERROR;
	if (dev->chip_num > AW9523_CHIP_NUM_MAX)
		return HAL_ERROR;
	if (dev->rst_gpio_port == NULL)
		return HAL_ERROR;

	HAL_StatusTypeDef res = HAL_OK;

	for (uint32_t chip = 0; chip < dev->chip_num; chip++) {
		uint8_t chip_addr = AW9523_ADDRESS;
		if (dev->chip[chip].a0)
			chip_addr |= 0x02;
		if (dev->chip[chip].a1)
			chip_addr |= 0x04;
		uint8_t reg_out[2] = {0};
		uint8_t reg_dim[16] = {0};

		for (uint8_t i = 0; i < 8; i++) {
			if (dev->chip[chip].p0_val[i])
				reg_out[0] |= (0x01 << i);
			if (dev->chip[chip].p1_val[i])
				reg_out[1] |= (0x01 << i);
		}

		/* DIM0..DIM15 are NOT sequential vs P0/P1 pin numbers. */
		reg_dim[0]  = dev->chip[chip].p1_led_dim[0];
		reg_dim[1]  = dev->chip[chip].p1_led_dim[1];
		reg_dim[2]  = dev->chip[chip].p1_led_dim[2];
		reg_dim[3]  = dev->chip[chip].p1_led_dim[3];
		reg_dim[4]  = dev->chip[chip].p0_led_dim[0];
		reg_dim[5]  = dev->chip[chip].p0_led_dim[1];
		reg_dim[6]  = dev->chip[chip].p0_led_dim[2];
		reg_dim[7]  = dev->chip[chip].p0_led_dim[3];
		reg_dim[8]  = dev->chip[chip].p0_led_dim[4];
		reg_dim[9]  = dev->chip[chip].p0_led_dim[5];
		reg_dim[10] = dev->chip[chip].p0_led_dim[6];
		reg_dim[11] = dev->chip[chip].p0_led_dim[7];
		reg_dim[12] = dev->chip[chip].p1_led_dim[4];
		reg_dim[13] = dev->chip[chip].p1_led_dim[5];
		reg_dim[14] = dev->chip[chip].p1_led_dim[6];
		reg_dim[15] = dev->chip[chip].p1_led_dim[7];

		res |= aw9523_set_reg(dev->hi2c, chip_addr, OUTPUT_PORT0, reg_out, 2);
		res |= aw9523_set_reg(dev->hi2c, chip_addr, DIM0, reg_dim, 16);
	}

	return res;
}

HAL_StatusTypeDef aw9523_set_pin_gpioval(aw9523_dev *dev, uint32_t chip, AW9523_PIN pin, uint8_t val)
{
	if (dev == NULL)
		return HAL_ERROR;
	if (chip >= dev->chip_num || chip >= AW9523_CHIP_NUM_MAX)
		return HAL_ERROR;

	if (pin == AW9523_PIN_NULL)
		return HAL_OK;

	for (uint32_t i = 0; i < 16; i++) {
		if ((pin & (0x01 << i)) != 0) {
			switch (0x01 << i) {
			case AW9523_PIN_0_0: dev->chip[chip].p0_val[0] = val; break;
			case AW9523_PIN_0_1: dev->chip[chip].p0_val[1] = val; break;
			case AW9523_PIN_0_2: dev->chip[chip].p0_val[2] = val; break;
			case AW9523_PIN_0_3: dev->chip[chip].p0_val[3] = val; break;
			case AW9523_PIN_0_4: dev->chip[chip].p0_val[4] = val; break;
			case AW9523_PIN_0_5: dev->chip[chip].p0_val[5] = val; break;
			case AW9523_PIN_0_6: dev->chip[chip].p0_val[6] = val; break;
			case AW9523_PIN_0_7: dev->chip[chip].p0_val[7] = val; break;
			case AW9523_PIN_1_0: dev->chip[chip].p1_val[0] = val; break;
			case AW9523_PIN_1_1: dev->chip[chip].p1_val[1] = val; break;
			case AW9523_PIN_1_2: dev->chip[chip].p1_val[2] = val; break;
			case AW9523_PIN_1_3: dev->chip[chip].p1_val[3] = val; break;
			case AW9523_PIN_1_4: dev->chip[chip].p1_val[4] = val; break;
			case AW9523_PIN_1_5: dev->chip[chip].p1_val[5] = val; break;
			case AW9523_PIN_1_6: dev->chip[chip].p1_val[6] = val; break;
			case AW9523_PIN_1_7: dev->chip[chip].p1_val[7] = val; break;
			default: break;
			}
		}
	}

	return HAL_OK;
}

HAL_StatusTypeDef aw9523_set_pin_ledval(aw9523_dev *dev, uint32_t chip, AW9523_PIN pin, uint8_t led_val)
{
	if (dev == NULL)
		return HAL_ERROR;
	if (chip >= dev->chip_num || chip >= AW9523_CHIP_NUM_MAX)
		return HAL_ERROR;

	if (pin == AW9523_PIN_NULL)
		return HAL_OK;

	for (uint32_t i = 0; i < 16; i++) {
		if ((pin & (0x01 << i)) != 0) {
			switch (0x01 << i) {
			case AW9523_PIN_0_0: dev->chip[chip].p0_led_dim[0] = led_val; break;
			case AW9523_PIN_0_1: dev->chip[chip].p0_led_dim[1] = led_val; break;
			case AW9523_PIN_0_2: dev->chip[chip].p0_led_dim[2] = led_val; break;
			case AW9523_PIN_0_3: dev->chip[chip].p0_led_dim[3] = led_val; break;
			case AW9523_PIN_0_4: dev->chip[chip].p0_led_dim[4] = led_val; break;
			case AW9523_PIN_0_5: dev->chip[chip].p0_led_dim[5] = led_val; break;
			case AW9523_PIN_0_6: dev->chip[chip].p0_led_dim[6] = led_val; break;
			case AW9523_PIN_0_7: dev->chip[chip].p0_led_dim[7] = led_val; break;
			case AW9523_PIN_1_0: dev->chip[chip].p1_led_dim[0] = led_val; break;
			case AW9523_PIN_1_1: dev->chip[chip].p1_led_dim[1] = led_val; break;
			case AW9523_PIN_1_2: dev->chip[chip].p1_led_dim[2] = led_val; break;
			case AW9523_PIN_1_3: dev->chip[chip].p1_led_dim[3] = led_val; break;
			case AW9523_PIN_1_4: dev->chip[chip].p1_led_dim[4] = led_val; break;
			case AW9523_PIN_1_5: dev->chip[chip].p1_led_dim[5] = led_val; break;
			case AW9523_PIN_1_6: dev->chip[chip].p1_led_dim[6] = led_val; break;
			case AW9523_PIN_1_7: dev->chip[chip].p1_led_dim[7] = led_val; break;
			default: break;
			}
		}
	}

	return HAL_OK;
}

static HAL_StatusTypeDef aw9523_SetAllIo_Hiz(aw9523_dev *dev, uint32_t chip)
{
	HAL_StatusTypeDef res = HAL_OK;
	uint8_t data[16] = {0};

	uint8_t chip_addr = AW9523_ADDRESS;
	if (dev->chip[chip].a0)
		chip_addr |= 0x02;
	if (dev->chip[chip].a1)
		chip_addr |= 0x04;

	res |= aw9523_set_reg(dev->hi2c, chip_addr, LED_MODE_SWITCH_P0, data, 2);
	res |= aw9523_set_reg(dev->hi2c, chip_addr, DIM0, data, 16);

	return res;
}

#define AW9523_I2C_BUFF_SIZE 32
static HAL_StatusTypeDef aw9523_set_reg(I2C_HandleTypeDef *hi2c, uint16_t DevAddress, uint8_t RegAddress, uint8_t *pData, uint16_t Size)
{
	uint8_t buf[AW9523_I2C_BUFF_SIZE] = {RegAddress};
	if (Size > AW9523_I2C_BUFF_SIZE - 1)
		return HAL_ERROR;

	for (uint32_t i = 0; i < Size; i++)
		buf[i + 1] = pData[i];
	return aw9523_i2c_transmit(hi2c, DevAddress, buf, Size + 1);
}

static inline HAL_StatusTypeDef aw9523_i2c_transmit(I2C_HandleTypeDef *hi2c, uint16_t DevAddress, uint8_t *pData, uint16_t Size)
{
	return HAL_I2C_Master_Transmit(hi2c, DevAddress, pData, Size, I2C_TIMOUT_MS);
}
