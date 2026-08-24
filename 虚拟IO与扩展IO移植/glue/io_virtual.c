#include "io_virtual.h"

#include <string.h>

#include "cmsis_os.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

#include "exio.h"
#include "aw9523_port.h"

#ifndef VIO_LOG_E
#define VIO_LOG_E(fmt, ...)  ((void)0)
#endif
#ifndef VIO_LOG_W
#define VIO_LOG_W(fmt, ...)  ((void)0)
#endif
#ifndef VIO_LOG_I
#define VIO_LOG_I(fmt, ...)  ((void)0)
#endif

#ifndef VIO_CMD_QUEUE_LEN
#define VIO_CMD_QUEUE_LEN  64
#endif
#ifndef VIO_CMD_SEND_MS
#define VIO_CMD_SEND_MS    20
#endif

#ifndef VIO_I2C
extern I2C_HandleTypeDef hi2c2;
#define VIO_I2C  hi2c2
#endif

#ifndef VIO_RST_PORT
#define VIO_RST_PORT  EXIO_RST_GPIO_Port
#endif
#ifndef VIO_RST_PIN
#define VIO_RST_PIN   EXIO_RST_Pin
#endif

typedef enum {
	VIO_CMD_GPIO_SET = 0,
	VIO_CMD_LED_SET,
} vio_cmd_type_t;

typedef struct {
	vio_cmd_type_t type;
	uint8_t        id;
	uint8_t        val;
} vio_cmd_t;

static exio_t s_exio_dev;
static QueueHandle_t s_vio_cmd_q;
static StaticQueue_t s_vio_cmd_static;
static uint8_t s_vio_cmd_storage[VIO_CMD_QUEUE_LEN * sizeof(vio_cmd_t)];
static volatile uint8_t s_dirty;

/*
 * TODO: fill chip_num / a0 / a1 from the target schematic.
 *
 * 8-bit write address = 0xB0 | (a0 ? 0x02 : 0) | (a1 ? 0x04 : 0)
 * Trainer board (reference only):
 *   chip0 EXIO1 AD=00 -> 0xB0
 *   chip1 EXIO2 AD=01 -> 0xB2  (a0=1, a1=0)
 *   chip2 EXIO4 AD=11 -> 0xB6
 *
 * ledmode_isel must be 1..3 so GCR (P0 push-pull + Imax) is written.
 * 0 = Imax 4/4, 1 = 3/4, 2 = 2/4, 3 = 1/4.
 */
static const aw9523_dev aw9523_dev_cfg = {
	.hi2c          = &VIO_I2C,
	.rst_gpio_port = VIO_RST_PORT,
	.rst_gpio_pin  = VIO_RST_PIN,
	.chip_num      = 1,
	.chip = {
		{ .a0 = 0, .a1 = 0, .gpio_p0_mod_pp = 1, .ledmode_isel = 3 },
	}
};

/*
 * TODO: replace with the target pin map. Index MUST match vio_gpio_id_t.
 * Unused slots stay 0 == AW9523_PIN_NULL.
 * Do not assign the same chip+pin to two GPIO rows, or GPIO vs LED.
 */
static const exio_gpio_pin_t exio_gpio_pin_cfg[64] = {
	{ .pin = AW9523_PIN_0_0, .mode = AW9523_PIN_MODE_OUT, .val_default = 0, .chip_num = 0, .desc = "demo_gpio0" },
};

/*
 * TODO: replace with the target LED map. Index MUST match vio_led_id_t.
 */
static const exio_led_pin_t exio_led_pin_cfg[64] = {
	{ .pin = AW9523_PIN_0_1, .val_default = 0, .chip_num = 0, .desc = "demo_led0" },
};

static HAL_StatusTypeDef io_virtual_cmd_send(vio_cmd_type_t type, uint8_t id, uint8_t val)
{
	if (s_vio_cmd_q == NULL)
		return HAL_ERROR;

	vio_cmd_t cmd = {
		.type = type,
		.id   = id,
		.val  = val,
	};

	if (xQueueSend(s_vio_cmd_q, &cmd, pdMS_TO_TICKS(VIO_CMD_SEND_MS)) != pdPASS) {
		VIO_LOG_W("cmd queue full type=%u id=%u val=%u",
		          (unsigned)type, (unsigned)id, (unsigned)val);
		return HAL_BUSY;
	}

	return HAL_OK;
}

static void io_virtual_handle_cmd(const vio_cmd_t *cmd)
{
	HAL_StatusTypeDef res = HAL_OK;

	if (cmd->type == VIO_CMD_GPIO_SET) {
		if (cmd->id >= VIO_GPIO_NUM)
			return;

		const exio_gpio_pin_t *pin = &s_exio_dev.exio_gpio_pin[cmd->id];
		if (pin->pin == AW9523_PIN_NULL)
			return;

		res = aw9523_set_pin_gpioval(&s_exio_dev.aw9523_dev, pin->chip_num, pin->pin, cmd->val ? 1 : 0);
	} else if (cmd->type == VIO_CMD_LED_SET) {
		if (cmd->id >= VIO_LED_NUM)
			return;

		const exio_led_pin_t *pin = &s_exio_dev.exio_led_pin[cmd->id];
		if (pin->pin == AW9523_PIN_NULL)
			return;

		res = aw9523_set_pin_ledval(&s_exio_dev.aw9523_dev, pin->chip_num, pin->pin, cmd->val);
	} else {
		return;
	}

	if (res == HAL_OK)
		s_dirty = 1;
}

HAL_StatusTypeDef io_virtual_gpio_set(vio_gpio_id_t id, uint8_t level)
{
	if (id >= VIO_GPIO_NUM)
		return HAL_ERROR;

	return io_virtual_cmd_send(VIO_CMD_GPIO_SET, (uint8_t)id, level ? 1 : 0);
}

HAL_StatusTypeDef io_virtual_led_set(vio_led_id_t id, uint8_t brightness)
{
	if (id >= VIO_LED_NUM)
		return HAL_ERROR;

	return io_virtual_cmd_send(VIO_CMD_LED_SET, (uint8_t)id, brightness);
}

void io_virtual_task(void *arg)
{
	(void)arg;

	s_vio_cmd_q = xQueueCreateStatic(VIO_CMD_QUEUE_LEN, sizeof(vio_cmd_t),
	                                 s_vio_cmd_storage, &s_vio_cmd_static);
	if (s_vio_cmd_q == NULL) {
		VIO_LOG_E("cmd queue create failed");
		vTaskDelete(NULL);
		return;
	}

	vTaskSuspendAll();
	memset(&s_exio_dev, 0, sizeof(exio_t));
	memcpy(&s_exio_dev.aw9523_dev,    &aw9523_dev_cfg,    sizeof(aw9523_dev_cfg));
	memcpy(&s_exio_dev.exio_gpio_pin, &exio_gpio_pin_cfg, sizeof(exio_gpio_pin_cfg));
	memcpy(&s_exio_dev.exio_led_pin,  &exio_led_pin_cfg,  sizeof(exio_led_pin_cfg));

	{
		HAL_StatusTypeDef res = exio_init(&s_exio_dev);
		if (res == HAL_OK)
			VIO_LOG_I("Init: exio_Init=%d", res);
		else
			VIO_LOG_E("Init: exio_Init=%d", res);
	}

	xTaskResumeAll();

	for (;;) {
		vio_cmd_t cmd;

		if (xQueueReceive(s_vio_cmd_q, &cmd, portMAX_DELAY) != pdPASS)
			continue;

		io_virtual_handle_cmd(&cmd);

		while (xQueueReceive(s_vio_cmd_q, &cmd, 0) == pdPASS)
			io_virtual_handle_cmd(&cmd);

		if (s_dirty) {
			exio_apply(&s_exio_dev);
			s_dirty = 0;
		}
	}
}
