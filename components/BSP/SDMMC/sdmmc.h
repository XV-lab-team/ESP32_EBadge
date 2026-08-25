#ifndef SDMMC_H
#define SDMMC_H

#include <stdbool.h>

#include "esp_err.h"
#include "sdmmc_cmd.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Board SDMMC 4-bit pins (user 2026-08-25). Do not copy the port-kit defaults. */
#define SDMMC_PIN_CD                1
#define SDMMC_PIN_D2                2
#define SDMMC_PIN_D3                42
#define SDMMC_PIN_CMD               41
#define SDMMC_PIN_CLK               40
#define SDMMC_PIN_D0                39
#define SDMMC_PIN_D1                38

typedef struct {
    int clk_pin;
    int cmd_pin;
    int d0_pin;
    int d1_pin;
    int d2_pin;
    int d3_pin;
    int cd_pin;
    int bus_width;
    int max_freq_khz;
    bool internal_pullup;
} sdmmc_config_t;

esp_err_t sdmmc_init(const sdmmc_config_t *config, sdmmc_card_t **out_card);
esp_err_t sdmmc_deinit(sdmmc_card_t *card);
void sdmmc_log_card_info(const sdmmc_card_t *card);

#ifdef __cplusplus
}
#endif

#endif
