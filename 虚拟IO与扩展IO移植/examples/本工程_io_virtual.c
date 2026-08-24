//虚拟IO  将MCU自带的IO和扩展IO连接到统一的接口上  一些可能会改变驱动引脚是否使用扩展的引脚，必须由此控制
//注意！！虚拟IO控制的实时性很差，不能控制一些频繁变化的IO
#include "io_virtual.h"
#include <stdio.h>
#include <string.h>

#include "cmsis_os.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "bsp.h"
#include "log.h"
#include "exio.h"

/* -------------------- 本模块日志宏（独立 TAG；W/I/D/V 可单独开关） -------------------- */
#define IO_VIRTUAL_TAG  "io_virtual"

#define IO_VIRTUAL_WARNING_ON  1
#define IO_VIRTUAL_INFO_ON     1
#define IO_VIRTUAL_DEBUG_ON    1
#define IO_VIRTUAL_VERBOSE_ON  0

#define IO_VIRTUAL_LOG_E(fmt, ...)  do { XK_LOGE(IO_VIRTUAL_TAG, fmt, ##__VA_ARGS__); } while (0)
#define IO_VIRTUAL_LOG_W(fmt, ...)  do { if (IO_VIRTUAL_WARNING_ON) XK_LOGW(IO_VIRTUAL_TAG, fmt, ##__VA_ARGS__); } while (0)
#define IO_VIRTUAL_LOG_I(fmt, ...)  do { if (IO_VIRTUAL_INFO_ON)    XK_LOGI(IO_VIRTUAL_TAG, fmt, ##__VA_ARGS__); } while (0)
#define IO_VIRTUAL_LOG_D(fmt, ...)  do { if (IO_VIRTUAL_DEBUG_ON)   XK_LOGD(__FUNCTION__, fmt, ##__VA_ARGS__); } while (0)
#define IO_VIRTUAL_LOG_V(fmt, ...)  do { if (IO_VIRTUAL_VERBOSE_ON) XK_LOGV(IO_VIRTUAL_TAG, fmt, ##__VA_ARGS__); } while (0)

/* 每帧 apply 约 10 条 + fault_apply 一次可超 20 条；16 易满导致照明等后写命令被丢 */
#define VIO_CMD_QUEUE_LEN   64
#define VIO_CMD_SEND_MS     20

typedef enum {
	VIO_CMD_GPIO_SET = 0,
	VIO_CMD_LED_SET,
} vio_cmd_type_t;

typedef struct {
	vio_cmd_type_t type;
	uint8_t          id;
	uint8_t          val;
} vio_cmd_t;

static exio_t s_exio_dev;
static QueueHandle_t s_vio_cmd_q;
static StaticQueue_t s_vio_cmd_static;
static uint8_t s_vio_cmd_storage[VIO_CMD_QUEUE_LEN * sizeof(vio_cmd_t)];
static volatile uint8_t s_dirty;

/* AW9523 设备：新 PCB 丝印 EXIO1/2/4 → 代码 chip 0/1/2（AD=00/01/11） */
static const aw9523_dev aw9523_dev_cfg={
		.hi2c						= &hi2c2,			//i2c句柄
		.rst_gpio_port 	=	EXIO_RST_GPIO_Port,//RST引脚的port
		.rst_gpio_pin		=	EXIO_RST_Pin,		//RST引脚的pin
		.chip_num				=	3,					//芯片数量
		.chip = {
			{ .a0=0,  .a1=0,  .gpio_p0_mod_pp=1,  .ledmode_isel = 3, 	}, /* EXIO1 0xB0 */
			{ .a0=1,	.a1=0,	.gpio_p0_mod_pp=1,  .ledmode_isel = 3,  }, /* EXIO2 0xB2 AD0=1 AD1=0 */
			{ .a0=1,	.a1=1,	.gpio_p0_mod_pp=1,  .ledmode_isel = 3,  }, /* EXIO4 0xB6 */
		}
};

/* GPIO 表：与 vio_gpio_id_t 下标一一对应；新板网名见《新PCB引脚变更记录》§5 */
static const exio_gpio_pin_t exio_gpio_pin_cfg[64]={
  {.pin=AW9523_PIN_1_4, .mode=AW9523_PIN_MODE_OUT,  .val_default=0, .chip_num=1,  .desc="照明" },
  {.pin=AW9523_PIN_0_2, .mode=AW9523_PIN_MODE_OUT,  .val_default=0, .chip_num=1,  .desc="扩展断电" }, /* EX_PWR_DOWN；旧名 EXPANSION */
  {.pin=AW9523_PIN_1_1, .mode=AW9523_PIN_MODE_OUT,  .val_default=0, .chip_num=0,  .desc="电调1断电" },
  {.pin=AW9523_PIN_1_3, .mode=AW9523_PIN_MODE_OUT,  .val_default=0, .chip_num=0,  .desc="电调1缺相" },
  {.pin=AW9523_PIN_1_2, .mode=AW9523_PIN_MODE_OUT,  .val_default=0, .chip_num=0,  .desc="电调1换相" },
  {.pin=AW9523_PIN_0_5, .mode=AW9523_PIN_MODE_OUT,  .val_default=0, .chip_num=2,  .desc="电调2断电" },
  {.pin=AW9523_PIN_1_0, .mode=AW9523_PIN_MODE_OUT,  .val_default=0, .chip_num=0,  .desc="电调2缺相" },
  {.pin=AW9523_PIN_0_3, .mode=AW9523_PIN_MODE_OUT,  .val_default=0, .chip_num=2,  .desc="电调2换相" },
  {.pin=AW9523_PIN_0_6, .mode=AW9523_PIN_MODE_OUT,  .val_default=0, .chip_num=0,  .desc="电调3断电" },
  {.pin=AW9523_PIN_1_4, .mode=AW9523_PIN_MODE_OUT,  .val_default=0, .chip_num=0,  .desc="电调3缺相" },
  {.pin=AW9523_PIN_0_7, .mode=AW9523_PIN_MODE_OUT,  .val_default=0, .chip_num=0,  .desc="电调3换相" },
	{.pin=AW9523_PIN_0_4, .mode=AW9523_PIN_MODE_OUT,  .val_default=0, .chip_num=2,  .desc="电调4断电" },
  {.pin=AW9523_PIN_0_1, .mode=AW9523_PIN_MODE_OUT,  .val_default=0, .chip_num=2,  .desc="电调4缺相" },
  {.pin=AW9523_PIN_0_2, .mode=AW9523_PIN_MODE_OUT,  .val_default=0, .chip_num=2,  .desc="电调4换相" },
	{.pin=AW9523_PIN_0_0, .mode=AW9523_PIN_MODE_OUT,  .val_default=0, .chip_num=0,  .desc="电调5断电" },
  {.pin=AW9523_PIN_0_1, .mode=AW9523_PIN_MODE_OUT,  .val_default=0, .chip_num=0,  .desc="电调5缺相" },
  {.pin=AW9523_PIN_0_5, .mode=AW9523_PIN_MODE_OUT,  .val_default=0, .chip_num=0,  .desc="电调5换相" },
	{.pin=AW9523_PIN_1_5, .mode=AW9523_PIN_MODE_OUT,  .val_default=0, .chip_num=1,  .desc="舵机1断电" },
	{.pin=AW9523_PIN_1_5, .mode=AW9523_PIN_MODE_OUT,  .val_default=0, .chip_num=0,  .desc="舵机2断电" },
 	{.pin=AW9523_PIN_0_2, .mode=AW9523_PIN_MODE_OUT,  .val_default=0, .chip_num=0,  .desc="舵机3断电" },
	{.pin=AW9523_PIN_0_4, .mode=AW9523_PIN_MODE_OUT,  .val_default=0, .chip_num=0,  .desc="舵机4断电" },
	{.pin=AW9523_PIN_0_3, .mode=AW9523_PIN_MODE_OUT,  .val_default=0, .chip_num=0,  .desc="舵机5断电" },
	{.pin=AW9523_PIN_0_6, .mode=AW9523_PIN_MODE_OUT,  .val_default=0, .chip_num=1,  .desc="舵机6断电" },
	{.pin=AW9523_PIN_0_3, .mode=AW9523_PIN_MODE_OUT,  .val_default=0, .chip_num=1,  .desc="扩展UP" },   /* EX_IO1 */
	{.pin=AW9523_PIN_0_1, .mode=AW9523_PIN_MODE_OUT,  .val_default=0, .chip_num=1,  .desc="扩展DOWN" }, /* EX_IO2 */
	{.pin=AW9523_PIN_0_5, .mode=AW9523_PIN_MODE_OUT,  .val_default=0, .chip_num=1,  .desc="绞盘UP" },
	{.pin=AW9523_PIN_0_0, .mode=AW9523_PIN_MODE_OUT,  .val_default=0, .chip_num=1,  .desc="绞盘DOWN" },
	{.pin=AW9523_PIN_0_4, .mode=AW9523_PIN_MODE_OUT,  .val_default=0, .chip_num=1,  .desc="绞盘断电" },
	{.pin=AW9523_PIN_0_7, .mode=AW9523_PIN_MODE_OUT,  .val_default=0, .chip_num=1,  .desc="照明断电" },
	{.pin=AW9523_PIN_1_7, .mode=AW9523_PIN_MODE_OUT,  .val_default=0, .chip_num=0,  .desc="接收机断电" }, /* FS */
	{.pin=AW9523_PIN_1_6, .mode=AW9523_PIN_MODE_OUT,  .val_default=0, .chip_num=0,  .desc="飞控断电" },   /* FC */
};

/* LED：M1~M5 指示灯均在 EXIO4（chip=2） */
static const exio_led_pin_t exio_led_pin_cfg[64]={
  {.pin=AW9523_PIN_1_1, .val_default=0, .chip_num=2,  .desc="电调1的指示灯" }, /* M1_L EXIO4_1_1 */
  {.pin=AW9523_PIN_1_0, .val_default=0, .chip_num=2,  .desc="电调2的指示灯" }, /* M2_L EXIO4_1_0 */
  {.pin=AW9523_PIN_1_2, .val_default=0, .chip_num=2,  .desc="电调3的指示灯" }, /* M3_L EXIO4_1_2 */
  {.pin=AW9523_PIN_0_0, .val_default=0, .chip_num=2,  .desc="电调4的指示灯" }, /* M4_L EXIO4_0_0 */
  {.pin=AW9523_PIN_1_3, .val_default=0, .chip_num=2,  .desc="电调5的指示灯" }, /* M5_L EXIO4_1_3 */
};

// 将 IO 控制命令投递到队列，由 io_virtual_task 异步执行
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
		IO_VIRTUAL_LOG_W("cmd queue full type=%u id=%u val=%u",
		                 (unsigned)type, (unsigned)id, (unsigned)val);
		return HAL_BUSY;
	}

	return HAL_OK;
}

// 处理单条队列命令：按逻辑 ID 查配置表，写入 aw9523 输出缓存
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

// 指定 GPIO 输出高低电平，level: 0=低电平，非0=高电平
HAL_StatusTypeDef io_virtual_gpio_set(vio_gpio_id_t id, uint8_t level)
{
	if (id >= VIO_GPIO_NUM)
		return HAL_ERROR;

	return io_virtual_cmd_send(VIO_CMD_GPIO_SET, (uint8_t)id, level ? 1 : 0);
}

// 指定 LED 输出亮度，brightness: 0~255
HAL_StatusTypeDef io_virtual_led_set(vio_led_id_t id, uint8_t brightness)
{
	if (id >= VIO_LED_NUM)
		return HAL_ERROR;

	return io_virtual_cmd_send(VIO_CMD_LED_SET, (uint8_t)id, brightness);
}


/**
 * 虚拟 IO 任务：初始化扩展 IO，阻塞等待命令队列并刷新硬件输出
 */
void io_virtual_task(void* arg)
{
	s_vio_cmd_q = xQueueCreateStatic(VIO_CMD_QUEUE_LEN, sizeof(vio_cmd_t),
	                                s_vio_cmd_storage, &s_vio_cmd_static);
	if (s_vio_cmd_q == NULL) {
		IO_VIRTUAL_LOG_E("cmd queue create failed");
		vTaskDelete(NULL);
		return;
	}

	vTaskSuspendAll();
	memset(&s_exio_dev, 0, sizeof(exio_t));
	memcpy(&s_exio_dev.aw9523_dev,     &aw9523_dev_cfg,     sizeof(aw9523_dev_cfg));
	memcpy(&s_exio_dev.exio_gpio_pin,  &exio_gpio_pin_cfg,  sizeof(exio_gpio_pin_cfg));
	memcpy(&s_exio_dev.exio_led_pin,   &exio_led_pin_cfg,   sizeof(exio_led_pin_cfg));

	HAL_StatusTypeDef res = exio_init(&s_exio_dev);
	if (res == HAL_OK)
		IO_VIRTUAL_LOG_I("Init: exio_Init=%d", res);
	else
		IO_VIRTUAL_LOG_E("Init: exio_Init=%d", res);

	xTaskResumeAll();

	for (;;) {
		vio_cmd_t cmd;

		// 无命令时阻塞休眠，避免空转占满 CPU
		if (xQueueReceive(s_vio_cmd_q, &cmd, portMAX_DELAY) != pdPASS)
			continue;

		io_virtual_handle_cmd(&cmd);

		// 尽量一次收完队列里积压的命令，再统一刷硬件
		while (xQueueReceive(s_vio_cmd_q, &cmd, 0) == pdPASS)
			io_virtual_handle_cmd(&cmd);

		if (s_dirty) {
			exio_apply(&s_exio_dev);
			s_dirty = 0;
		}
	}
}

/* -------------------- AW9523 在位写入探测（诊断；默认不创建本任务） -------------------- */
#define VIO_CHIP_TEST_BASE_ADDR   0xB0u   /* 与 aw9523.c AW9523_ADDRESS 一致 */
#define VIO_CHIP_TEST_REG_OUT0    0x02u   /* OUTPUT_PORT0，连写 2 字节含 PORT1 */
#define VIO_CHIP_TEST_I2C_MS      10u
#define VIO_CHIP_TEST_PERIOD_MS   1000u

/**
 * 按 AD 位拼 I2C 7bit 地址左移后的 8bit 地址（HAL 约定）
 */
static uint8_t vio_chip_test_addr(uint8_t a0, uint8_t a1)
{
	uint8_t addr = (uint8_t)VIO_CHIP_TEST_BASE_ADDR;

	if (a0)
		addr |= 0x02u;
	if (a1)
		addr |= 0x04u;
	return addr;
}

/**
 * 对单片写 OUTPUT=0，用 ACK/超时判断芯片是否仍在总线上
 * @return HAL_OK 表示写成功（芯片应答）
 */
static HAL_StatusTypeDef vio_chip_test_write_one(uint8_t a0, uint8_t a1)
{
	uint8_t addr = vio_chip_test_addr(a0, a1);
	uint8_t out[2] = { 0u, 0u }; /* 安全默认：输出全低，避免误吸合继电器 */

	return HAL_I2C_Mem_Write(&hi2c2, addr, VIO_CHIP_TEST_REG_OUT0,
	                         I2C_MEMADD_SIZE_8BIT, out, 2u, VIO_CHIP_TEST_I2C_MS);
}

/**
 * AW9523 三片在位写入探测任务
 * 周期对 EXIO1/EXIO2/EXIO4 各做一次 OUTPUT 寄存器写入，打 OK/FAIL 日志。
 * 诊断用：logic 里默认不创建；勿与 io_virtual_task 长时间并行抢 I2C。
 */
void io_virtual_chip_test_task(void *arg)
{
	(void)arg;

	static const char *const silk_name[3] = { "EXIO1", "EXIO2", "EXIO4" };
	uint32_t round = 0u;

	IO_VIRTUAL_LOG_I("chip_test: start (3 chips, write OUTPUT=0)");

	for (;;) {
		uint8_t ok_cnt = 0u;

		round++;
		for (uint32_t i = 0u; i < 3u; i++) {
			uint8_t a0 = aw9523_dev_cfg.chip[i].a0;
			uint8_t a1 = aw9523_dev_cfg.chip[i].a1;
			uint8_t addr = vio_chip_test_addr(a0, a1);
			HAL_StatusTypeDef res = vio_chip_test_write_one(a0, a1);

			if (res == HAL_OK) {
				ok_cnt++;
				IO_VIRTUAL_LOG_I("chip_test[%lu] %s chip=%lu addr=0x%02X OK",
				                 (unsigned long)round, silk_name[i],
				                 (unsigned long)i, (unsigned)addr);
			} else {
				IO_VIRTUAL_LOG_E("chip_test[%lu] %s chip=%lu addr=0x%02X FAIL res=%d",
				                 (unsigned long)round, silk_name[i],
				                 (unsigned long)i, (unsigned)addr, (int)res);
			}
		}

		IO_VIRTUAL_LOG_I("chip_test[%lu] summary %u/3 OK",
		                 (unsigned long)round, (unsigned)ok_cnt);
		vTaskDelay(pdMS_TO_TICKS(VIO_CHIP_TEST_PERIOD_MS));
	}
}




