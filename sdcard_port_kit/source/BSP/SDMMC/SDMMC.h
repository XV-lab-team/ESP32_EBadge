#ifndef __SDMMC_H_
#define __SDMMC_H_



/**
 * @file sdmmc.h
 * @brief SDMMC 卡初始化与反初始化接口，提供统一句柄供其他模块使用
 */

#pragma once

#include "sdmmc_cmd.h"
#include "driver/sdmmc_host.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif


#define SDMMC_PIN_CD   8
#define SDMMC_PIN_CLK  16
#define SDMMC_PIN_CMD  15
#define SDMMC_PIN_D0   17
#define SDMMC_PIN_D1   18
#define SDMMC_PIN_D2   6
#define SDMMC_PIN_D3   7


//SDMMC 配置参数结构体
typedef struct {
    int clk_pin;           //!< 时钟引脚
    int cmd_pin;           //!< 命令引脚
    int d0_pin;            //!< 数据引脚 D0
    int d1_pin;            //!< 数据引脚 D1
    int d2_pin;            //!< 数据引脚 D2
    int d3_pin;            //!< 数据引脚 D3
    int cd_pin;            //!< 卡检测引脚
    int bus_width;         //!< 总线宽度：1 或 4
    int max_freq_khz;      //!< 最大时钟频率(kHz)，范围 400 ~ 40000，默认 20000 (20MHz)
    bool internal_pullup;  //!< 是否启用内部上拉电阻
} sdmmc_config_t;

/**
 * @brief 初始化 SDMMC 卡
 *
 * @param config 配置参数指针，若为 NULL 则使用默认配置
 * @param out_card 输出卡句柄指针
 * @return esp_err_t
 */
esp_err_t sdmmc_init(const sdmmc_config_t *config, sdmmc_card_t **out_card);

/**
 * @brief 反初始化 SDMMC 卡
 *
 * @param card 卡句柄
 * @return esp_err_t
 */
esp_err_t sdmmc_deinit(sdmmc_card_t *card);

#ifdef __cplusplus
}
#endif



#endif


