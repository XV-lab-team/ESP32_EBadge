#ifndef __AW9523_PORT_H_
#define __AW9523_PORT_H_

/*
 * ESP32-S3 board port for AW9523 / exio.
 * I2C: GPIO14 SCL / GPIO21 SDA. RSTN: GPIO3.
 * Do not change aw9523.c / exio.c behavior.
 */

#include <stdint.h>

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

typedef enum {
    HAL_OK      = 0x00,
    HAL_ERROR   = 0x01,
    HAL_BUSY    = 0x02,
    HAL_TIMEOUT = 0x03,
} HAL_StatusTypeDef;

typedef int GPIO_TypeDef;

typedef struct {
    i2c_master_bus_handle_t bus;
    i2c_master_dev_handle_t dev[4];
} I2C_HandleTypeDef;

#define AW9523_I2C_PORT             I2C_NUM_0
#define AW9523_I2C_SCL_GPIO         GPIO_NUM_14
#define AW9523_I2C_SDA_GPIO         GPIO_NUM_21
#define AW9523_I2C_HZ               400000
#define AW9523_RST_GPIO             GPIO_NUM_3

#ifndef AW9523_MCU_GPIO_OUT_RESET
#define AW9523_MCU_GPIO_OUT_RESET(port, pin) \
    do { \
        (void)(port); \
        gpio_set_level((gpio_num_t)(pin), 0); \
        vTaskDelay(pdMS_TO_TICKS(1)); \
    } while (0)
#endif

#ifndef AW9523_MCU_GPIO_OUT_SET
#define AW9523_MCU_GPIO_OUT_SET(port, pin) \
    do { \
        (void)(port); \
        gpio_set_level((gpio_num_t)(pin), 1); \
        vTaskDelay(pdMS_TO_TICKS(1)); \
    } while (0)
#endif

#ifndef AW9523_LOG_E
#define AW9523_LOG_E(fmt, ...)  ESP_LOGE("aw9523", fmt, ##__VA_ARGS__)
#endif

#ifndef EXIO_LOG_E
#define EXIO_LOG_E(fmt, ...)    ESP_LOGE("exio", fmt, ##__VA_ARGS__)
#endif
#ifndef EXIO_LOG_W
#define EXIO_LOG_W(fmt, ...)    ESP_LOGW("exio", fmt, ##__VA_ARGS__)
#endif
#ifndef EXIO_LOG_I
#define EXIO_LOG_I(fmt, ...)    ESP_LOGI("exio", fmt, ##__VA_ARGS__)
#endif
#ifndef EXIO_LOG_D
#define EXIO_LOG_D(fmt, ...)    ESP_LOGD("exio", fmt, ##__VA_ARGS__)
#endif
#ifndef EXIO_LOG_V
#define EXIO_LOG_V(fmt, ...)    ESP_LOGV("exio", fmt, ##__VA_ARGS__)
#endif

HAL_StatusTypeDef HAL_I2C_Master_Transmit(I2C_HandleTypeDef *hi2c, uint16_t DevAddress,
                                          uint8_t *pData, uint16_t Size, uint32_t Timeout);

esp_err_t aw9523_port_rst_gpio_init(void);
esp_err_t aw9523_port_i2c_bus_init(I2C_HandleTypeDef *hi2c);
void aw9523_port_i2c_scan(I2C_HandleTypeDef *hi2c);

#endif
