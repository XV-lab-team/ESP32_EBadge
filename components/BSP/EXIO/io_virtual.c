#include "io_virtual.h"

#include <string.h>

#include "esp_log.h"
#include "exio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

static const char *TAG = "vio";

#ifndef VIO_CMD_QUEUE_LEN
#define VIO_CMD_QUEUE_LEN  64
#endif
#ifndef VIO_CMD_SEND_MS
#define VIO_CMD_SEND_MS    20
#endif

#define IO_VIRTUAL_TASK_STACK_BYTES  4096
#define IO_VIRTUAL_TASK_PRIORITY     4

typedef enum {
    VIO_CMD_GPIO_SET = 0,
    VIO_CMD_LED_SET,
} vio_cmd_type_t;

typedef struct {
    vio_cmd_type_t type;
    uint8_t        id;
    uint8_t        val;
} vio_cmd_t;

static I2C_HandleTypeDef s_i2c;
static GPIO_TypeDef s_rst_port;
static exio_t s_exio_dev;
static QueueHandle_t s_vio_cmd_q;
static StaticQueue_t s_vio_cmd_static;
static uint8_t s_vio_cmd_storage[VIO_CMD_QUEUE_LEN * sizeof(vio_cmd_t)];
static volatile uint8_t s_dirty;

/*
 * One chip, AD1=1 AD0=1 -> 8-bit write address 0xB6 (7-bit 0x5B).
 * ledmode_isel=3 so GCR (P0 push-pull + 1/4 Imax) is written.
 * RGB LEDs are 20 mA max; isel=3 is ~9 mA at DIM=255. Do not change to 0 or 1.
 * Record: docs/AW9523虚拟IO移植记录-给接手AI.md (user 2026-08-24: keep this range).
 */
static const aw9523_dev aw9523_dev_cfg = {
    .hi2c          = &s_i2c,
    .rst_gpio_port = &s_rst_port,
    .rst_gpio_pin  = AW9523_RST_GPIO,
    .chip_num      = 1,
    .chip = {
        { .a0 = 1, .a1 = 1, .gpio_p0_mod_pp = 1, .ledmode_isel = 3 },
    }
};

/* No expander GPIO on this board. Unused slots stay 0 == AW9523_PIN_NULL. */
static const exio_gpio_pin_t exio_gpio_pin_cfg[64] = {0};

static const exio_led_pin_t exio_led_pin_cfg[64] = {
    { .pin = AW9523_PIN_1_7, .val_default = 0, .chip_num = 0, .desc = "R1" },
    { .pin = AW9523_PIN_1_6, .val_default = 0, .chip_num = 0, .desc = "G1" },
    { .pin = AW9523_PIN_1_4, .val_default = 0, .chip_num = 0, .desc = "B1" },
    { .pin = AW9523_PIN_1_3, .val_default = 0, .chip_num = 0, .desc = "R2" },
    { .pin = AW9523_PIN_0_0, .val_default = 0, .chip_num = 0, .desc = "G2" },
    { .pin = AW9523_PIN_0_3, .val_default = 0, .chip_num = 0, .desc = "B2" },
    { .pin = AW9523_PIN_0_1, .val_default = 0, .chip_num = 0, .desc = "R3" },
    { .pin = AW9523_PIN_0_2, .val_default = 0, .chip_num = 0, .desc = "G3" },
    { .pin = AW9523_PIN_0_4, .val_default = 0, .chip_num = 0, .desc = "B3" },
    { .pin = AW9523_PIN_0_6, .val_default = 0, .chip_num = 0, .desc = "R4" },
    { .pin = AW9523_PIN_0_5, .val_default = 0, .chip_num = 0, .desc = "G4" },
    { .pin = AW9523_PIN_0_7, .val_default = 0, .chip_num = 0, .desc = "B4" },
    { .pin = AW9523_PIN_1_0, .val_default = 0, .chip_num = 0, .desc = "R5" },
    { .pin = AW9523_PIN_1_1, .val_default = 0, .chip_num = 0, .desc = "G5" },
    { .pin = AW9523_PIN_1_2, .val_default = 0, .chip_num = 0, .desc = "B5" },
};

static HAL_StatusTypeDef io_virtual_cmd_send(vio_cmd_type_t type, uint8_t id, uint8_t val)
{
    if (s_vio_cmd_q == NULL) {
        return HAL_ERROR;
    }

    vio_cmd_t cmd = {
        .type = type,
        .id   = id,
        .val  = val,
    };

    if (xQueueSend(s_vio_cmd_q, &cmd, pdMS_TO_TICKS(VIO_CMD_SEND_MS)) != pdPASS) {
        ESP_LOGW(TAG, "cmd queue full type=%u id=%u val=%u",
                 (unsigned)type, (unsigned)id, (unsigned)val);
        return HAL_BUSY;
    }

    return HAL_OK;
}

static void io_virtual_handle_cmd(const vio_cmd_t *cmd)
{
    HAL_StatusTypeDef res = HAL_OK;

    if (cmd->type == VIO_CMD_GPIO_SET) {
        if (cmd->id >= VIO_GPIO_NUM) {
            return;
        }

        const exio_gpio_pin_t *pin = &s_exio_dev.exio_gpio_pin[cmd->id];
        if (pin->pin == AW9523_PIN_NULL) {
            return;
        }

        res = aw9523_set_pin_gpioval(&s_exio_dev.aw9523_dev, pin->chip_num, pin->pin, cmd->val ? 1 : 0);
    } else if (cmd->type == VIO_CMD_LED_SET) {
        if (cmd->id >= VIO_LED_NUM) {
            return;
        }

        const exio_led_pin_t *pin = &s_exio_dev.exio_led_pin[cmd->id];
        if (pin->pin == AW9523_PIN_NULL) {
            return;
        }

        res = aw9523_set_pin_ledval(&s_exio_dev.aw9523_dev, pin->chip_num, pin->pin, cmd->val);
    } else {
        return;
    }

    if (res == HAL_OK) {
        s_dirty = 1;
    }
}

HAL_StatusTypeDef io_virtual_gpio_set(vio_gpio_id_t id, uint8_t level)
{
    if (id >= VIO_GPIO_NUM) {
        return HAL_ERROR;
    }

    return io_virtual_cmd_send(VIO_CMD_GPIO_SET, (uint8_t)id, level ? 1 : 0);
}

HAL_StatusTypeDef io_virtual_led_set(vio_led_id_t id, uint8_t brightness)
{
    if (id >= VIO_LED_NUM) {
        return HAL_ERROR;
    }

    return io_virtual_cmd_send(VIO_CMD_LED_SET, (uint8_t)id, brightness);
}

HAL_StatusTypeDef io_virtual_rgb_set(uint8_t rgb_1_to_5, uint8_t r, uint8_t g, uint8_t b)
{
    vio_led_id_t base;
    HAL_StatusTypeDef res;

    if (rgb_1_to_5 < 1 || rgb_1_to_5 > 5) {
        return HAL_ERROR;
    }

    base = (vio_led_id_t)((rgb_1_to_5 - 1) * 3);
    res = io_virtual_led_set(base, r);
    if (res != HAL_OK) {
        return res;
    }
    res = io_virtual_led_set((vio_led_id_t)(base + 1), g);
    if (res != HAL_OK) {
        return res;
    }
    return io_virtual_led_set((vio_led_id_t)(base + 2), b);
}

static void io_virtual_task(void *arg)
{
    (void)arg;

    if (aw9523_port_i2c_bus_init(&s_i2c) != ESP_OK) {
        ESP_LOGE(TAG, "I2C bus init failed");
        vTaskDelete(NULL);
        return;
    }

    s_vio_cmd_q = xQueueCreateStatic(VIO_CMD_QUEUE_LEN, sizeof(vio_cmd_t),
                                     s_vio_cmd_storage, &s_vio_cmd_static);
    if (s_vio_cmd_q == NULL) {
        ESP_LOGE(TAG, "cmd queue create failed");
        vTaskDelete(NULL);
        return;
    }

    memset(&s_exio_dev, 0, sizeof(exio_t));
    memcpy(&s_exio_dev.aw9523_dev,    &aw9523_dev_cfg,    sizeof(aw9523_dev_cfg));
    memcpy(&s_exio_dev.exio_gpio_pin, &exio_gpio_pin_cfg, sizeof(exio_gpio_pin_cfg));
    memcpy(&s_exio_dev.exio_led_pin,  &exio_led_pin_cfg,  sizeof(exio_led_pin_cfg));

    {
        HAL_StatusTypeDef res = exio_init(&s_exio_dev);
        if (res == HAL_OK) {
            ESP_LOGI(TAG, "exio_init ok");
        } else {
            ESP_LOGE(TAG, "exio_init failed res=%d", (int)res);
        }
        aw9523_port_i2c_scan(&s_i2c);
    }

    for (;;) {
        vio_cmd_t cmd;

        if (xQueueReceive(s_vio_cmd_q, &cmd, portMAX_DELAY) != pdPASS) {
            continue;
        }

        io_virtual_handle_cmd(&cmd);

        while (xQueueReceive(s_vio_cmd_q, &cmd, 0) == pdPASS) {
            io_virtual_handle_cmd(&cmd);
        }

        if (s_dirty) {
            exio_apply(&s_exio_dev);
            s_dirty = 0;
        }
    }
}

esp_err_t io_virtual_start(void)
{
    esp_err_t err = aw9523_port_rst_gpio_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "RST GPIO init failed: %s", esp_err_to_name(err));
        return err;
    }

    if (xTaskCreate(io_virtual_task, "io_virtual", IO_VIRTUAL_TASK_STACK_BYTES,
                    NULL, IO_VIRTUAL_TASK_PRIORITY, NULL) != pdPASS) {
        ESP_LOGE(TAG, "xTaskCreate(io_virtual) failed");
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
