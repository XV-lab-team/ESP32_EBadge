#include <dirent.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "io_virtual.h"
#include "key.h"
#include "lcd.h"
#include "led_script.h"
#include "lvgl_app.h"
#include "os_monitor.h"
#include "sdmmc_fat.h"
#include "shell.h"
#include "shell_app.h"
#include "ui_usb_mode.h"

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

static int shell_cmd_key(int argc, char *argv[])
{
    SHELL_TypeDef *sh = shellGetCurrent();
    int i;

    (void)argc;
    (void)argv;
    if (sh == NULL) {
        return 0;
    }
    for (i = 0; i < KEY_NUM; i++) {
        shellPrint(sh, "%s GPIO%d %s\r\n",
                   key_name((key_id_t)i),
                   (int)key_gpio((key_id_t)i),
                   key_is_pressed((key_id_t)i) ? "pressed" : "released");
    }
    shellPrint(sh, "mask=0x%02x\r\n", key_get_mask());
    return 0;
}
SHELL_EXPORT_CMD_EX(key, shell_cmd_key, "read KEY1-3", key);

static int shell_cmd_sd(int argc, char *argv[])
{
    SHELL_TypeDef *sh = shellGetCurrent();
    const sdmmc_card_t *card;
    DIR *dir;
    struct dirent *ent;
    uint64_t size_mb;

    (void)argc;
    (void)argv;
    if (sh == NULL) {
        return 0;
    }
    if (!sdmmc_fat_is_mounted()) {
        shellPrint(sh, "sd: not mounted\r\n");
        return -1;
    }
    card = sdmmc_fat_get_card();
    if (card != NULL) {
        size_mb = ((uint64_t)card->csd.capacity) * card->csd.sector_size / (1024 * 1024);
        shellPrint(sh, "mount=%s name=%s size=%" PRIu64 "MB freq=%dkHz\r\n",
                   sdmmc_fat_mount_path(),
                   card->cid.name,
                   size_mb,
                   card->real_freq_khz);
    } else {
        shellPrint(sh, "mount=%s\r\n", sdmmc_fat_mount_path());
    }

    dir = opendir(sdmmc_fat_mount_path());
    if (dir == NULL) {
        shellPrint(sh, "opendir failed\r\n");
        return -1;
    }
    while ((ent = readdir(dir)) != NULL) {
        shellPrint(sh, "  %s\r\n", ent->d_name);
    }
    closedir(dir);
    return 0;
}
SHELL_EXPORT_CMD_EX(sd, shell_cmd_sd, "SD card info and list", sd);

void app_main(void)
{
    /* USB Serial/JTAG shell first, so it stays up even if LCD init fails. */
    shell_app_start();

    /* After shell: dump goes to the same USB COM. Auto-refresh stays off. */
    if (os_monitor_start() != pdPASS) {
        ESP_LOGE(TAG, "os_monitor_start 失败");
    }

    esp_err_t nvs_err = nvs_flash_init();
    if (nvs_err == ESP_ERR_NVS_NO_FREE_PAGES || nvs_err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "nvs 需擦除后重初始化: %s", esp_err_to_name(nvs_err));
        nvs_err = nvs_flash_erase();
        if (nvs_err == ESP_OK) {
            nvs_err = nvs_flash_init();
        }
    }
    if (nvs_err != ESP_OK) {
        ESP_LOGW(TAG, "nvs_flash_init 失败: %s，模式不能掉电保存", esp_err_to_name(nvs_err));
    }

    /* Drive GPIO3 (AW9523 RSTN / strapping) as soon as possible. */
    if (io_virtual_start() != ESP_OK) {
        ESP_LOGE(TAG, "io_virtual_start 失败，LED 不可用；USB 下载口仍应可用");
    }

    if (led_script_init() != ESP_OK) {
        ESP_LOGE(TAG, "led_script_init 失败；USB 下载口仍应可用");
    }

    if (key_init() != ESP_OK) {
        ESP_LOGE(TAG, "key_init 失败；USB 下载口仍应可用");
    }

    /* GPIO18 上电按住：立刻清 NVS 配置模式，不必按到画面出来。 */
    ui_usb_mode_apply_boot_override();

    {
        esp_err_t sd_err = sdmmc_fat_start();
        if (sd_err != ESP_OK) {
            ESP_LOGW(TAG, "sdmmc_fat_start 失败: %s（无卡或非 FAT 时正常），继续启动",
                     esp_err_to_name(sd_err));
        }
    }

    ESP_LOGI(TAG, "电子吧唧 USB 模式设置页");
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

    err = lvgl_app_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "lvgl_app_start 失败: %s，进入空转以免看门狗复位导致 USB 掉线",
                 esp_err_to_name(err));
        while (1) {
            vTaskDelay(pdMS_TO_TICKS(2000));
            ESP_LOGE(TAG, "LVGL 未就绪；USB 仍应可重新下载");
        }
    }
}
