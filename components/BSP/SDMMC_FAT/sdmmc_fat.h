#ifndef SDMMC_FAT_H
#define SDMMC_FAT_H

#include <stdbool.h>

#include "esp_err.h"
#include "sdmmc_cmd.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SDMMC_FAT_MOUNT_POINT       "/sdcard"

esp_err_t sdmmc_fat_mount(sdmmc_card_t *card, const char *base_path);
esp_err_t sdmmc_fat_unmount(void);
esp_err_t sdmmc_fat_start(void);
esp_err_t sdmmc_fat_stop(void);
bool sdmmc_fat_is_mounted(void);
const char *sdmmc_fat_mount_path(void);
sdmmc_card_t *sdmmc_fat_get_card(void);

#ifdef __cplusplus
}
#endif

#endif
