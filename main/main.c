#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "lcd.h"

static const char *TAG = "main";

void app_main(void)
{
    ESP_LOGI(TAG, "电子吧唧 LCD 点亮测试启动");
    ESP_LOGI(TAG, "不占用 USB PHY / GPIO19 / GPIO20，下载口应保持可用");
            vTaskDelay(pdMS_TO_TICKS(1000));
    esp_err_t err = lcd_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "lcd_init 失败: %s，进入空转以免看门狗复位导致 USB 掉线",
                 esp_err_to_name(err));
        while (1) {
            vTaskDelay(pdMS_TO_TICKS(2000));
            ESP_LOGE(TAG, "LCD 未就绪，请查上方错误；USB 仍应可重新下载");
        }
    }

    ESP_LOGI(TAG, "LCD_FW coord-oneshot-v1 开始画底部色带");
    err = lcd_draw_test_pattern();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "lcd_draw_test_pattern 失败: %s", esp_err_to_name(err));
    }

    while (1) {
        ESP_LOGI(TAG, "LCD_FW coord-oneshot-v1 仍在运行 (若没有这行日志=没烧到这版)");
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}
