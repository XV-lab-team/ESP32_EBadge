#ifndef __AW9523_PORT_H_
#define __AW9523_PORT_H_

/*
 * Board port for AW9523 / exio.
 *
 * Target AI: edit THIS file. Do not change aw9523.c / exio.c behavior.
 * Include path must see this header together with aw9523.h / exio.h.
 */

#include "main.h"
/* If I2C_HandleTypeDef is unknown after main.h, add: #include "i2c.h" */

#ifndef AW9523_MCU_GPIO_OUT_RESET
#define AW9523_MCU_GPIO_OUT_RESET(port, pin) \
	HAL_GPIO_WritePin((port), (pin), GPIO_PIN_RESET)
#endif

#ifndef AW9523_MCU_GPIO_OUT_SET
#define AW9523_MCU_GPIO_OUT_SET(port, pin) \
	HAL_GPIO_WritePin((port), (pin), GPIO_PIN_SET)
#endif

/*
 * Optional logs. Default: compile with no log library.
 * Wire to the target log if needed, e.g. XK_LOGE("AW9523", fmt, ##__VA_ARGS__).
 */
#ifndef AW9523_LOG_E
#define AW9523_LOG_E(fmt, ...)  ((void)0)
#endif

#ifndef EXIO_LOG_E
#define EXIO_LOG_E(fmt, ...)    ((void)0)
#endif
#ifndef EXIO_LOG_W
#define EXIO_LOG_W(fmt, ...)    ((void)0)
#endif
#ifndef EXIO_LOG_I
#define EXIO_LOG_I(fmt, ...)    ((void)0)
#endif
#ifndef EXIO_LOG_D
#define EXIO_LOG_D(fmt, ...)    ((void)0)
#endif
#ifndef EXIO_LOG_V
#define EXIO_LOG_V(fmt, ...)    ((void)0)
#endif

#endif
