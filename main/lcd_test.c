#include "lcd_test.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lcd.h"

static const char *TAG = "lcd_test";

#define LCD_TEST_TASK_STACK     4096
#define LCD_TEST_TASK_PRIO      5
#define LCD_TEST_THEME_COUNT    4
#define LCD_TEST_STROKE         6
#define LCD_TEST_R_SMALL        72
#define LCD_TEST_DOT_R          6
#define LCD_TEST_CROSS_HALF     3
#define LCD_TEST_STROKE_Q       (LCD_TEST_STROKE * 2)
#define LCD_TEST_R_LARGE_Q      ((LCD_H_RES - 1) - LCD_TEST_STROKE)
#define LCD_TEST_R_SMALL_Q      (LCD_TEST_R_SMALL * 2)
#define LCD_TEST_DOT_R_Q        (LCD_TEST_DOT_R * 2)
#define LCD_TEST_CROSS_0        (LCD_H_RES / 2 - LCD_TEST_CROSS_HALF)
#define LCD_TEST_CROSS_1        (LCD_H_RES / 2 + LCD_TEST_CROSS_HALF - 1)

typedef struct {
    uint8_t bg_r, bg_g, bg_b;
    uint8_t ring_r, ring_g, ring_b;
    uint8_t cross_r, cross_g, cross_b;
} lcd_test_theme_t;

/* 四套风格：黑白、HUD 蓝青、热成像红金、浅底深线 */
static const lcd_test_theme_t s_themes[LCD_TEST_THEME_COUNT] = {
    {  0,   0,   0,  255, 255, 255,  255,  40,  40 },
    {  0,  16,  48,    0, 220, 255,  255, 220,   0 },
    { 40,   0,   0,  255, 180,   0,  255, 255, 255 },
    { 230, 230, 230,  20,  20,  20,    0,  90, 200 },
};

static uint16_t s_rowbuf[LCD_H_RES * LCD_DRAW_ROWS_MAX];

static uint16_t lcd_rgb565_be(uint8_t r, uint8_t g, uint8_t b)
{
    return __builtin_bswap16(LCD_COLOR_RGB565(r, g, b));
}

static int lcd_center_d2(int x, int y)
{
    const int dx = 2 * x + 1 - LCD_H_RES;
    const int dy = 2 * y + 1 - LCD_V_RES;
    return dx * dx + dy * dy;
}

static int lcd_on_ring(int x, int y, int r_mid_q, int stroke_q)
{
    const int d2 = lcd_center_d2(x, y);
    int r_in_q = r_mid_q - stroke_q / 2;
    const int r_out_q = r_mid_q + stroke_q / 2;
    if (r_in_q < 0) {
        r_in_q = 0;
    }
    return (d2 >= r_in_q * r_in_q) && (d2 <= r_out_q * r_out_q);
}

static int lcd_on_dot(int x, int y)
{
    return lcd_center_d2(x, y) <= LCD_TEST_DOT_R_Q * LCD_TEST_DOT_R_Q;
}

static int lcd_on_cross(int x, int y)
{
    return ((x >= LCD_TEST_CROSS_0) && (x <= LCD_TEST_CROSS_1)) ||
           ((y >= LCD_TEST_CROSS_0) && (y <= LCD_TEST_CROSS_1));
}

static esp_err_t lcd_test_draw_reticle(uint8_t theme)
{
    theme = (uint8_t)(theme % LCD_TEST_THEME_COUNT);
    const lcd_test_theme_t *pal = &s_themes[theme];
    const uint16_t bg = lcd_rgb565_be(pal->bg_r, pal->bg_g, pal->bg_b);
    const uint16_t ring = lcd_rgb565_be(pal->ring_r, pal->ring_g, pal->ring_b);
    const uint16_t cross = lcd_rgb565_be(pal->cross_r, pal->cross_g, pal->cross_b);

    //ESP_LOGI(TAG, "LCD_FW reticle-task-v1 画准星 theme=%u 大圆q=%d 小圆q=%d",
    //         (unsigned)theme, LCD_TEST_R_LARGE_Q, LCD_TEST_R_SMALL_Q);

    esp_err_t err = lcd_prepare_1wire();
    if (err != ESP_OK) {
        return err;
    }

    int y0 = 0;
    while (y0 < LCD_V_RES) {
        int rows = LCD_V_RES - y0;
        if (rows > LCD_DRAW_ROWS_MAX) {
            rows = LCD_DRAW_ROWS_MAX;
        }
        for (int row = 0; row < rows; row++) {
            const int y = y0 + row;
            uint16_t *line = &s_rowbuf[row * LCD_H_RES];
            for (int x = 0; x < LCD_H_RES; x++) {
                uint16_t pix = bg;
                if (lcd_on_ring(x, y, LCD_TEST_R_LARGE_Q, LCD_TEST_STROKE_Q) ||
                    lcd_on_ring(x, y, LCD_TEST_R_SMALL_Q, LCD_TEST_STROKE_Q)) {
                    pix = ring;
                }
                if (lcd_on_cross(x, y) || lcd_on_dot(x, y)) {
                    pix = cross;
                }
                line[x] = pix;
            }
        }
        err = lcd_draw_rgb565_1wire(y0, rows, s_rowbuf);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "准星写 y=%d rows=%d 失败: %s", y0, rows, esp_err_to_name(err));
            return err;
        }
        y0 += rows;
    }
    return ESP_OK;
}

static void lcd_test_task(void *arg)
{
    (void)arg;

    ESP_LOGI(TAG, "LCD_FW reticle-task-v1 先刷纯黑，再循环准星四色");
    esp_err_t err = lcd_fill_1wire(0, 0, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "lcd_fill_1wire 纯黑失败: %s", esp_err_to_name(err));
    }
    vTaskDelay(pdMS_TO_TICKS(400));

    /* 0-1-2-3-2-1 来回刷，不要只单向跳 */
    static const uint8_t k_theme_pingpong[] = { 0, 1, 2, 3, 2, 1 };
    unsigned idx = 0;
    while (1) {
        const uint8_t theme = k_theme_pingpong[idx % (sizeof(k_theme_pingpong) / sizeof(k_theme_pingpong[0]))];

        err = lcd_test_draw_reticle(theme);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "lcd_test_draw_reticle theme=%u 失败: %s",
                     (unsigned)theme, esp_err_to_name(err));
        }
        idx++;
        vTaskDelay(pdMS_TO_TICKS(1200));
    }
}

esp_err_t lcd_test_start(void)
{
    const BaseType_t ok = xTaskCreate(lcd_test_task, "lcd_test",
                                      LCD_TEST_TASK_STACK, NULL,
                                      LCD_TEST_TASK_PRIO, NULL);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "创建 lcd_test 任务失败");
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "lcd_test 任务已启动 (stack=%d prio=%d)",
             LCD_TEST_TASK_STACK, LCD_TEST_TASK_PRIO);
    return ESP_OK;
}
