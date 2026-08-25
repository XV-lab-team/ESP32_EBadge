#ifndef __SDMMC_TUSB_MSC_H_
#define __SDMMC_TUSB_MSC_H_

#include "sdmmc_cmd.h"
#include "esp_err.h"


/**
 * @brief 启动 USB MSC 功能，使用已经初始化好的 SD 卡句柄
 *
 * @param card 已初始化的 SD 卡句柄
 * @return esp_err_t
 */
esp_err_t tusb_msc_sdmmc_start(sdmmc_card_t *card);

/**
 * @brief 停止 USB MSC 功能，卸载存储并清理资源
 *
 * @return esp_err_t
 */
esp_err_t tusb_msc_sdmmc_stop(void);




#endif