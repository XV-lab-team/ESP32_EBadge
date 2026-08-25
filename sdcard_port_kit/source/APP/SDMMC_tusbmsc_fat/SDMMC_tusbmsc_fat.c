#include "SDMMC_tusbmsc_fat.h"
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_err.h"

#include "sdmmc.h"
#include "SDMMC_tusb_msc.h"     
#include "sdmmc_fat.h"          


#include "driver/sdmmc_host.h"
// #include "sdmmc_cmd.h"



#define TAG "SDMMC_TUSBMSC_FAT"

static sdmmc_card_t *card = NULL;
static uint8_t flag = 0; // 0: 未使用任何功能，1: 已使用 USB MSC 功能, 2: 已使用 SDMMC 功能

void SDMMC_tusbmsc_fat_init(void) {
    flag = 0; // 初始化标志
    esp_err_t ret = sdmmc_init(NULL, &card);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize SDMMC: %s", esp_err_to_name(ret));
        return;
    }
}

//开启USB MSC功能，使用已经初始化好的 SD 卡句柄
void SDMMC_tusbmsc_fat_SetTusbMsc(void){
    if(flag == 0)
    {   
        esp_err_t ret = tusb_msc_sdmmc_start(card);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to start USB MSC: %s", esp_err_to_name(ret));
            return;
        }
        flag = 1; // 设置为已使用 USB MSC 功能
    }
    else if(flag == 1)
    {
        ESP_LOGW(TAG, "USB MSC 已启动");
    }
    else if(flag == 2)
    {
        ESP_LOGW(TAG, "SDMMC 已启动   USB MSC 无法启动");
    }

}





