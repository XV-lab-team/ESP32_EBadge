#include "usb_msc.h"

#include "esp_log.h"
#include "esp_private/usb_phy.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "hal/usb_serial_jtag_hal.h"
#include "hal/usb_serial_jtag_ll.h"
#include "led_script.h"
#include "sdmmc_fat.h"
#include "tinyusb.h"
#include "tinyusb_default_config.h"
#include "tinyusb_msc.h"

static const char *TAG = "usb_msc";

#define TUSB_DESC_TOTAL_LEN             (TUD_CONFIG_DESC_LEN + TUD_MSC_DESC_LEN)
#define USB_MSC_TASK_STACK              8192
#define USB_MSC_TASK_PRIO               5
#define USB_MSC_HOST_EJECT_WAIT_MS      4000
#define USB_MSC_REMOUNT_TRIES           3
#define USB_MSC_DELETE_TRIES            5

enum {
    ITF_NUM_MSC = 0,
    ITF_NUM_TOTAL
};

enum {
    EDPT_MSC_OUT = 0x01,
    EDPT_MSC_IN  = 0x81,
};

typedef struct {
    usb_msc_op_t op;
    esp_err_t err;
} usb_msc_result_t;

static tusb_desc_device_t s_desc_dev = {
    .bLength = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0200,
    .bDeviceClass = 0x00,
    .bDeviceSubClass = 0x00,
    .bDeviceProtocol = 0x00,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor = 0x303A,
    .idProduct = 0x4002,
    .bcdDevice = 0x100,
    .iManufacturer = 0x01,
    .iProduct = 0x02,
    .iSerialNumber = 0x03,
    .bNumConfigurations = 0x01
};

static const uint8_t s_msc_fs_desc[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, TUSB_DESC_TOTAL_LEN,
                          TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),
    TUD_MSC_DESCRIPTOR(ITF_NUM_MSC, 0, EDPT_MSC_OUT, EDPT_MSC_IN, 64),
};

static const char *s_string_desc[] = {
    (const char[]) { 0x09, 0x04 },
    "XV-lab",
    "EBadge UDisk",
    "0001",
    "SD MSC",
};

static tinyusb_msc_storage_handle_t s_storage;
static usb_phy_handle_t s_usj_phy;
static volatile int s_active;
static volatile int s_phy_stolen;
static volatile int s_host_has_medium;
static volatile int s_busy;
static esp_err_t s_last_remount_err = ESP_OK;
static QueueHandle_t s_cmd_q;
static QueueHandle_t s_done_q;
static TaskHandle_t s_task;

static esp_err_t usj_phy_restore(void)
{
    usb_phy_config_t cfg = {
        .controller = USB_PHY_CTRL_SERIAL_JTAG,
        .target = USB_PHY_TARGET_INT,
        .otg_mode = USB_PHY_MODE_DEFAULT,
    };
    esp_err_t err;

    if (s_usj_phy != NULL) {
        (void)usb_del_phy(s_usj_phy);
        s_usj_phy = NULL;
    }

    err = usb_new_phy(&cfg, &s_usj_phy);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "usb_new_phy(USJ) 失败: %s", esp_err_to_name(err));
        s_usj_phy = NULL;
        return err;
    }
#if SOC_USB_SERIAL_JTAG_SUPPORTED && USB_SERIAL_JTAG_LL_EXT_PHY_SUPPORTED
    usb_serial_jtag_hal_phy_set_external(NULL, false);
#endif
    ESP_LOGI(TAG, "USB PHY 已还给 Serial/JTAG，同一口可再下载");
    return ESP_OK;
}

static void fat_remount_if_needed(void)
{
    sdmmc_card_t *card = sdmmc_fat_get_card();
    int i;

    s_last_remount_err = ESP_OK;
    if (card == NULL || sdmmc_fat_is_mounted()) {
        return;
    }
    for (i = 0; i < USB_MSC_REMOUNT_TRIES; i++) {
        s_last_remount_err = sdmmc_fat_mount(card, SDMMC_FAT_MOUNT_POINT);
        if (s_last_remount_err == ESP_OK) {
            return;
        }
        ESP_LOGW(TAG, "FAT 重新挂载失败 (%s)，重试 %d/%d",
                 esp_err_to_name(s_last_remount_err), i + 1, USB_MSC_REMOUNT_TRIES);
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

static void msc_event_cb(tinyusb_msc_storage_handle_t handle, tinyusb_msc_event_t *event, void *arg)
{
    (void)handle;
    (void)arg;
    if (event == NULL) {
        return;
    }
    if (event->id == TINYUSB_MSC_EVENT_MOUNT_COMPLETE) {
        s_host_has_medium = (event->mount_point == TINYUSB_MSC_STORAGE_MOUNT_USB) ? 1 : 0;
    } else if (event->id == TINYUSB_MSC_EVENT_MOUNT_FAILED) {
        s_host_has_medium = 0;
    }
}

static void wait_host_eject(void)
{
    int waited = 0;

    if (s_storage != NULL) {
        (void)tinyusb_msc_set_storage_mount_point(s_storage, TINYUSB_MSC_STORAGE_MOUNT_APP);
    }
    while (s_host_has_medium && waited < USB_MSC_HOST_EJECT_WAIT_MS) {
        vTaskDelay(pdMS_TO_TICKS(50));
        waited += 50;
    }
    if (s_host_has_medium) {
        ESP_LOGW(TAG, "电脑未弹出 U 盘，超时后仍退出（可能损坏 FAT）");
    }
}

static esp_err_t msc_enter_sync(void)
{
    sdmmc_card_t *card;
    tinyusb_msc_storage_config_t storage_cfg = {
        .mount_point = TINYUSB_MSC_STORAGE_MOUNT_USB,
        .fat_fs = {
            .base_path = NULL,
            .config.max_files = 5,
            .config.format_if_mount_failed = false,
            .format_flags = 0,
        },
    };
    tinyusb_config_t tusb_cfg = TINYUSB_DEFAULT_CONFIG();
    esp_err_t err;

    if (s_active) {
        return ESP_OK;
    }
    if (s_phy_stolen) {
        err = usj_phy_restore();
        if (err != ESP_OK) {
            return err;
        }
        s_phy_stolen = 0;
    }

    card = sdmmc_fat_get_card();
    if (card == NULL) {
        ESP_LOGE(TAG, "没有 SD 卡，不切换 USB PHY");
        return ESP_ERR_NOT_FOUND;
    }

    err = led_script_stop();
    if (err == ESP_ERR_TIMEOUT) {
        ESP_LOGE(TAG, "led_script_stop 超时，拒绝卸 FAT");
        return err;
    }

    if (sdmmc_fat_is_mounted()) {
        err = sdmmc_fat_unmount();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "FAT unmount 失败: %s", esp_err_to_name(err));
            return err;
        }
    }

    if (s_usj_phy != NULL) {
        (void)usb_del_phy(s_usj_phy);
        s_usj_phy = NULL;
    }

    storage_cfg.medium.card = card;
    err = tinyusb_msc_new_storage_sdmmc(&storage_cfg, &s_storage);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "tinyusb_msc_new_storage_sdmmc: %s", esp_err_to_name(err));
        s_storage = NULL;
        fat_remount_if_needed();
        return err;
    }

    (void)tinyusb_msc_set_storage_callback(msc_event_cb, NULL);

    tusb_cfg.descriptor.device = &s_desc_dev;
    tusb_cfg.descriptor.full_speed_config = s_msc_fs_desc;
    tusb_cfg.descriptor.string = s_string_desc;
    tusb_cfg.descriptor.string_count = sizeof(s_string_desc) / sizeof(s_string_desc[0]);

    err = tinyusb_driver_install(&tusb_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "tinyusb_driver_install: %s", esp_err_to_name(err));
        (void)tinyusb_msc_delete_storage(s_storage);
        s_storage = NULL;
        (void)tinyusb_driver_uninstall();
        fat_remount_if_needed();
        return err;
    }

    s_host_has_medium = 1;
    s_phy_stolen = 1;
    s_active = 1;
    ESP_LOGW(TAG, "USB MSC 已把 SD 交给电脑；Serial/JTAG 暂时不可用。退出配置可恢复下载口。");
    return ESP_OK;
}

static esp_err_t msc_exit_sync(void)
{
    esp_err_t restore_err = ESP_OK;
    int i;

    if (!s_active && !s_phy_stolen) {
        return ESP_OK;
    }

    if (s_active) {
        wait_host_eject();
        if (s_storage != NULL) {
            for (i = 0; i < USB_MSC_DELETE_TRIES; i++) {
                if (tinyusb_msc_delete_storage(s_storage) == ESP_OK) {
                    break;
                }
                vTaskDelay(pdMS_TO_TICKS(200));
            }
            s_storage = NULL;
        }
        (void)tinyusb_driver_uninstall();
        s_active = 0;
        s_host_has_medium = 0;
    }

    if (s_phy_stolen) {
        restore_err = usj_phy_restore();
        if (restore_err == ESP_OK) {
            s_phy_stolen = 0;
        }
    }

    fat_remount_if_needed();
    return restore_err;
}

static void usb_msc_task(void *arg)
{
    usb_msc_op_t op;
    usb_msc_result_t result;

    (void)arg;
    for (;;) {
        if (xQueueReceive(s_cmd_q, &op, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        result.op = op;
        if (op == USB_MSC_OP_ENTER) {
            result.err = msc_enter_sync();
        } else {
            result.err = msc_exit_sync();
        }
        (void)xQueueOverwrite(s_done_q, &result);
    }
}

static esp_err_t usb_msc_worker_start(void)
{
    if (s_task != NULL) {
        return ESP_OK;
    }

    s_cmd_q = xQueueCreate(2, sizeof(usb_msc_op_t));
    s_done_q = xQueueCreate(1, sizeof(usb_msc_result_t));
    if (s_cmd_q == NULL || s_done_q == NULL) {
        ESP_LOGE(TAG, "创建 MSC 队列失败");
        return ESP_ERR_NO_MEM;
    }
    if (xTaskCreate(usb_msc_task, "usb_msc", USB_MSC_TASK_STACK, NULL,
                    USB_MSC_TASK_PRIO, &s_task) != pdPASS) {
        s_task = NULL;
        ESP_LOGE(TAG, "创建 MSC worker 失败");
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

static esp_err_t usb_msc_request(usb_msc_op_t op)
{
    esp_err_t err = usb_msc_worker_start();

    if (err != ESP_OK) {
        return err;
    }
    if (s_busy) {
        return ESP_ERR_INVALID_STATE;
    }
    s_busy = 1;
    if (xQueueSend(s_cmd_q, &op, 0) != pdTRUE) {
        s_busy = 0;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t usb_msc_request_enter(void)
{
    return usb_msc_request(USB_MSC_OP_ENTER);
}

esp_err_t usb_msc_request_exit(void)
{
    return usb_msc_request(USB_MSC_OP_EXIT);
}

int usb_msc_poll_result(usb_msc_op_t *op, esp_err_t *err)
{
    usb_msc_result_t result;

    if (s_done_q == NULL) {
        return 0;
    }
    if (xQueueReceive(s_done_q, &result, 0) != pdTRUE) {
        return 0;
    }
    s_busy = 0;
    if (op != NULL) {
        *op = result.op;
    }
    if (err != NULL) {
        *err = result.err;
    }
    return 1;
}

int usb_msc_is_active(void)
{
    return s_active;
}

int usb_msc_is_busy(void)
{
    return s_busy;
}

esp_err_t usb_msc_last_remount_err(void)
{
    return s_last_remount_err;
}
