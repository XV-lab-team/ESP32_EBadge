#include "sdmmc.h"
#include "esp_log.h"
#include "driver/sdmmc_host.h"
#include "sdmmc_cmd.h"
#include "esp_err.h"
#include <stdlib.h>
#include <string.h>

static const char *TAG = "sdmmc";

// 默认配置
static const sdmmc_config_t default_config = {
    .clk_pin = SDMMC_PIN_CLK,
    .cmd_pin = SDMMC_PIN_CMD,
    .d0_pin = SDMMC_PIN_D0,
    .d1_pin = SDMMC_PIN_D1,
    .d2_pin = SDMMC_PIN_D2,
    .d3_pin = SDMMC_PIN_D3,
    .cd_pin = SDMMC_PIN_CD,
    .bus_width = 1,                 
    .max_freq_khz =   40*1000,    //SDMMC 40M频率
    .internal_pullup = true,

};

esp_err_t sdmmc_init(const sdmmc_config_t *config, sdmmc_card_t **out_card) {
    esp_err_t ret;
    sdmmc_card_t *card = NULL;
    bool host_init = false;

    // 使用用户配置或默认配置
    const sdmmc_config_t *cfg = config ? config : &default_config;

    // 检查必须的引脚是否设置
    if (cfg->clk_pin == -1 || cfg->cmd_pin == -1 || cfg->d0_pin == -1) {
        ESP_LOGE(TAG, "Missing required pins: clk, cmd, d0");
        return ESP_ERR_INVALID_ARG;
    }

    // 总线宽度为4时检查其他引脚
    if (cfg->bus_width == 4 && (cfg->d1_pin == -1 || cfg->d2_pin == -1 || cfg->d3_pin == -1)) {
        ESP_LOGE(TAG, "4-bit width requires d1, d2, d3 pins");
        return ESP_ERR_INVALID_ARG;
    }

    // 配置 host
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.max_freq_khz = cfg->max_freq_khz;

    // 配置 slot
    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
    slot_config.clk = cfg->clk_pin;
    slot_config.cmd = cfg->cmd_pin;
    slot_config.d0 = cfg->d0_pin;
    slot_config.d1 = cfg->d1_pin;
    slot_config.d2 = cfg->d2_pin;
    slot_config.d3 = cfg->d3_pin;
    slot_config.gpio_cd = cfg->cd_pin;
    slot_config.gpio_wp = -1;
    slot_config.width = cfg->bus_width;

    // 内部上拉
    if (cfg->internal_pullup) {
        slot_config.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;
    }

    // 分配卡结构体
    card = (sdmmc_card_t *)malloc(sizeof(sdmmc_card_t));
    memset(card, 0, sizeof(sdmmc_card_t));

    if (!card) {
        ESP_LOGE(TAG, "Failed to allocate card structure");
        return ESP_ERR_NO_MEM;
    }

    // 初始化 host 驱动
    ret = host.init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Host init failed: %s", esp_err_to_name(ret));
        goto clean;
    }
    host_init = true;

    // 初始化 slot
    ret = sdmmc_host_init_slot(host.slot, &slot_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Slot init failed: %s", esp_err_to_name(ret));
        goto clean;
    }

    // 检测并初始化卡
    ret = sdmmc_card_init(&host, card);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Card init failed: %s", esp_err_to_name(ret));
        goto clean;
    }

    // 打印卡信息
    sdmmc_card_print_info(stdout, card);

    *out_card = card;
    return ESP_OK;

clean:
    if (host_init) {
        if (host.flags & SDMMC_HOST_FLAG_DEINIT_ARG) {
            host.deinit_p(host.slot);
        } else {
            host.deinit();
        }
    }
    if (card) {
        free(card);
    }
    return ret;
}

esp_err_t sdmmc_deinit(sdmmc_card_t *card) {
    if (!card) {
        return ESP_ERR_INVALID_ARG;
    }

    sdmmc_host_t host = card->host;

    // 反初始化 host
    if (host.flags & SDMMC_HOST_FLAG_DEINIT_ARG) {
        host.deinit_p(host.slot);
    } else {
        host.deinit();
    }

    free(card);
    return ESP_OK;
}