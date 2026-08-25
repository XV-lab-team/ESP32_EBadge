#include "lcd.h"

#include <string.h>

#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "esp_attr.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_commands.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_st77916.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lcd_init_cmds.h"

static const char *TAG = "lcd";

/* 每次最多 20 行一次 RAMWR，缓冲大小和 lcd.h 的 LCD_DRAW_ROWS_MAX 对齐 */
#define LCD_FILL_LINES              LCD_DRAW_ROWS_MAX
/* PSRAM 整帧不能直接 DMA：SPI 会再申请同等大小的内部 priv TX，259KB 会 ESP_ERR_NO_MEM */
#define LCD_DMA_BOUNCE_ROWS         40
#define LCD_MAX_TRANSFER_SZ         (LCD_H_RES * LCD_DMA_BOUNCE_ROWS * LCD_FB_BYTES_PER_PIXEL)
#define LCD_COLOR_DMA_TIMEOUT_MS    1000
#define LCD_COLOR_DMA_GRACE_MS      50

#define LCD_BL_LEDC_TIMER           LEDC_TIMER_0
#define LCD_BL_LEDC_MODE            LEDC_LOW_SPEED_MODE
#define LCD_BL_LEDC_CHANNEL         LEDC_CHANNEL_0

static esp_lcd_panel_io_handle_t s_io = NULL;
static esp_lcd_panel_handle_t s_panel = NULL;
static uint16_t *s_fill_buf = NULL;
static uint16_t *s_dma_bounce = NULL;
static SemaphoreHandle_t s_color_done = NULL;
static bool s_ready = false;

static int lcd_qspi_cmd(uint8_t cmd)
{
    return (int)((0x02u << 24) | ((uint32_t)cmd << 8));
}

static int lcd_qspi_rd_cmd(uint8_t cmd)
{
    return (int)((0x0Bu << 24) | ((uint32_t)cmd << 8));
}

static esp_err_t lcd_tx_reg(uint8_t cmd, const uint8_t *data, size_t len)
{
    return esp_lcd_panel_io_tx_param(s_io, lcd_qspi_cmd(cmd), data, len);
}

static esp_err_t lcd_rx_reg(uint8_t cmd, void *data, size_t len)
{
    return esp_lcd_panel_io_rx_param(s_io, lcd_qspi_rd_cmd(cmd), data, len);
}

static esp_err_t lcd_read_and_log(const char *name, uint8_t cmd, size_t n)
{
    uint8_t buf[8] = {0};
    if (n > sizeof(buf)) {
        n = sizeof(buf);
    }
    esp_err_t err = lcd_rx_reg(cmd, buf, n);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "读 %s (0x%02X) 失败: %s", name, cmd, esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "%s (0x%02X): %02X %02X %02X %02X %02X %02X %02X %02X",
             name, cmd, buf[0], buf[1], buf[2], buf[3], buf[4], buf[5], buf[6], buf[7]);
    if (cmd == LCD_CMD_RDDPM) {
        /* 实验 O 的 RAMRD dummy 是 0x7F；状态寄存器同样可能首字节是 dummy */
        const uint8_t v = (buf[0] == 0x7F) ? buf[1] : buf[0];
        ESP_LOGI(TAG, "RDDPM 按 0x%02X 解码: booster=%u idle=%u partial=%u slpout=%u dispon=%u invert=%u",
                 v,
                 (unsigned)((v >> 7) & 1u),
                 (unsigned)((v >> 6) & 1u),
                 (unsigned)((v >> 5) & 1u),
                 (unsigned)((v >> 4) & 1u),
                 (unsigned)((v >> 3) & 1u),
                 (unsigned)((v >> 2) & 1u));
    }
    return ESP_OK;
}

/* 寄存器窗口是闭区间；init 里 0x4C kick 曾把 0x2B 设成 y=360 */
static esp_err_t lcd_set_addr_win(int x0, int y0, int x1, int y1)
{
    const uint8_t caset[] = {
        (uint8_t)((x0 >> 8) & 0xFF), (uint8_t)(x0 & 0xFF),
        (uint8_t)((x1 >> 8) & 0xFF), (uint8_t)(x1 & 0xFF),
    };
    const uint8_t raset[] = {
        (uint8_t)((y0 >> 8) & 0xFF), (uint8_t)(y0 & 0xFF),
        (uint8_t)((y1 >> 8) & 0xFF), (uint8_t)(y1 & 0xFF),
    };
    esp_err_t err = lcd_tx_reg(LCD_CMD_CASET, caset, sizeof(caset));
    if (err != ESP_OK) {
        return err;
    }
    return lcd_tx_reg(LCD_CMD_RASET, raset, sizeof(raset));
}

static esp_err_t lcd_restore_full_window(void)
{
    return lcd_set_addr_win(0, 0, LCD_H_RES - 1, LCD_V_RES - 1);
}

/* 像素走和 init 一样的 opcode 0x02 / tx_param，不用 tx_color 的四线 QIO */
static esp_err_t lcd_write_ram_1wire(const void *data, size_t len)
{
    return esp_lcd_panel_io_tx_param(s_io, lcd_qspi_cmd(LCD_CMD_RAMWR), data, len);
}

static esp_err_t lcd_fill_rect_1wire(int x, int y, int w, int h, uint8_t r, uint8_t g, uint8_t b)
{
    if (!s_ready || s_io == NULL || s_fill_buf == NULL) {
        ESP_LOGE(TAG, "lcd_fill_rect_1wire: 尚未初始化");
        return ESP_ERR_INVALID_STATE;
    }
    if (w <= 0 || h <= 0) {
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

    ESP_LOGI(TAG, "一线 0x2C 填充 (%d,%d) %dx%d RGB888=%u,%u,%u RGB565=0x%04X 总线=0x%04X",
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
        esp_err_t err = lcd_set_addr_win(x, y0, x + w - 1, y0 + rows - 1);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "一线设窗口失败: %s", esp_err_to_name(err));
            return err;
        }
        err = lcd_write_ram_1wire(s_fill_buf, pix * LCD_FB_BYTES_PER_PIXEL);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "一线 RAMWR 失败: %s", esp_err_to_name(err));
            return err;
        }
        y0 += rows;
    }
    return ESP_OK;
}

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

static void lcd_color_dma_drain(void)
{
    if (s_color_done == NULL) {
        return;
    }
    while (xSemaphoreTake(s_color_done, 0) == pdTRUE) {
    }
}

static esp_err_t lcd_wait_color_dma(void)
{
    if (xSemaphoreTake(s_color_done, pdMS_TO_TICKS(LCD_COLOR_DMA_TIMEOUT_MS)) == pdTRUE) {
        return ESP_OK;
    }
    ESP_LOGE(TAG, "等待像素 DMA 超时 (%d ms)", LCD_COLOR_DMA_TIMEOUT_MS);
    if (xSemaphoreTake(s_color_done, pdMS_TO_TICKS(LCD_COLOR_DMA_GRACE_MS)) == pdTRUE) {
        ESP_LOGW(TAG, "像素 DMA 在宽限内完成");
        return ESP_OK;
    }
    /* 迟到的 ISR give 不能当成下一帧完成，否则会边 DMA 边改 bounce。 */
    lcd_color_dma_drain();
    return ESP_ERR_TIMEOUT;
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

    ESP_LOGI(TAG, "发送 HD18004C18 init (0xF0=0x28, 0x76=0x0F, B0=0x52, 栅极 0x48+行号 0x04, 非 VoCat/180/IDF)");
    err = esp_lcd_panel_init(s_panel);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_lcd_panel_init 失败: %s", esp_err_to_name(err));
        return err;
    }

    /* HD18004 表已含 0x21 invert 和 0x29 DISPON */
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

    ESP_LOGI(TAG, "init 后重设 CASET/RASET 0-359, 避免 0x4C kick 把窗口留在 y=360");
    err = lcd_restore_full_window();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "恢复全屏窗口失败: %s", esp_err_to_name(err));
        return err;
    }

    s_fill_buf = heap_caps_malloc(LCD_H_RES * LCD_FILL_LINES * LCD_FB_BYTES_PER_PIXEL, MALLOC_CAP_DMA);
    if (s_fill_buf == NULL) {
        ESP_LOGE(TAG, "申请 DMA 色块缓冲失败, 需要 %u 字节",
                 (unsigned)(LCD_H_RES * LCD_FILL_LINES * LCD_FB_BYTES_PER_PIXEL));
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "DMA 色块缓冲 %u 字节 (每次 %d 行, RGB565 %d B/pix)",
             (unsigned)(LCD_H_RES * LCD_FILL_LINES * LCD_FB_BYTES_PER_PIXEL),
             LCD_FILL_LINES, LCD_FB_BYTES_PER_PIXEL);

    s_dma_bounce = heap_caps_malloc(LCD_MAX_TRANSFER_SZ, MALLOC_CAP_DMA);
    if (s_dma_bounce == NULL) {
        ESP_LOGE(TAG, "申请 DMA bounce 失败, 需要 %u 字节", (unsigned)LCD_MAX_TRANSFER_SZ);
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "DMA bounce %u 字节 (%d 行), PSRAM 帧经此拷贝再 tx_color",
             (unsigned)LCD_MAX_TRANSFER_SZ, LCD_DMA_BOUNCE_ROWS);

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

esp_err_t lcd_draw_rgb565(int x, int y, int w, int h, const uint16_t *rgb565_be)
{
    if (!s_ready || s_panel == NULL || s_dma_bounce == NULL) {
        ESP_LOGE(TAG, "lcd_draw_rgb565: 尚未初始化");
        return ESP_ERR_INVALID_STATE;
    }
    if (rgb565_be == NULL || w <= 0 || h <= 0) {
        return ESP_ERR_INVALID_ARG;
    }
    /* 源缓冲按传入的 w 为行跨距；越界直接拒绝，避免裁剪后像素错位 */
    if (x < 0 || y < 0 || x >= LCD_H_RES || y >= LCD_V_RES ||
        x + w > LCD_H_RES || y + h > LCD_V_RES) {
        ESP_LOGE(TAG, "lcd_draw_rgb565: 区域越界 (%d,%d) %dx%d", x, y, w, h);
        return ESP_ERR_INVALID_ARG;
    }

    /* 源常在 PSRAM。SPI 对外部 RAM 会再 malloc 内部 priv TX，整帧 259KB 会失败。
     * 拷到预申请的内部 DMA bounce 再 draw_bitmap；FULL 帧仍是先画完再连续刷。 */
    const uint16_t *src = rgb565_be;
    int y0 = y;
    const int y1 = y + h;
    while (y0 < y1) {
        int rows = y1 - y0;
        if (rows > LCD_DMA_BOUNCE_ROWS) {
            rows = LCD_DMA_BOUNCE_ROWS;
        }
        const size_t pix = (size_t)w * (size_t)rows;
        memcpy(s_dma_bounce, src, pix * LCD_FB_BYTES_PER_PIXEL);

        esp_err_t err = esp_lcd_panel_draw_bitmap(s_panel, x, y0, x + w, y0 + rows, s_dma_bounce);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "lcd_draw_rgb565 draw_bitmap (%d,%d) %dx%d 失败: %s",
                     x, y0, w, rows, esp_err_to_name(err));
            return err;
        }
        err = lcd_wait_color_dma();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "lcd_draw_rgb565 (%d,%d) %dx%d DMA 未完成", x, y0, w, rows);
            return err;
        }
        src += pix;
        y0 += rows;
    }
    return ESP_OK;
}

esp_err_t lcd_prepare_1wire(void)
{
    if (!s_ready || s_panel == NULL || s_io == NULL) {
        ESP_LOGE(TAG, "lcd_prepare_1wire: 尚未初始化");
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = esp_lcd_panel_invert_color(s_panel, true);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "lcd_prepare_1wire invert on 失败: %s", esp_err_to_name(err));
        return err;
    }

    const uint8_t colmod55 = 0x55;
    err = lcd_tx_reg(LCD_CMD_COLMOD, &colmod55, 1);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "lcd_prepare_1wire 写 COLMOD 0x55 失败: %s", esp_err_to_name(err));
        return err;
    }

    return lcd_restore_full_window();
}

esp_err_t lcd_fill_1wire(uint8_t r, uint8_t g, uint8_t b)
{
    esp_err_t err = lcd_prepare_1wire();
    if (err != ESP_OK) {
        return err;
    }
    return lcd_fill_rect_1wire(0, 0, LCD_H_RES, LCD_V_RES, r, g, b);
}

static esp_err_t lcd_write_rows_1wire(int y, int rows)
{
    esp_err_t err = lcd_set_addr_win(0, y, LCD_H_RES - 1, y + rows - 1);
    if (err != ESP_OK) {
        return err;
    }
    return lcd_write_ram_1wire(s_fill_buf,
                               (size_t)LCD_H_RES * (size_t)rows * LCD_FB_BYTES_PER_PIXEL);
}

esp_err_t lcd_draw_rgb565_1wire(int y, int rows, const uint16_t *rgb565_be)
{
    if (!s_ready || s_io == NULL || s_fill_buf == NULL) {
        ESP_LOGE(TAG, "lcd_draw_rgb565_1wire: 尚未初始化");
        return ESP_ERR_INVALID_STATE;
    }
    if (rgb565_be == NULL || rows <= 0 || y < 0 || y >= LCD_V_RES) {
        return ESP_ERR_INVALID_ARG;
    }
    if (y + rows > LCD_V_RES) {
        rows = LCD_V_RES - y;
    }
    if (rows > LCD_FILL_LINES) {
        ESP_LOGE(TAG, "lcd_draw_rgb565_1wire: rows=%d 超过 %d", rows, LCD_FILL_LINES);
        return ESP_ERR_INVALID_ARG;
    }

    const size_t pix = (size_t)LCD_H_RES * (size_t)rows;
    memcpy(s_fill_buf, rgb565_be, pix * LCD_FB_BYTES_PER_PIXEL);
    return lcd_write_rows_1wire(y, rows);
}

esp_err_t lcd_draw_rgb565_1wire_area(int x, int y, int w, int h, const uint16_t *rgb565_be)
{
    if (!s_ready || s_io == NULL || s_fill_buf == NULL) {
        ESP_LOGE(TAG, "lcd_draw_rgb565_1wire_area: 尚未初始化");
        return ESP_ERR_INVALID_STATE;
    }
    if (rgb565_be == NULL || w <= 0 || h <= 0) {
        return ESP_ERR_INVALID_ARG;
    }
    /* 源缓冲按传入的 w 为行跨距；越界直接拒绝，避免裁剪后像素错位 */
    if (x < 0 || y < 0 || x >= LCD_H_RES || y >= LCD_V_RES ||
        x + w > LCD_H_RES || y + h > LCD_V_RES) {
        ESP_LOGE(TAG, "lcd_draw_rgb565_1wire_area: 区域越界 (%d,%d) %dx%d", x, y, w, h);
        return ESP_ERR_INVALID_ARG;
    }

    const uint16_t *src = rgb565_be;
    int y0 = y;
    const int y1 = y + h;
    while (y0 < y1) {
        int rows = y1 - y0;
        if (rows > LCD_FILL_LINES) {
            rows = LCD_FILL_LINES;
        }
        const size_t pix = (size_t)w * (size_t)rows;
        memcpy(s_fill_buf, src, pix * LCD_FB_BYTES_PER_PIXEL);
        esp_err_t err = lcd_set_addr_win(x, y0, x + w - 1, y0 + rows - 1);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "lcd_draw_rgb565_1wire_area 设窗口失败: %s", esp_err_to_name(err));
            return err;
        }
        err = lcd_write_ram_1wire(s_fill_buf, pix * LCD_FB_BYTES_PER_PIXEL);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "lcd_draw_rgb565_1wire_area RAMWR 失败: %s", esp_err_to_name(err));
            return err;
        }
        src += pix;
        y0 += rows;
    }
    return ESP_OK;
}

esp_err_t lcd_draw_test_pattern(void)
{
    uint8_t ram[32] = {0};

    if (!s_ready || s_io == NULL || s_panel == NULL) {
        ESP_LOGE(TAG, "lcd_draw_test_pattern: 尚未初始化");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "LCD_FW invert-on-v1: HD18004C18 + invert on + 一线 16-bit 洋红底 + y=176 绿横线");

    esp_err_t err = esp_lcd_panel_invert_color(s_panel, true);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "invert on 失败: %s", esp_err_to_name(err));
        return err;
    }

    err = lcd_restore_full_window();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "恢复全屏窗口失败: %s", esp_err_to_name(err));
        return err;
    }

    const uint8_t colmod55 = 0x55;
    err = lcd_tx_reg(LCD_CMD_COLMOD, &colmod55, 1);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "写 COLMOD 0x55 失败: %s", esp_err_to_name(err));
        return err;
    }

    (void)lcd_read_and_log("RDDID", LCD_CMD_RDDID, 8);
    (void)lcd_read_and_log("RDDPM", LCD_CMD_RDDPM, 8);
    (void)lcd_read_and_log("MADCTL", LCD_CMD_RDD_MADCTL, 8);
    (void)lcd_read_and_log("COLMOD", LCD_CMD_RDD_COLMOD, 8);

    err = lcd_fill_rect_1wire(0, 0, LCD_H_RES, LCD_V_RES, 255, 0, 255);
    if (err != ESP_OK) {
        return err;
    }
    /* 圆屏直径附近，8 行厚；洋红/绿和上一版青/红差开，避免 GRAM 残留误判 */
    err = lcd_fill_rect_1wire(0, 176, LCD_H_RES, 8, 0, 255, 0);
    if (err != ESP_OK) {
        return err;
    }

    err = lcd_set_addr_win(0, 180, 7, 180);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "RAMRD 前设窗口失败: %s", esp_err_to_name(err));
        return err;
    }
    err = lcd_rx_reg(LCD_CMD_RAMRD, ram, sizeof(ram));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "RAMRD 0x2E 失败: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "RAMRD y=180 前32字节 (绿横线 RGB565=0x07E0 总线 07 E0; 首字节可能 dummy):");
        ESP_LOG_BUFFER_HEX(TAG, ram, sizeof(ram));
    }

    err = lcd_restore_full_window();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "读回后恢复全屏窗口失败: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "LCD_FW invert-on-v1 完成: 预期整圆洋红、中间绿线");
    return ESP_OK;
}

esp_lcd_panel_handle_t lcd_get_panel(void)
{
    return s_panel;
}
