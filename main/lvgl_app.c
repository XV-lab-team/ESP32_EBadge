#include "lvgl_app.h"

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "lcd.h"
#include "lvgl.h"
#include "sdmmc_fat.h"
#include "ui_usb_mode.h"

static const char *TAG = "lvgl_app";

#define LVGL_FB_ALIGN           64
#define LVGL_FB_BYTES           ((size_t)LCD_H_RES * (size_t)LCD_V_RES * LCD_FB_BYTES_PER_PIXEL)
#define HEAP_MONITOR_PERIOD_MS  500

/* 圆屏边距已由用户确认：顶/底 8、左 4。不要只靠 sdkconfig 对齐。 */
#define DEBUG_PAD_TOP           8
#define DEBUG_PAD_BOTTOM        8
#define DEBUG_PAD_LEFT          4

static const char *s_sd_fs_status = "sd --";

static void lvgl_sd_fs_probe(void)
{
    lv_fs_dir_t dir;
    lv_fs_res_t res;
    char fn[64];
    int n = 0;

    if (!sdmmc_fat_is_mounted()) {
        s_sd_fs_status = "sd --";
        ESP_LOGW(TAG, "LVGL FS %s skipped, FAT not mounted", LVGL_SD_DRIVE);
        return;
    }

    res = lv_fs_dir_open(&dir, "S:/");
    if (res != LV_FS_RES_OK) {
        s_sd_fs_status = "sd fail";
        ESP_LOGE(TAG, "lv_fs_dir_open %s failed (%d)", LVGL_SD_DRIVE, (int)res);
        return;
    }

    s_sd_fs_status = "sd S:";
    ESP_LOGI(TAG, "LVGL FS %s -> %s", LVGL_SD_DRIVE, sdmmc_fat_mount_path());
    while (n < 8) {
        res = lv_fs_dir_read(&dir, fn, sizeof(fn));
        if (res != LV_FS_RES_OK || fn[0] == '\0') {
            break;
        }
        ESP_LOGI(TAG, "  %s", fn);
        n++;
    }
    lv_fs_dir_close(&dir);
}

void lvgl_sd_fs_refresh(void)
{
    lvgl_sd_fs_probe();
}

void lvgl_sd_fs_set_status(const char *text)
{
    if (text != NULL && text[0] != '\0') {
        s_sd_fs_status = text;
    }
}

static void heap_monitor_cb(lv_timer_t *t)
{
    lv_obj_t *label = (lv_obj_t *)lv_timer_get_user_data(t);
    const unsigned int_kb = (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024);
    const unsigned psram_kb = (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024);
    const unsigned dma_kb = (unsigned)(heap_caps_get_free_size(MALLOC_CAP_DMA) / 1024);

    lv_label_set_text_fmt(label, "int %uK\npsram %uK\ndma %uK\n%s",
                          int_kb, psram_kb, dma_kb, s_sd_fs_status);
}

static void lvgl_style_debug_label(lv_obj_t *label)
{
    lv_obj_set_style_bg_opa(label, LV_OPA_50, 0);
    lv_obj_set_style_bg_color(label, lv_color_black(), 0);
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_obj_set_style_pad_all(label, 3, 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_12, 0);
}

static void lvgl_place_debug_labels(lv_display_t *disp)
{
    lv_obj_t *sys = lv_display_get_layer_sys(disp);
    const uint32_t n = lv_obj_get_child_count(sys);
    uint32_t i;

    /*
     * lv_conf_kconfig.h 把 TOP_MID 写成了 CONFIG_LV_USE_PERF_MONITOR_ALIGN_TOP_MID，
     * 实际 Kconfig 是 CONFIG_LV_PERF_MONITOR_ALIGN_TOP_MID，perf 会落到默认右下被圆屏切掉。
     * 子对象顺序：perf、mem、heap。圆屏边距已由用户确认：顶/底 8、左 4。
     */
    for (i = 0; i < n; i++) {
        lvgl_style_debug_label(lv_obj_get_child(sys, i));
    }
    if (n >= 1) {
        lv_obj_align(lv_obj_get_child(sys, 0), LV_ALIGN_TOP_MID, 0, DEBUG_PAD_TOP);
    }
    if (n >= 2) {
        lv_obj_align(lv_obj_get_child(sys, 1), LV_ALIGN_BOTTOM_MID, 0, -DEBUG_PAD_BOTTOM);
    }
    if (n >= 3) {
        lv_obj_align(lv_obj_get_child(sys, 2), LV_ALIGN_LEFT_MID, DEBUG_PAD_LEFT, 0);
    }
}

static void lvgl_create_debug_overlay(lv_display_t *disp)
{
    lv_obj_t *heap_label = lv_label_create(lv_display_get_layer_sys(disp));
    lvgl_style_debug_label(heap_label);
    lv_label_set_text(heap_label, "int ?K\npsram ?K\ndma ?K\nsd ?");

    lv_timer_t *timer = lv_timer_create(heap_monitor_cb, HEAP_MONITOR_PERIOD_MS, heap_label);
    if (timer != NULL) {
        lv_timer_ready(timer);
    }

    lvgl_place_debug_labels(disp);
}

static void *lvgl_alloc_fb(size_t buf_bytes)
{
    void *buf = heap_caps_aligned_alloc(LVGL_FB_ALIGN, buf_bytes,
                                        MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    if (buf != NULL) {
        ESP_LOGI(TAG, "LVGL 全屏缓冲 %u 字节在内部 DMA RAM", (unsigned)buf_bytes);
        return buf;
    }

    buf = heap_caps_aligned_alloc(LVGL_FB_ALIGN, buf_bytes, MALLOC_CAP_SPIRAM);
    if (buf != NULL) {
        ESP_LOGI(TAG, "LVGL 全屏缓冲 %u 字节在 PSRAM, flush 经内部 DMA bounce",
                 (unsigned)buf_bytes);
        return buf;
    }

    return NULL;
}

static void lvgl_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    const int x = area->x1;
    const int y = area->y1;
    const int w = lv_area_get_width(area);
    const int h = lv_area_get_height(area);

    /* RGB565_SWAPPED：LVGL 已按总线大端画出，FULL 缓冲留给下一帧，禁止原地 bswap */
    esp_err_t err = lcd_draw_rgb565(x, y, w, h, (const uint16_t *)px_map);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "flush (%d,%d) %dx%d 失败: %s", x, y, w, h, esp_err_to_name(err));
    }

    lv_display_flush_ready(disp);
}

esp_err_t lvgl_app_start(void)
{
    ESP_LOGI(TAG, "准备官方 DMA 写屏并启动 LVGL %dx%d RGB565_SWAPPED FULL",
             LCD_H_RES, LCD_V_RES);

    esp_err_t err = lcd_prepare_1wire();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "lcd_prepare_1wire 失败: %s", esp_err_to_name(err));
        return err;
    }

    const lvgl_port_cfg_t lvgl_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    err = lvgl_port_init(&lvgl_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "lvgl_port_init 失败: %s", esp_err_to_name(err));
        return err;
    }

    const size_t buf_bytes = LVGL_FB_BYTES;
    void *buf = lvgl_alloc_fb(buf_bytes);
    if (buf == NULL) {
        ESP_LOGE(TAG, "申请 LVGL 全屏缓冲失败, 需要 %u 字节", (unsigned)buf_bytes);
        return ESP_ERR_NO_MEM;
    }

    if (!lvgl_port_lock(0)) {
        ESP_LOGE(TAG, "lvgl_port_lock 失败");
        return ESP_ERR_TIMEOUT;
    }

    lv_display_t *disp = lv_display_create(LCD_H_RES, LCD_V_RES);
    if (disp == NULL) {
        lvgl_port_unlock();
        ESP_LOGE(TAG, "lv_display_create 失败");
        return ESP_ERR_NO_MEM;
    }

    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565_SWAPPED);
    lv_display_set_flush_cb(disp, lvgl_flush_cb);
    lv_display_set_buffers(disp, buf, NULL, (uint32_t)buf_bytes, LV_DISPLAY_RENDER_MODE_FULL);

    lvgl_sd_fs_probe();

    err = ui_usb_mode_start();
    if (err == ESP_OK) {
        lvgl_create_debug_overlay(disp);
    }
    lvgl_port_unlock();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ui_usb_mode_start 失败: %s", esp_err_to_name(err));
        return err;
    }

    /* TinyUSB 进出走 worker，不要占着 LVGL 锁。 */
    ui_usb_mode_kick_boot_msc();

    ESP_LOGI(TAG, "USB 模式设置页已启动, sysmon 开；正常模式不占用 USB PHY / GPIO19 / GPIO20");
    return ESP_OK;
}
