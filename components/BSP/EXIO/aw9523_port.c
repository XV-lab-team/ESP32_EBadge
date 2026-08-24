#include "aw9523_port.h"

#include <string.h>

static const char *TAG = "aw9523_port";

esp_err_t aw9523_port_rst_gpio_init(void)
{
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << AW9523_RST_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&io);
    if (err != ESP_OK) {
        return err;
    }
    /* Hold AW9523 in reset until aw9523_init pulses RSTN. */
    gpio_set_level(AW9523_RST_GPIO, 0);
    return ESP_OK;
}

esp_err_t aw9523_port_i2c_bus_init(I2C_HandleTypeDef *hi2c)
{
    if (hi2c == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(hi2c, 0, sizeof(*hi2c));

    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = AW9523_I2C_PORT,
        .sda_io_num = AW9523_I2C_SDA_GPIO,
        .scl_io_num = AW9523_I2C_SCL_GPIO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };

    esp_err_t err = i2c_new_master_bus(&bus_cfg, &hi2c->bus);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2c_new_master_bus failed: %s", esp_err_to_name(err));
        return err;
    }

    return ESP_OK;
}

void aw9523_port_i2c_scan(I2C_HandleTypeDef *hi2c)
{
    uint8_t addr;

    if (hi2c == NULL || hi2c->bus == NULL) {
        return;
    }

    /* AD1=AD0=1 -> 7-bit 0x5B. Call after RSTN is released. */
    for (addr = 0x58; addr <= 0x5B; addr++) {
        esp_err_t err = i2c_master_probe(hi2c->bus, addr, 50);
        ESP_LOGI(TAG, "I2C 0x%02x %s", addr, (err == ESP_OK) ? "ACK" : "nack");
    }
}

HAL_StatusTypeDef HAL_I2C_Master_Transmit(I2C_HandleTypeDef *hi2c, uint16_t DevAddress,
                                          uint8_t *pData, uint16_t Size, uint32_t Timeout)
{
    uint8_t idx;

    if (hi2c == NULL || hi2c->bus == NULL || pData == NULL) {
        return HAL_ERROR;
    }
    if (DevAddress < 0xB0 || DevAddress > 0xB6 || ((DevAddress & 0x01) != 0)) {
        return HAL_ERROR;
    }

    idx = (uint8_t)((DevAddress - 0xB0) >> 1);
    if (idx >= 4) {
        return HAL_ERROR;
    }

    if (hi2c->dev[idx] == NULL) {
        i2c_device_config_t dev_cfg = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = (uint16_t)(DevAddress >> 1),
            .scl_speed_hz = AW9523_I2C_HZ,
        };
        if (i2c_master_bus_add_device(hi2c->bus, &dev_cfg, &hi2c->dev[idx]) != ESP_OK) {
            return HAL_ERROR;
        }
    }

    if (i2c_master_transmit(hi2c->dev[idx], pData, Size, (int)Timeout) != ESP_OK) {
        return HAL_ERROR;
    }
    return HAL_OK;
}
