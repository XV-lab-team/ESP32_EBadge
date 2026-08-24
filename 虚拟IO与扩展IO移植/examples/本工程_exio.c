//用于控制+存储IO扩展信息
#include "exio.h"
#include "log.h"

/* -------------------- 本模块日志宏（独立 TAG；W/I/D/V 可单独开关） -------------------- */
#define EXIO_TAG  "exio"

#define EXIO_WARNING_ON  1
#define EXIO_INFO_ON     1
#define EXIO_DEBUG_ON    1
#define EXIO_VERBOSE_ON  0

#define EXIO_LOG_E(fmt, ...)  do { XK_LOGE(EXIO_TAG, fmt, ##__VA_ARGS__); } while (0)
#define EXIO_LOG_W(fmt, ...)  do { if (EXIO_WARNING_ON) XK_LOGW(EXIO_TAG, fmt, ##__VA_ARGS__); } while (0)
#define EXIO_LOG_I(fmt, ...)  do { if (EXIO_INFO_ON)    XK_LOGI(EXIO_TAG, fmt, ##__VA_ARGS__); } while (0)
#define EXIO_LOG_D(fmt, ...)  do { if (EXIO_DEBUG_ON)   XK_LOGD(__FUNCTION__, fmt, ##__VA_ARGS__); } while (0)
#define EXIO_LOG_V(fmt, ...)  do { if (EXIO_VERBOSE_ON) XK_LOGV(EXIO_TAG, fmt, ##__VA_ARGS__); } while (0)


HAL_StatusTypeDef exio_init(exio_t* dev){
	HAL_StatusTypeDef res = HAL_OK;
	HAL_StatusTypeDef tmp;

	if(dev == NULL){
		EXIO_LOG_E("{%s} dev == NULL", __FUNCTION__);
		return HAL_ERROR;
	}

	tmp = aw9523_init(&dev->aw9523_dev);
	if(tmp != HAL_OK)
		EXIO_LOG_E("aw9523_init failed, res=%d", tmp);
	res |= tmp;

	// 将GPIO的配置写入缓存
	for(uint32_t i = 0; i < sizeof(dev->exio_gpio_pin) / sizeof(exio_gpio_pin_t); i++){
		if(dev->exio_gpio_pin[i].pin == AW9523_PIN_NULL)
			continue;

		tmp = aw9523_set_pin_mode(&dev->aw9523_dev,
		                          dev->exio_gpio_pin[i].chip_num,
		                          dev->exio_gpio_pin[i].pin,
		                          dev->exio_gpio_pin[i].mode);
		if(tmp != HAL_OK)
			EXIO_LOG_E("GPIO[%u] %s set_pin_mode failed, chip=%u pin=0x%x res=%d",
			        (unsigned)i,
			        dev->exio_gpio_pin[i].desc ? dev->exio_gpio_pin[i].desc : "?",
			        (unsigned)dev->exio_gpio_pin[i].chip_num,
			        (unsigned)dev->exio_gpio_pin[i].pin,
			        tmp);
		res |= tmp;

		tmp = aw9523_set_pin_gpioval(&dev->aw9523_dev,
		                             dev->exio_gpio_pin[i].chip_num,
		                             dev->exio_gpio_pin[i].pin,
		                             dev->exio_gpio_pin[i].val_default);
		if(tmp != HAL_OK)
			EXIO_LOG_E("GPIO[%u] %s set_pin_gpioval failed, chip=%u pin=0x%x val=%u res=%d",
			        (unsigned)i,
			        dev->exio_gpio_pin[i].desc ? dev->exio_gpio_pin[i].desc : "?",
			        (unsigned)dev->exio_gpio_pin[i].chip_num,
			        (unsigned)dev->exio_gpio_pin[i].pin,
			        dev->exio_gpio_pin[i].val_default,
			        tmp);
		res |= tmp;
	}

	// 将LED的配置写入缓存
	for(uint32_t i = 0; i < sizeof(dev->exio_led_pin) / sizeof(exio_led_pin_t); i++){
		if(dev->exio_led_pin[i].pin == AW9523_PIN_NULL)
			continue;

		tmp = aw9523_set_pin_mode(&dev->aw9523_dev,
		                          dev->exio_led_pin[i].chip_num,
		                          dev->exio_led_pin[i].pin,
		                          AW9523_PIN_MODE_LED);
		if(tmp != HAL_OK)
			EXIO_LOG_E("LED[%u] %s set_pin_mode failed, chip=%u pin=0x%x res=%d",
			        (unsigned)i,
			        dev->exio_led_pin[i].desc ? dev->exio_led_pin[i].desc : "?",
			        (unsigned)dev->exio_led_pin[i].chip_num,
			        (unsigned)dev->exio_led_pin[i].pin,
			        tmp);
		res |= tmp;

		tmp = aw9523_set_pin_ledval(&dev->aw9523_dev,
		                            dev->exio_led_pin[i].chip_num,
		                            dev->exio_led_pin[i].pin,
		                            dev->exio_led_pin[i].val_default);
		if(tmp != HAL_OK)
			EXIO_LOG_E("LED[%u] %s set_pin_ledval failed, chip=%u pin=0x%x val=%u res=%d",
			        (unsigned)i,
			        dev->exio_led_pin[i].desc ? dev->exio_led_pin[i].desc : "?",
			        (unsigned)dev->exio_led_pin[i].chip_num,
			        (unsigned)dev->exio_led_pin[i].pin,
			        dev->exio_led_pin[i].val_default,
			        tmp);
		res |= tmp;
	}

	tmp = aw9523_apply_output(&dev->aw9523_dev);
	if(tmp != HAL_OK)
		EXIO_LOG_E("aw9523_apply_output failed, res=%d", tmp);
	res |= tmp;

	tmp = aw9523_apply_config(&dev->aw9523_dev);
	if(tmp != HAL_OK)
		EXIO_LOG_E("aw9523_apply_config failed, res=%d", tmp);
	res |= tmp;

	if(res != HAL_OK)
		EXIO_LOG_E("{%s} init failed, total res=%d", __FUNCTION__, res);

	return res;
}





//刷新外部芯片
HAL_StatusTypeDef exio_apply(exio_t* dev){
	if(dev == NULL)
		return HAL_ERROR;
  HAL_StatusTypeDef res = HAL_OK;
  res|= aw9523_apply_output(&dev->aw9523_dev);
	return res;
}















