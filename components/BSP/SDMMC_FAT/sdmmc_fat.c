#include "sdmmc_fat.h"

#include <string.h>

#include "diskio_impl.h"
#include "diskio_sdmmc.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "ff.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "sdmmc.h"

static const char *TAG = "sdmmc_fat";

static sdmmc_card_t *s_card;
static bool s_owns_card;
static bool s_mounted;
static BYTE s_pdrv = FF_DRV_NOT_USED;
static char s_mount_path[32] = SDMMC_FAT_MOUNT_POINT;
static char s_drv[3];
static SemaphoreHandle_t s_lock;

static void fat_ensure_lock(void)
{
    if (s_lock == NULL) {
        s_lock = xSemaphoreCreateMutex();
    }
}

static int fat_lock(void)
{
    fat_ensure_lock();
    if (s_lock == NULL) {
        return 0;
    }
    return xSemaphoreTake(s_lock, portMAX_DELAY) == pdTRUE;
}

static void fat_unlock(void)
{
    if (s_lock != NULL) {
        xSemaphoreGive(s_lock);
    }
}

static esp_err_t sdmmc_fat_unmount_unlocked(void)
{
    FRESULT fres;

    if (!s_mounted) {
        return ESP_ERR_INVALID_STATE;
    }

    fres = f_mount(NULL, s_drv, 0);
    if (fres != FR_OK) {
        ESP_LOGE(TAG, "f_mount(NULL) failed (%d), keep mounted", (int)fres);
        return ESP_FAIL;
    }
    esp_vfs_fat_unregister_path(s_mount_path);
    ff_diskio_unregister(s_pdrv);
    s_mounted = false;
    s_pdrv = FF_DRV_NOT_USED;
    ESP_LOGI(TAG, "FAT unmounted from %s", s_mount_path);
    return ESP_OK;
}

static esp_err_t sdmmc_fat_mount_unlocked(sdmmc_card_t *card, const char *base_path)
{
    esp_err_t err;
    FATFS *fs = NULL;
    FRESULT fres;
    BYTE pdrv = FF_DRV_NOT_USED;
    esp_vfs_fat_conf_t conf;

    if (card == NULL || base_path == NULL || base_path[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_mounted) {
        return ESP_ERR_INVALID_STATE;
    }
    if (strlen(base_path) >= sizeof(s_mount_path)) {
        return ESP_ERR_INVALID_ARG;
    }

    err = ff_diskio_get_drive(&pdrv);
    if (err != ESP_OK || pdrv == FF_DRV_NOT_USED) {
        ESP_LOGE(TAG, "no free FATFS volume");
        return ESP_ERR_NO_MEM;
    }

    ff_diskio_register_sdmmc(pdrv, card);
    s_drv[0] = (char)('0' + pdrv);
    s_drv[1] = ':';
    s_drv[2] = '\0';

    conf.base_path = base_path;
    conf.fat_drive = s_drv;
    conf.max_files = 8;
    err = esp_vfs_fat_register_cfg(&conf, &fs);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_vfs_fat_register_cfg: %s", esp_err_to_name(err));
        ff_diskio_unregister(pdrv);
        return err;
    }

    fres = f_mount(fs, s_drv, 1);
    if (fres != FR_OK) {
        ESP_LOGE(TAG, "f_mount failed (%d); card present but not a usable FAT volume", (int)fres);
        f_mount(NULL, s_drv, 0);
        esp_vfs_fat_unregister_path(base_path);
        ff_diskio_unregister(pdrv);
        return ESP_FAIL;
    }

    strncpy(s_mount_path, base_path, sizeof(s_mount_path) - 1);
    s_mount_path[sizeof(s_mount_path) - 1] = '\0';
    s_card = card;
    s_pdrv = pdrv;
    s_mounted = true;
    ESP_LOGI(TAG, "FAT mounted at %s", s_mount_path);
    return ESP_OK;
}

esp_err_t sdmmc_fat_mount(sdmmc_card_t *card, const char *base_path)
{
    esp_err_t err;

    if (!fat_lock()) {
        return ESP_ERR_NO_MEM;
    }
    err = sdmmc_fat_mount_unlocked(card, base_path);
    fat_unlock();
    return err;
}

esp_err_t sdmmc_fat_unmount(void)
{
    esp_err_t err;

    if (!fat_lock()) {
        return ESP_ERR_NO_MEM;
    }
    err = sdmmc_fat_unmount_unlocked();
    fat_unlock();
    return err;
}

esp_err_t sdmmc_fat_start(void)
{
    esp_err_t err;
    sdmmc_card_t *card = NULL;

    if (!fat_lock()) {
        return ESP_ERR_NO_MEM;
    }
    if (s_mounted || s_card != NULL) {
        fat_unlock();
        return ESP_ERR_INVALID_STATE;
    }
    fat_unlock();

    err = sdmmc_init(NULL, &card);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "sdmmc_init: %s", esp_err_to_name(err));
        return err;
    }

    if (!fat_lock()) {
        sdmmc_deinit(card);
        return ESP_ERR_NO_MEM;
    }
    err = sdmmc_fat_mount_unlocked(card, SDMMC_FAT_MOUNT_POINT);
    if (err != ESP_OK) {
        fat_unlock();
        sdmmc_deinit(card);
        return err;
    }

    s_owns_card = true;
    fat_unlock();
    return ESP_OK;
}

esp_err_t sdmmc_fat_stop(void)
{
    esp_err_t err = ESP_OK;
    sdmmc_card_t *card;
    bool owns;

    if (!fat_lock()) {
        return ESP_ERR_NO_MEM;
    }
    if (s_mounted) {
        err = sdmmc_fat_unmount_unlocked();
    }
    card = s_card;
    owns = s_owns_card;
    s_card = NULL;
    s_owns_card = false;
    fat_unlock();

    if (owns && card != NULL) {
        sdmmc_deinit(card);
    }
    return err;
}

bool sdmmc_fat_is_mounted(void)
{
    bool mounted;

    if (!fat_lock()) {
        return false;
    }
    mounted = s_mounted;
    fat_unlock();
    return mounted;
}

const char *sdmmc_fat_mount_path(void)
{
    return s_mount_path;
}

sdmmc_card_t *sdmmc_fat_get_card(void)
{
    sdmmc_card_t *card;

    if (!fat_lock()) {
        return NULL;
    }
    card = s_card;
    fat_unlock();
    return card;
}
