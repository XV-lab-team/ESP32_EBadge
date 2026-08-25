#ifndef USB_MSC_H
#define USB_MSC_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    USB_MSC_OP_ENTER = 1,
    USB_MSC_OP_EXIT = 2,
} usb_msc_op_t;

/*
 * 配置模式：把已初始化的 SD 卡暴露为 USB MSC。
 * 会暂时抢走 GPIO19/20 的 USB PHY（电脑看不到 Serial/JTAG）。
 * 进出走 worker 任务，不要在 LVGL 任务里同步调用。
 * 退出后必须 usb_msc_request_exit()，把 PHY 还给 USB Serial/JTAG。
 * sdkconfig 保持 CONFIG_USJ_ENABLE_USB_SERIAL_JTAG=y，不烧 eFuse。
 */
esp_err_t usb_msc_request_enter(void);
esp_err_t usb_msc_request_exit(void);
int usb_msc_poll_result(usb_msc_op_t *op, esp_err_t *err);
int usb_msc_is_active(void);
int usb_msc_is_busy(void);
esp_err_t usb_msc_last_remount_err(void);

#ifdef __cplusplus
}
#endif

#endif
