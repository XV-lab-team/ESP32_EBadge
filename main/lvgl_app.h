#ifndef LVGL_APP_H
#define LVGL_APP_H

#include "esp_err.h"

/* LVGL 盘符 S: 映射 VFS /sdcard/。例：lv_image_set_src(img, "S:photo.bin") */
#define LVGL_SD_DRIVE               "S:"

/* 启动 LVGL（官方 port 任务 + 全屏 FULL DMA flush + USB 模式设置页 + 屏上 sysmon）。
 * 调用前必须 lcd_init 成功。SD/FAT 应已在 app_main 里先挂上。
 */
esp_err_t lvgl_app_start(void);
void lvgl_sd_fs_refresh(void);
void lvgl_sd_fs_set_status(const char *text);

#endif
