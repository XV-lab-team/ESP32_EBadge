#ifndef __IO_VIRTUAL_H_
#define __IO_VIRTUAL_H_

#include "main.h"

// GPIO 閫昏緫缂栧彿锛岄『搴忎笌 io_virtual.c 涓? exio_gpio_pin_cfg[] 涓�鑷?
typedef enum {
	VIO_GPIO_LIGHTING = 0,
	VIO_GPIO_EXPANSION,
	VIO_GPIO_ESC1_POWER_OFF,
	VIO_GPIO_ESC1_PHASE_LOSS,
	VIO_GPIO_ESC1_PHASE_SWAP,
	VIO_GPIO_ESC2_POWER_OFF,
	VIO_GPIO_ESC2_PHASE_LOSS,
	VIO_GPIO_ESC2_PHASE_SWAP,
	VIO_GPIO_ESC3_POWER_OFF,
	VIO_GPIO_ESC3_PHASE_LOSS,
	VIO_GPIO_ESC3_PHASE_SWAP,
	VIO_GPIO_ESC4_POWER_OFF,
	VIO_GPIO_ESC4_PHASE_LOSS,
	VIO_GPIO_ESC4_PHASE_SWAP,
	VIO_GPIO_ESC5_POWER_OFF,
	VIO_GPIO_ESC5_PHASE_LOSS,
	VIO_GPIO_ESC5_PHASE_SWAP,
	VIO_GPIO_SERVO1_POWER_OFF,
	VIO_GPIO_SERVO2_POWER_OFF,
	VIO_GPIO_SERVO3_POWER_OFF,
	VIO_GPIO_SERVO4_POWER_OFF,
	VIO_GPIO_SERVO5_POWER_OFF,
	VIO_GPIO_SERVO6_POWER_OFF,
	/* 扩展信号：UP/DOWN；断电仍用 VIO_GPIO_EXPANSION（=EX_PWR_DOWN） */
	VIO_GPIO_EX_IO1,
	VIO_GPIO_EX_IO2,
	/* 绞盘信号 / 断电；照明断电；接收机 FS / 飞控 FC 断电（仅追加，勿中插） */
	VIO_GPIO_WINCH_UP,
	VIO_GPIO_WINCH_DOWN,
	VIO_GPIO_WINCH_PWR_DOWN,
	VIO_GPIO_LIGHTING_PWR_DOWN,
	VIO_GPIO_FS_PWR_DOWN,
	VIO_GPIO_FC_PWR_DOWN,
	VIO_GPIO_NUM
} vio_gpio_id_t;

// LED 閫昏緫缂栧彿锛岄『搴忎笌 io_virtual.c 涓? exio_led_pin_cfg[] 涓�鑷?
typedef enum {
	VIO_LED_ESC1 = 0,
	VIO_LED_ESC2,
	VIO_LED_ESC3,
	VIO_LED_ESC4,
	VIO_LED_ESC5,
	VIO_LED_NUM
} vio_led_id_t;

void io_virtual_task(void* arg);

/**
 * AW9523 三片在位写入探测任务（诊断用；默认勿创建）
 * 对 EXIO1/2/4 各写一次 OUTPUT 寄存器，按 I2C 结果打日志；不入命令队列。
 * 与 io_virtual_task 共用 I2C2，同时跑可能互相干扰。
 */
void io_virtual_chip_test_task(void *arg);

// 鎸囧畾 GPIO 杈撳嚭楂樹綆鐢靛钩锛宭evel: 0=浣庣數骞筹紝闈?0=楂樼數骞?
HAL_StatusTypeDef io_virtual_gpio_set(vio_gpio_id_t id, uint8_t level);

// 鎺у埗 LED 浜?搴︼紝brightness: 0~255
HAL_StatusTypeDef io_virtual_led_set(vio_led_id_t id, uint8_t brightness);

static inline HAL_StatusTypeDef io_virtual_gpio_high(vio_gpio_id_t id){
	return io_virtual_gpio_set(id, 1);
}

static inline HAL_StatusTypeDef io_virtual_gpio_low(vio_gpio_id_t id){
	return io_virtual_gpio_set(id, 0);
}

#endif
