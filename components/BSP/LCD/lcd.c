#include "lcd.h"

#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "esp_attr.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_st77916.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lcd_init_cmds.h"

static const char *TAG = "lcd";

/* 色条测试每次 20 行一次 RAMWR，便于看出是只写了顶上一小条还是整条都在 */
#define LCD_FILL_LINES              20
#define LCD_MAX_TRANSFER_SZ         (LCD_H_RES * 80 * LCD_FB_BYTES_PER_PIXEL)
#define LCD_COLOR_DMA_TIMEOUT_MS    1000

#define LCD_BL_LEDC_TIMER           LEDC_TIMER_0
#define LCD_BL_LEDC_MODE            LEDC_LOW_SPEED_MODE
#define LCD_BL_LEDC_CHANNEL         LEDC_CHANNEL_0

static esp_lcd_panel_io_handle_t s_io = NULL;
static esp_lcd_panel_handle_t s_panel = NULL;
static uint16_t *s_fill_buf = NULL;
static SemaphoreHandle_t s_color_done = NULL;
static bool s_ready = false;

static bool IRAM_ATTR lcd_on_color_trans_done(esp_lcd_panel_io_handle_t panel_io,
                                              esp_lcd_panel_io_event_data_t *edata,
                                              void *user_ctx)
{
    BaseType_t high_task_wakeup = pdFALSE;
    (void)panel_io;
    (void)edata;
    (void)user_ctx;
    xSemaphoreGiveFromISR(s_color_done, &high_task_wakeup);
    return high_task_wakeup == pdTRUE;
}

static esp_err_t lcd_wait_color_dma(void)
{
    if (xSemaphoreTake(s_color_done, pdMS_TO_TICKS(LCD_COLOR_DMA_TIMEOUT_MS)) != pdTRUE) {
        ESP_LOGE(TAG, "等待像素 DMA 超时 (%d ms)", LCD_COLOR_DMA_TIMEOUT_MS);
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

static uint32_t lcd_percent_to_duty(uint8_t percent)
{
    if (percent > 100) {
        percent = 100;
    }
    return ((uint32_t)percent * LCD_BL_PWM_MAX_DUTY) / 100;
}

esp_err_t lcd_set_backlight(uint8_t percent)
{
    uint8_t clamped = (percent > 100) ? 100 : percent;
    uint32_t duty = lcd_percent_to_duty(clamped);

    ESP_LOGI(TAG, "背光设置为 %u%% (duty=%lu/%d, GPIO%d, 高电平亮)",
             (unsigned)clamped, (unsigned long)duty, LCD_BL_PWM_MAX_DUTY, LCD_PIN_BL);

    esp_err_t err = ledc_set_duty(LCD_BL_LEDC_MODE, LCD_BL_LEDC_CHANNEL, duty);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ledc_set_duty 失败: %s", esp_err_to_name(err));
        return err;
    }
    err = ledc_update_duty(LCD_BL_LEDC_MODE, LCD_BL_LEDC_CHANNEL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ledc_update_duty 失败: %s", esp_err_to_name(err));
        return err;
    }
    return ESP_OK;
}

static esp_err_t lcd_backlight_init(void)
{
    ESP_LOGI(TAG, "初始化背光 PWM: GPIO%d, %d Hz, %d bit, 默认 %d%%",
             LCD_PIN_BL, LCD_BL_PWM_FREQ_HZ, 10, LCD_BL_DEFAULT_PERCENT);

    const ledc_timer_config_t timer_cfg = {
        .speed_mode = LCD_BL_LEDC_MODE,
        .duty_resolution = LCD_BL_PWM_RESOLUTION,
        .timer_num = LCD_BL_LEDC_TIMER,
        .freq_hz = LCD_BL_PWM_FREQ_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    esp_err_t err = ledc_timer_config(&timer_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ledc_timer_config 失败: %s", esp_err_to_name(err));
        return err;
    }

    const ledc_channel_config_t ch_cfg = {
        .gpio_num = LCD_PIN_BL,
        .speed_mode = LCD_BL_LEDC_MODE,
        .channel = LCD_BL_LEDC_CHANNEL,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = LCD_BL_LEDC_TIMER,
        .duty = 0,
        .hpoint = 0,
        .flags.output_invert = 0,
    };
    err = ledc_channel_config(&ch_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ledc_channel_config 失败: %s", esp_err_to_name(err));
        return err;
    }

    return lcd_set_backlight(LCD_BL_DEFAULT_PERCENT);
}

esp_err_t lcd_init(void)
{
    if (s_ready) {
        ESP_LOGW(TAG, "LCD 已经初始化，跳过");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "开始初始化 ST77916 QSPI LCD");
    ESP_LOGI(TAG, "分辨率 %dx%d, bpp=%d, PCLK=%d Hz, SPI host=%d",
             LCD_H_RES, LCD_V_RES, LCD_BITS_PER_PIXEL, LCD_PCLK_HZ, (int)LCD_HOST);
    ESP_LOGI(TAG, "引脚 D0=%d D1=%d D2=%d D3=%d SCK=%d CS=%d BL=%d RST=NC",
             LCD_PIN_D0, LCD_PIN_D1, LCD_PIN_D2, LCD_PIN_D3,
             LCD_PIN_SCK, LCD_PIN_CS, LCD_PIN_BL);

    esp_err_t err = lcd_backlight_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "背光初始化失败: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "初始化 QSPI 总线, max_transfer_sz=%u", (unsigned)LCD_MAX_TRANSFER_SZ);
    const spi_bus_config_t buscfg = ST77916_PANEL_BUS_QSPI_CONFIG(
        LCD_PIN_SCK,
        LCD_PIN_D0,
        LCD_PIN_D1,
        LCD_PIN_D2,
        LCD_PIN_D3,
        LCD_MAX_TRANSFER_SZ);
    err = spi_bus_initialize(LCD_HOST, &buscfg, SPI_DMA_CH_AUTO);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "spi_bus_initialize 失败: %s", esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "QSPI 总线就绪");

    if (s_color_done == NULL) {
        s_color_done = xSemaphoreCreateBinary();
        if (s_color_done == NULL) {
            ESP_LOGE(TAG, "创建像素 DMA 信号量失败");
            return ESP_ERR_NO_MEM;
        }
    }

    ESP_LOGI(TAG, "安装 panel IO (QSPI, CS=%d, 无 DC, 等 DMA 完成后再复用缓冲)", LCD_PIN_CS);
    esp_lcd_panel_io_spi_config_t io_config = ST77916_PANEL_IO_QSPI_CONFIG(LCD_PIN_CS, lcd_on_color_trans_done, NULL);
    io_config.pclk_hz = LCD_PCLK_HZ;
    io_config.cs_ena_posttrans = 3;
    err = esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io_config, &s_io);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_lcd_new_panel_io_spi 失败: %s", esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "panel IO 就绪, pclk=%u Hz", (unsigned)io_config.pclk_hz);

    ESP_LOGI(TAG, "安装 ST77916 驱动, use_qspi_interface=1, 自定义 init %u 条 (360x360 QSPI)",
             (unsigned)LCD_INIT_CMDS_SIZE);
    st77916_vendor_config_t vendor_config = {
        .init_cmds = lcd_init_cmds,
        .init_cmds_size = LCD_INIT_CMDS_SIZE,
        .flags = {
            .use_qspi_interface = 1,
        },
    };
    const esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = LCD_PIN_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = LCD_BITS_PER_PIXEL,
        .vendor_config = &vendor_config,
    };
    err = esp_lcd_new_panel_st77916(s_io, &panel_config, &s_panel);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_lcd_new_panel_st77916 失败: %s", esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "ST77916 panel 对象已创建");

    ESP_LOGI(TAG, "复位面板 (RST 未接 GPIO, 驱动内部软复位)");
    err = esp_lcd_panel_reset(s_panel);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_lcd_panel_reset 失败: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "发送 1.8 寸 ST77916 初始化 (0xF0=0x08, 非 VoCat 1.5 寸表)");
    err = esp_lcd_panel_init(s_panel);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_lcd_panel_init 失败: %s", esp_err_to_name(err));
        return err;
    }

    /* 1.8 寸序列已含 0x21 invert 和 0x29 DISPON */
    err = esp_lcd_panel_invert_color(s_panel, true);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_lcd_panel_invert_color 失败: %s", esp_err_to_name(err));
        return err;
    }

    err = esp_lcd_panel_disp_on_off(s_panel, true);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_lcd_panel_disp_on_off 失败: %s", esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "显示已打开");

    s_fill_buf = heap_caps_malloc(LCD_H_RES * LCD_FILL_LINES * LCD_FB_BYTES_PER_PIXEL, MALLOC_CAP_DMA);
    if (s_fill_buf == NULL) {
        ESP_LOGE(TAG, "申请 DMA 色块缓冲失败, 需要 %u 字节",
                 (unsigned)(LCD_H_RES * LCD_FILL_LINES * LCD_FB_BYTES_PER_PIXEL));
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "DMA 色块缓冲 %u 字节 (每次 %d 行, RGB565 %d B/pix)",
             (unsigned)(LCD_H_RES * LCD_FILL_LINES * LCD_FB_BYTES_PER_PIXEL),
             LCD_FILL_LINES, LCD_FB_BYTES_PER_PIXEL);

    s_ready = true;
    ESP_LOGI(TAG, "LCD 初始化完成");
    return ESP_OK;
}

esp_err_t lcd_fill_rect(int x, int y, int w, int h, uint8_t r, uint8_t g, uint8_t b)
{
    if (!s_ready || s_panel == NULL || s_fill_buf == NULL) {
        ESP_LOGE(TAG, "lcd_fill_rect: 尚未初始化");
        return ESP_ERR_INVALID_STATE;
    }
    if (w <= 0 || h <= 0) {
        ESP_LOGE(TAG, "lcd_fill_rect: 非法尺寸 %dx%d", w, h);
        return ESP_ERR_INVALID_ARG;
    }

    if (x < 0) {
        w += x;
        x = 0;
    }
    if (y < 0) {
        h += y;
        y = 0;
    }
    if (x >= LCD_H_RES || y >= LCD_V_RES || w <= 0 || h <= 0) {
        ESP_LOGE(TAG, "lcd_fill_rect: 矩形在屏外 (%d,%d) %dx%d", x, y, w, h);
        return ESP_ERR_INVALID_ARG;
    }
    if (x + w > LCD_H_RES) {
        w = LCD_H_RES - x;
    }
    if (y + h > LCD_V_RES) {
        h = LCD_V_RES - y;
    }

    const uint16_t rgb565 = LCD_COLOR_RGB565(r, g, b);
    const uint16_t color_be = __builtin_bswap16(rgb565);

    ESP_LOGI(TAG, "填充矩形 (%d,%d) %dx%d RGB888=%u,%u,%u RGB565=0x%04X (总线 0x%04X)",
             x, y, w, h, r, g, b, rgb565, color_be);

    int y0 = y;
    const int y1 = y + h;
    while (y0 < y1) {
        int rows = y1 - y0;
        if (rows > LCD_FILL_LINES) {
            rows = LCD_FILL_LINES;
        }
        const size_t pix = (size_t)w * (size_t)rows;
        for (size_t i = 0; i < pix; i++) {
            s_fill_buf[i] = color_be;
        }
        esp_err_t err = esp_lcd_panel_draw_bitmap(s_panel, x, y0, x + w, y0 + rows, s_fill_buf);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "draw_bitmap (%d,%d)-(%d,%d) 失败: %s",
                     x, y0, x + w, y0 + rows, esp_err_to_name(err));
            return err;
        }
        err = lcd_wait_color_dma();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "draw_bitmap (%d,%d)-(%d,%d) DMA 未完成",
                     x, y0, x + w, y0 + rows);
            return err;
        }
        y0 += rows;
    }

    return ESP_OK;
}

esp_err_t lcd_fill_color(uint8_t r, uint8_t g, uint8_t b)
{
    return lcd_fill_rect(0, 0, LCD_H_RES, LCD_V_RES, r, g, b);
}

esp_err_t lcd_draw_test_pattern(void)
{
    const int rows = 80;
    const int y0 = LCD_V_RES - rows; /* 280：故意写在底部，用来分辨是第一行还是最后一行 */
    const size_t pix = (size_t)LCD_H_RES * (size_t)rows;
    uint16_t *fb = NULL;

    if (!s_ready || s_panel == NULL) {
        ESP_LOGE(TAG, "lcd_draw_test_pattern: 尚未初始化");
        return ESP_ERR_INVALID_STATE;
    }

    fb = heap_caps_malloc(pix * LCD_FB_BYTES_PER_PIXEL, MALLOC_CAP_DMA);
    if (fb == NULL) {
        ESP_LOGE(TAG, "oneshot 申请 %u 字节 DMA 缓冲失败", (unsigned)(pix * LCD_FB_BYTES_PER_PIXEL));
        return ESP_ERR_NO_MEM;
    }

    const uint16_t red = __builtin_bswap16(LCD_COLOR_RGB565(255, 0, 0));
    const uint16_t green = __builtin_bswap16(LCD_COLOR_RGB565(0, 255, 0));
    const uint16_t blue = __builtin_bswap16(LCD_COLOR_RGB565(0, 0, 255));
    const uint16_t white = __builtin_bswap16(LCD_COLOR_RGB565(255, 255, 255));

    for (int y = 0; y < rows; y++) {
        uint16_t color;
        if (y < 20) {
            color = red;
        } else if (y < 40) {
            color = green;
        } else if (y < 60) {
            color = blue;
        } else {
            color = white;
        }
        for (int x = 0; x < LCD_H_RES; x++) {
            fb[y * LCD_H_RES + x] = color;
        }
    }

    ESP_LOGI(TAG, "LCD_FW coord-oneshot-v1: 一次 RAMWR 写 y=%d..%d (底部80行) 红/绿/蓝/白各20行",
             y0, LCD_V_RES - 1);
    ESP_LOGI(TAG, "看位置: 色带在底部=Y有效; 色带仍在顶部=RASET无效; 仍半黑半白=没烧到这版");

    esp_err_t err = esp_lcd_panel_draw_bitmap(s_panel, 0, y0, LCD_H_RES, LCD_V_RES, fb);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "oneshot draw_bitmap 失败: %s", esp_err_to_name(err));
        heap_caps_free(fb);
        return err;
    }
    err = lcd_wait_color_dma();
    heap_caps_free(fb);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "oneshot DMA 超时");
        return err;
    }

    ESP_LOGI(TAG, "LCD_FW coord-oneshot-v1 发送完成");
    return ESP_OK;
}

esp_lcd_panel_handle_t lcd_get_panel(void)
{
    return s_panel;
}
