#include "ui_usb_mode.h"

#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "key.h"
#include "lcd.h"
#include "lvgl.h"
#include "lvgl_app.h"
#include "nvs.h"
#include "usb_msc.h"

static const char *TAG = "ui_usb";

/* 界面主字库：微软雅黑 16px 4bpp 子集（main/gen_ui_font.cs）。缺字再 fallback 到自带 CJK。 */
LV_FONT_DECLARE(ui_font_zh_16)
#define UI_FONT                    (&ui_font_zh_16)

#define UI_SAFE                    46
#define UI_TOAST_MS                1600
#define UI_BODY_W                  (LCD_H_RES - (UI_SAFE * 2))
#define UI_BODY_H                  (LCD_V_RES - (UI_SAFE * 2) - 20)
#define UI_NVS_NS                  "usbui"
#define UI_NVS_KEY                 "mode"
#define UI_KEY_QUEUE_LEN           16

typedef enum {
    UI_SCR_SETTINGS = 0,
    UI_SCR_CONFIRM_ENTER,
    UI_SCR_CONFIG_LIVE,
    UI_SCR_CONFIRM_EXIT,
} ui_screen_t;

typedef enum {
    UI_USB_NORMAL = 0,
    UI_USB_CONFIG,
} ui_usb_mode_t;

typedef struct {
    key_id_t id;
    key_event_t evt;
} ui_key_msg_t;

static ui_screen_t s_screen;
static ui_usb_mode_t s_mode;
static uint8_t s_focus;
static lv_obj_t *s_body;
static lv_obj_t *s_hint;
static lv_obj_t *s_toast;
static lv_timer_t *s_toast_timer;
static QueueHandle_t s_key_q;
static uint8_t s_msc_ok;
static uint8_t s_boot_msc;
static uint8_t s_boot_forced_normal;

static void ui_rebuild(void);
static void ui_show_toast(const char *text);

static lv_color_t ui_col_muted(void)
{
    return lv_color_hex(0x9AA0A6);
}

static lv_color_t ui_col_accent(void)
{
    return lv_color_hex(0x3B82F6);
}

static lv_color_t ui_col_focus_bg(void)
{
    return lv_color_hex(0x1A2332);
}

static void ui_nvs_load(void)
{
    nvs_handle_t h;
    uint8_t mode = UI_USB_NORMAL;
    esp_err_t err = nvs_open(UI_NVS_NS, NVS_READONLY, &h);
    if (err != ESP_OK) {
        s_mode = UI_USB_NORMAL;
        return;
    }
    err = nvs_get_u8(h, UI_NVS_KEY, &mode);
    nvs_close(h);
    s_mode = (err == ESP_OK && mode == UI_USB_CONFIG) ? UI_USB_CONFIG : UI_USB_NORMAL;
}

static void ui_nvs_save(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(UI_NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "nvs_open 失败: %s", esp_err_to_name(err));
        return;
    }
    err = nvs_set_u8(h, UI_NVS_KEY, (uint8_t)s_mode);
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "nvs 保存失败: %s", esp_err_to_name(err));
    }
}

static void ui_style_label(lv_obj_t *label, lv_color_t color, uint8_t center)
{
    lv_obj_set_style_text_font(label, UI_FONT, 0);
    lv_obj_set_style_text_color(label, color, 0);
    if (center) {
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    }
}

static lv_obj_t *ui_title(lv_obj_t *parent, const char *text)
{
    lv_obj_t *label = lv_label_create(parent);
    ui_style_label(label, lv_color_white(), 1);
    lv_label_set_text(label, text);
    lv_obj_set_width(label, lv_pct(100));
    return label;
}

static lv_obj_t *ui_sub(lv_obj_t *parent, const char *text)
{
    lv_obj_t *label = lv_label_create(parent);
    ui_style_label(label, ui_col_muted(), 1);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(label, lv_pct(100));
    lv_label_set_text(label, text);
    return label;
}

static lv_obj_t *ui_row(lv_obj_t *parent, uint8_t focused, uint8_t current,
                        const char *title, const char *sub)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_size(row, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(row, focused ? ui_col_focus_bg() : lv_color_hex(0x141414), 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(row, 2, 0);
    lv_obj_set_style_border_color(row, focused ? ui_col_accent() : lv_color_hex(0x141414), 0);
    lv_obj_set_style_radius(row, 12, 0);
    lv_obj_set_style_pad_ver(row, 8, 0);
    lv_obj_set_style_pad_hor(row, 12, 0);
    lv_obj_set_style_pad_row(row, 2, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_COLUMN);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *head = lv_obj_create(row);
    lv_obj_remove_style_all(head);
    lv_obj_set_size(head, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(head, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(head, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(head, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *t = lv_label_create(head);
    ui_style_label(t, lv_color_white(), 0);
    lv_label_set_text(t, title);

    if (current) {
        lv_obj_t *pill = lv_label_create(head);
        lv_label_set_text(pill, "当前");
        lv_obj_set_style_text_font(pill, UI_FONT, 0);
        lv_obj_set_style_text_color(pill, lv_color_white(), 0);
        lv_obj_set_style_bg_color(pill, ui_col_accent(), 0);
        lv_obj_set_style_bg_opa(pill, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(pill, 8, 0);
        lv_obj_set_style_pad_hor(pill, 6, 0);
        lv_obj_set_style_pad_ver(pill, 1, 0);
    }

    lv_obj_t *s = lv_label_create(row);
    ui_style_label(s, ui_col_muted(), 0);
    lv_label_set_long_mode(s, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s, lv_pct(100));
    lv_label_set_text(s, sub);
    return row;
}

static void ui_build_settings(void)
{
    ui_title(s_body, "USB 模式");
    ui_sub(s_body, s_mode == UI_USB_NORMAL ? "现为下载 / 调试" : "现为 U 盘");
    ui_row(s_body, s_focus == 0, s_mode == UI_USB_NORMAL, "正常模式", "USB = 下载 + 日志");
    ui_row(s_body, s_focus == 1, s_mode == UI_USB_CONFIG, "配置模式", "USB = U 盘 · CDC 稍后");
    lv_label_set_text(s_hint, "上 / 下选择 · 确定切换");
}

static void ui_build_confirm_enter(void)
{
    ui_title(s_body, "进入配置？");
    ui_sub(s_body, "USB 会变成 U 盘。电脑将暂时没有下载口。CDC 通信这次不做。");
    ui_row(s_body, s_focus == 0, 0, "取消", "留在正常模式");
    ui_row(s_body, s_focus == 1, 0, "进入", "占用 USB PHY");
    lv_label_set_text(s_hint, "默认停在取消 · 长按确定返回");
}

static void ui_build_config_live(void)
{
    ui_title(s_body, "配置模式");

    lv_obj_t *icon = lv_obj_create(s_body);
    lv_obj_set_size(icon, 52, 64);
    lv_obj_set_style_bg_opa(icon, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(icon, 2, 0);
    lv_obj_set_style_border_color(icon, ui_col_accent(), 0);
    lv_obj_set_style_radius(icon, 6, 0);
    lv_obj_set_style_pad_all(icon, 8, 0);
    lv_obj_remove_flag(icon, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *win = lv_obj_create(icon);
    lv_obj_set_size(win, lv_pct(100), 16);
    lv_obj_set_style_bg_color(win, lv_color_hex(0x2A3344), 0);
    lv_obj_set_style_bg_opa(win, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(win, 0, 0);
    lv_obj_set_style_radius(win, 2, 0);
    lv_obj_align(win, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_remove_flag(win, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *ready = lv_label_create(s_body);
    ui_style_label(ready, lv_color_white(), 1);
    lv_obj_set_width(ready, lv_pct(100));
    if (usb_msc_is_busy()) {
        lv_label_set_text(ready, s_msc_ok ? "正在退出 U 盘" : "U 盘启动中");
    } else {
        lv_label_set_text(ready, s_msc_ok ? "U 盘已就绪" : "U 盘未启动");
    }

    if (s_msc_ok) {
        ui_sub(s_body, "请在电脑打开可移动磁盘");
        ui_sub(s_body, "退出前请先弹出 U 盘");
    } else if (usb_msc_is_busy()) {
        ui_sub(s_body, "正在切换 USB，请稍候");
        ui_sub(s_body, "CDC 通信 · 暂未启用");
    } else {
        ui_sub(s_body, "没有 SD 卡，或 USB 切换失败");
        ui_sub(s_body, "下载口应仍可用。长按确定退出");
    }
    lv_label_set_text(s_hint, "长按确定退出");
}

static void ui_build_confirm_exit(void)
{
    ui_title(s_body, "退出配置？");
    ui_sub(s_body, "请先在电脑弹出 U 盘，否则可能损坏文件。USB 会恢复下载口。");
    ui_row(s_body, s_focus == 0, 0, "取消", "继续当 U 盘");
    ui_row(s_body, s_focus == 1, 0, "退出", "恢复下载 / 调试");
    lv_label_set_text(s_hint, "默认停在取消 · 长按确定返回");
}

static void ui_rebuild(void)
{
    lv_obj_clean(s_body);
    if (s_screen == UI_SCR_SETTINGS) {
        ui_build_settings();
    } else if (s_screen == UI_SCR_CONFIRM_ENTER) {
        ui_build_confirm_enter();
    } else if (s_screen == UI_SCR_CONFIG_LIVE) {
        ui_build_config_live();
    } else {
        ui_build_confirm_exit();
    }
}

static void ui_toast_hide(lv_timer_t *t)
{
    (void)t;
    if (s_toast) {
        lv_obj_add_flag(s_toast, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_toast_timer) {
        lv_timer_pause(s_toast_timer);
    }
}

static void ui_show_toast(const char *text)
{
    lv_label_set_text(s_toast, text);
    lv_obj_remove_flag(s_toast, LV_OBJ_FLAG_HIDDEN);
    if (s_toast_timer) {
        lv_timer_reset(s_toast_timer);
        lv_timer_resume(s_toast_timer);
    }
}

static void ui_on_msc_result(usb_msc_op_t op, esp_err_t err)
{
    if (op == USB_MSC_OP_ENTER) {
        if (err == ESP_OK) {
            s_mode = UI_USB_CONFIG;
            s_msc_ok = 1;
            s_screen = UI_SCR_CONFIG_LIVE;
            s_focus = 0;
            ui_nvs_save();
            lvgl_sd_fs_set_status("sd usb");
            ESP_LOGW(TAG, "配置模式：SD 已交给电脑。长按确定退出可恢复 Serial/JTAG。上电按住向上键可强制正常模式。");
        } else {
            s_msc_ok = 0;
            s_mode = UI_USB_NORMAL;
            s_screen = UI_SCR_SETTINGS;
            s_focus = 1;
            ui_nvs_save();
            if (err == ESP_ERR_NOT_FOUND) {
                ui_show_toast("没有 SD 卡，无法当 U 盘");
                ESP_LOGE(TAG, "无 SD 卡，未切换 USB PHY");
            } else if (err == ESP_ERR_TIMEOUT) {
                ui_show_toast("LED 脚本未停，无法当 U 盘");
                ESP_LOGE(TAG, "usb_msc_enter: LED 脚本 stop 超时");
            } else {
                ui_show_toast("U 盘启动失败");
                ESP_LOGE(TAG, "usb_msc_enter: %s", esp_err_to_name(err));
            }
        }
        ui_rebuild();
        return;
    }

    s_msc_ok = 0;
    s_mode = UI_USB_NORMAL;
    s_screen = UI_SCR_SETTINGS;
    s_focus = 0;
    ui_nvs_save();
    lvgl_sd_fs_refresh();
    if (err != ESP_OK) {
        ui_show_toast("下载口恢复失败，请拔插 USB");
        ESP_LOGE(TAG, "usb_msc_exit: %s", esp_err_to_name(err));
    } else if (usb_msc_last_remount_err() != ESP_OK) {
        ui_show_toast("SD 重新挂载失败");
        ESP_LOGW(TAG, "FAT remount: %s", esp_err_to_name(usb_msc_last_remount_err()));
    } else {
        ESP_LOGI(TAG, "已回到正常模式；USB PHY 应已还给 Serial/JTAG");
    }
    ui_rebuild();
}

static void ui_enter_config(void)
{
    esp_err_t err = usb_msc_request_enter();

    if (err != ESP_OK) {
        ui_show_toast("正在切换 USB，请稍候");
        return;
    }
    s_msc_ok = 0;
    s_screen = UI_SCR_CONFIG_LIVE;
    s_focus = 0;
    ui_show_toast("正在启动 U 盘");
}

static void ui_exit_config(void)
{
    esp_err_t err = usb_msc_request_exit();

    if (err != ESP_OK) {
        ui_show_toast("正在切换 USB，请稍候");
        return;
    }
    ui_show_toast("请稍候，正在恢复下载口");
}

static void ui_apply_key(key_id_t id, key_event_t evt)
{
    const uint8_t is_long = (evt == KEY_EVT_LONG_PRESS) && (id == KEY_ENTER);
    const uint8_t is_click = (evt == KEY_EVT_CLICK);

    if (!is_long && !is_click) {
        return;
    }
    if (usb_msc_is_busy()) {
        ui_show_toast("正在切换 USB，请稍候");
        return;
    }

    if (s_screen == UI_SCR_SETTINGS) {
        if (is_click && (id == KEY_UP || id == KEY_DOWN)) {
            s_focus = (uint8_t)(s_focus ? 0 : 1);
            ui_rebuild();
            return;
        }
        if (is_long) {
            ui_show_toast("上级菜单尚未做，长按确定以后用来返回");
            return;
        }
        if (is_click && id == KEY_ENTER) {
            if (s_focus == 0) {
                if (s_mode == UI_USB_NORMAL) {
                    ui_show_toast("已是正常模式");
                } else {
                    s_screen = UI_SCR_CONFIRM_EXIT;
                    s_focus = 0;
                    ui_rebuild();
                }
                return;
            }
            if (s_mode == UI_USB_CONFIG) {
                ui_show_toast("已是配置模式");
            } else {
                s_screen = UI_SCR_CONFIRM_ENTER;
                s_focus = 0;
                ui_rebuild();
            }
        }
        return;
    }

    if (s_screen == UI_SCR_CONFIRM_ENTER) {
        if (is_click && (id == KEY_UP || id == KEY_DOWN)) {
            s_focus = (uint8_t)(s_focus ? 0 : 1);
            ui_rebuild();
            return;
        }
        if (is_long || (is_click && id == KEY_ENTER && s_focus == 0)) {
            s_screen = UI_SCR_SETTINGS;
            s_focus = 1;
            ui_rebuild();
            return;
        }
        if (is_click && id == KEY_ENTER && s_focus == 1) {
            ui_enter_config();
            ui_rebuild();
        }
        return;
    }

    if (s_screen == UI_SCR_CONFIG_LIVE) {
        if (is_long) {
            s_screen = UI_SCR_CONFIRM_EXIT;
            s_focus = 0;
            ui_rebuild();
            return;
        }
        if (is_click && id == KEY_ENTER) {
            ui_show_toast("单击无作用，长按确定退出");
        }
        return;
    }

    if (is_click && (id == KEY_UP || id == KEY_DOWN)) {
        s_focus = (uint8_t)(s_focus ? 0 : 1);
        ui_rebuild();
        return;
    }
    if (is_long || (is_click && id == KEY_ENTER && s_focus == 0)) {
        s_screen = UI_SCR_CONFIG_LIVE;
        s_focus = 0;
        ui_rebuild();
        return;
    }
    if (is_click && id == KEY_ENTER && s_focus == 1) {
        ui_exit_config();
        ui_rebuild();
    }
}

static void ui_key_timer(lv_timer_t *t)
{
    ui_key_msg_t msg;
    usb_msc_op_t op;
    esp_err_t err;

    (void)t;
    if (usb_msc_poll_result(&op, &err)) {
        ui_on_msc_result(op, err);
    }
    if (s_key_q == NULL) {
        return;
    }
    while (xQueueReceive(s_key_q, &msg, 0) == pdTRUE) {
        ui_apply_key(msg.id, msg.evt);
    }
}

static void ui_key_cb(key_id_t id, key_event_t evt)
{
    ui_key_msg_t msg = { .id = id, .evt = evt };

    if (s_key_q == NULL) {
        return;
    }
    (void)xQueueSend(s_key_q, &msg, 0);
}

esp_err_t ui_usb_mode_start(void)
{
    lv_obj_t *scr = lv_screen_active();

    ui_nvs_load();
    if (s_boot_forced_normal || gpio_get_level(KEY1_GPIO) == 0) {
        s_mode = UI_USB_NORMAL;
        ui_nvs_save();
        ESP_LOGW(TAG, "上电按住向上键，强制正常模式");
    }

    s_screen = (s_mode == UI_USB_CONFIG) ? UI_SCR_CONFIG_LIVE : UI_SCR_SETTINGS;
    s_focus = 0;

    s_key_q = xQueueCreate(UI_KEY_QUEUE_LEN, sizeof(ui_key_msg_t));
    if (s_key_q == NULL) {
        return ESP_ERR_NO_MEM;
    }

    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_style_text_font(scr, UI_FONT, 0);
    lv_obj_set_style_text_color(scr, lv_color_white(), 0);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    s_body = lv_obj_create(scr);
    lv_obj_remove_style_all(s_body);
    lv_obj_set_size(s_body, UI_BODY_W, UI_BODY_H);
    lv_obj_align(s_body, LV_ALIGN_CENTER, 0, -8);
    lv_obj_set_flex_flow(s_body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_body, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(s_body, 8, 0);
    lv_obj_remove_flag(s_body, LV_OBJ_FLAG_SCROLLABLE);

    s_hint = lv_label_create(scr);
    ui_style_label(s_hint, ui_col_muted(), 1);
    lv_obj_set_width(s_hint, UI_BODY_W);
    lv_obj_align(s_hint, LV_ALIGN_BOTTOM_MID, 0, -UI_SAFE);

    s_toast = lv_label_create(scr);
    lv_label_set_long_mode(s_toast, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_toast, UI_BODY_W - 24);
    ui_style_label(s_toast, lv_color_white(), 1);
    lv_obj_set_style_bg_color(s_toast, lv_color_hex(0x2A2A2A), 0);
    lv_obj_set_style_bg_opa(s_toast, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(s_toast, 8, 0);
    lv_obj_set_style_radius(s_toast, 8, 0);
    lv_obj_align(s_toast, LV_ALIGN_BOTTOM_MID, 0, -(UI_SAFE + 28));
    lv_obj_add_flag(s_toast, LV_OBJ_FLAG_HIDDEN);

    s_toast_timer = lv_timer_create(ui_toast_hide, UI_TOAST_MS, NULL);
    if (s_toast_timer) {
        lv_timer_set_repeat_count(s_toast_timer, -1);
        lv_timer_pause(s_toast_timer);
    }
    lv_timer_create(ui_key_timer, 20, NULL);

    s_boot_msc = (s_screen == UI_SCR_CONFIG_LIVE) ? 1 : 0;
    ui_rebuild();
    key_set_callback(ui_key_cb);

    ESP_LOGI(TAG, "USB 模式设置页已启动");
    return ESP_OK;
}

void ui_usb_mode_apply_boot_override(void)
{
    if (gpio_get_level(KEY1_GPIO) != 0) {
        return;
    }
    s_boot_forced_normal = 1;
    s_mode = UI_USB_NORMAL;
    ui_nvs_save();
    ESP_LOGW(TAG, "上电按住向上键，已清 NVS 配置模式");
}

void ui_usb_mode_kick_boot_msc(void)
{
    esp_err_t err;

    if (!s_boot_msc) {
        return;
    }
    s_boot_msc = 0;
    err = usb_msc_request_enter();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "上电自动 MSC 请求失败: %s", esp_err_to_name(err));
        s_mode = UI_USB_NORMAL;
        s_screen = UI_SCR_SETTINGS;
        ui_nvs_save();
    } else {
        ESP_LOGI(TAG, "上电配置模式：已请求启动 U 盘");
    }
    if (lvgl_port_lock(0)) {
        ui_rebuild();
        lvgl_port_unlock();
    }
}
