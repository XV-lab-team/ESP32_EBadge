#include <stdio.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "io_virtual.h"
#include "lcd.h"
#include "lcd_test.h"
#include "shell.h"
#include "shell_app.h"

static const char *TAG = "main";

static int shell_cmd_id(int argc, char *argv[])
{
    SHELL_TypeDef *sh = shellGetCurrent();

    (void)argc;
    (void)argv;
    if (sh) {
        shellPrint(sh, "board ok\r\n");
    }
    return 0;
}
SHELL_EXPORT_CMD_EX(id, shell_cmd_id, "board id", id);

static int shell_cmd_vio_led(int argc, char *argv[])
{
    SHELL_TypeDef *sh = shellGetCurrent();
    int id;
    int val;
    HAL_StatusTypeDef res;

    if (argc < 3) {
        if (sh) {
            shellPrint(sh, "usage: vio_led <id 0-14> <0-255>\r\n");
            shellPrint(sh, "  0-2 R1G1B1  3-5 R2G2B2  6-8 R3G3B3\r\n");
            shellPrint(sh, "  9-11 R4G4B4  12-14 R5G5B5\r\n");
        }
        return -1;
    }
    id = atoi(argv[1]);
    val = atoi(argv[2]);
    if (id < 0 || id >= VIO_LED_NUM || val < 0 || val > 255) {
        if (sh) {
            shellPrint(sh, "bad args\r\n");
        }
        return -1;
    }
    res = io_virtual_led_set((vio_led_id_t)id, (uint8_t)val);
    if (sh) {
        shellPrint(sh, "vio_led %d %d -> %d\r\n", id, val, (int)res);
    }
    return (res == HAL_OK) ? 0 : -1;
}
SHELL_EXPORT_CMD_EX(vio_led, shell_cmd_vio_led, "set AW9523 LED dim", vio_led);

static int shell_cmd_vio_rgb(int argc, char *argv[])
{
    SHELL_TypeDef *sh = shellGetCurrent();
    int n;
    int r;
    int g;
    int b;
    HAL_StatusTypeDef res;

    if (argc < 5) {
        if (sh) {
            shellPrint(sh, "usage: vio_rgb <1-5> <r> <g> <b>\r\n");
        }
        return -1;
    }
    n = atoi(argv[1]);
    r = atoi(argv[2]);
    g = atoi(argv[3]);
    b = atoi(argv[4]);
    if (n < 1 || n > 5 || r < 0 || r > 255 || g < 0 || g > 255 || b < 0 || b > 255) {
        if (sh) {
            shellPrint(sh, "bad args\r\n");
        }
        return -1;
    }
    res = io_virtual_rgb_set((uint8_t)n, (uint8_t)r, (uint8_t)g, (uint8_t)b);
    if (sh) {
        shellPrint(sh, "vio_rgb %d %d %d %d -> %d\r\n", n, r, g, b, (int)res);
    }
    return (res == HAL_OK) ? 0 : -1;
}
SHELL_EXPORT_CMD_EX(vio_rgb, shell_cmd_vio_rgb, "set RGB LED 1-5", vio_rgb);

void app_main(void)
{
    /* USB Serial/JTAG shell first, so it stays up even if LCD init fails. */
    shell_app_start();

    /* Drive GPIO3 (AW9523 RSTN / strapping) as soon as possible. */
    if (io_virtual_start() != ESP_OK) {
        ESP_LOGE(TAG, "io_virtual_start 失败，LED 不可用；USB 下载口仍应可用");
    }

    ESP_LOGI(TAG, "电子吧唧 LCD 点亮测试启动");
    ESP_LOGI(TAG, "不占用 USB PHY / GPIO19 / GPIO20，下载口应保持可用");

    esp_err_t err = lcd_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "lcd_init 失败: %s，进入空转以免看门狗复位导致 USB 掉线",
                 esp_err_to_name(err));
        while (1) {
            vTaskDelay(pdMS_TO_TICKS(2000));
            ESP_LOGE(TAG, "LCD 未就绪，请查上方错误；USB 仍应可重新下载");
        }
    }

    err = lcd_test_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "lcd_test_start 失败: %s，进入空转以免看门狗复位导致 USB 掉线",
                 esp_err_to_name(err));
        while (1) {
            vTaskDelay(pdMS_TO_TICKS(2000));
        }
    }
}
