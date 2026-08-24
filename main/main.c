#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
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

void app_main(void)
{
    /* USB Serial/JTAG shell first, so it stays up even if LCD init fails. */
    shell_app_start();

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
