#include "sdmmc.h"

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/sdmmc_host.h"
#include "esp_log.h"
#include "sd_protocol_defs.h"
#include "sdmmc_cmd.h"

static const char *TAG = "sdmmc";

/* 20 MHz first; raise to 40 MHz after the card enumerates reliably. */
static const sdmmc_config_t default_config = {
    .clk_pin = SDMMC_PIN_CLK,
    .cmd_pin = SDMMC_PIN_CMD,
    .d0_pin = SDMMC_PIN_D0,
    .d1_pin = SDMMC_PIN_D1,
    .d2_pin = SDMMC_PIN_D2,
    .d3_pin = SDMMC_PIN_D3,
    .cd_pin = SDMMC_PIN_CD,
    .bus_width = 4,
    .max_freq_khz = 20 * 1000,
    .internal_pullup = true,
};

void sdmmc_log_card_info(const sdmmc_card_t *card)
{
    const char *type;
    uint64_t size_mb;

    if (card == NULL) {
        return;
    }

    if (card->is_sdio) {
        type = "SDIO";
    } else if (card->is_mmc) {
        type = "MMC";
    } else if ((card->ocr & SD_OCR_SDHC_CAP) == 0) {
        type = "SDSC";
    } else if (card->ocr & SD_OCR_S18_RA) {
        type = "SDHC/SDXC (UHS-I)";
    } else {
        type = "SDHC";
    }

    size_mb = ((uint64_t)card->csd.capacity) * card->csd.sector_size / (1024 * 1024);
    ESP_LOGI(TAG, "Name: %s", card->cid.name);
    ESP_LOGI(TAG, "Type: %s", type);
    ESP_LOGI(TAG, "Speed: %d kHz (limit %d kHz)%s",
             card->real_freq_khz, card->max_freq_khz, card->is_ddr ? ", DDR" : "");
    ESP_LOGI(TAG, "Size: %" PRIu64 " MB, bus_width=%u",
             size_mb,
             (unsigned)(1u << card->log_bus_width));
}

esp_err_t sdmmc_init(const sdmmc_config_t *config, sdmmc_card_t **out_card)
{
    esp_err_t ret;
    sdmmc_card_t *card = NULL;
    bool host_init = false;
    const sdmmc_config_t *cfg = config ? config : &default_config;
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();

    if (out_card == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_card = NULL;

    if (cfg->clk_pin == -1 || cfg->cmd_pin == -1 || cfg->d0_pin == -1) {
        ESP_LOGE(TAG, "Missing required pins: clk, cmd, d0");
        return ESP_ERR_INVALID_ARG;
    }
    if (cfg->bus_width == 4 &&
        (cfg->d1_pin == -1 || cfg->d2_pin == -1 || cfg->d3_pin == -1)) {
        ESP_LOGE(TAG, "4-bit width requires d1, d2, d3 pins");
        return ESP_ERR_INVALID_ARG;
    }

    host.max_freq_khz = cfg->max_freq_khz;

    slot_config.clk = cfg->clk_pin;
    slot_config.cmd = cfg->cmd_pin;
    slot_config.d0 = cfg->d0_pin;
    slot_config.d1 = cfg->d1_pin;
    slot_config.d2 = cfg->d2_pin;
    slot_config.d3 = cfg->d3_pin;
    slot_config.d4 = GPIO_NUM_NC;
    slot_config.d5 = GPIO_NUM_NC;
    slot_config.d6 = GPIO_NUM_NC;
    slot_config.d7 = GPIO_NUM_NC;
    slot_config.gpio_cd = cfg->cd_pin;
    slot_config.gpio_wp = GPIO_NUM_NC;
    slot_config.width = cfg->bus_width;
    if (cfg->internal_pullup) {
        slot_config.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;
    }

    card = (sdmmc_card_t *)malloc(sizeof(sdmmc_card_t));
    if (card == NULL) {
        ESP_LOGE(TAG, "Failed to allocate card structure");
        return ESP_ERR_NO_MEM;
    }
    memset(card, 0, sizeof(*card));

    ret = host.init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Host init failed: %s", esp_err_to_name(ret));
        goto clean;
    }
    host_init = true;

    ret = sdmmc_host_init_slot(host.slot, &slot_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Slot init failed: %s", esp_err_to_name(ret));
        goto clean;
    }

    ret = sdmmc_card_init(&host, card);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Card init failed: %s", esp_err_to_name(ret));
        goto clean;
    }

    sdmmc_log_card_info(card);
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
    free(card);
    return ret;
}

esp_err_t sdmmc_deinit(sdmmc_card_t *card)
{
    sdmmc_host_t host;

    if (card == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    host = card->host;
    if (host.flags & SDMMC_HOST_FLAG_DEINIT_ARG) {
        host.deinit_p(host.slot);
    } else {
        host.deinit();
    }
    free(card);
    return ESP_OK;
}
