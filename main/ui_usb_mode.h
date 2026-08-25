#ifndef UI_USB_MODE_H
#define UI_USB_MODE_H

#include "esp_err.h"

/* USB 模式设置页。配置模式会开 TinyUSB MSC（暂时占用 USB PHY）。
 * ui_usb_mode_start() 调用前须已持有 lvgl_port 锁。
 * 上电自动 MSC 必须在 unlock 之后用 ui_usb_mode_kick_boot_msc()。
 */
esp_err_t ui_usb_mode_start(void);
void ui_usb_mode_apply_boot_override(void);
void ui_usb_mode_kick_boot_msc(void);

#endif
