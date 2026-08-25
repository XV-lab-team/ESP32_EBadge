//TinyUSB 是一个开源的 USB 协议栈，专为嵌入式系统设计。它支持 设备模式 和 主机模式，
//并提供多种标准 USB 类驱动（如 MSC 大容量存储、CDC 虚拟串口、HID 人机交互设备等）,让开发者可以方便地在单片机上实现 USB 功能
//
//MSC是大容量存储
//
//
//使用USB模拟大容量存储器
//
#include "SDMMC_tusb_msc.h"

#include <errno.h>
#include <dirent.h>
#include <stdlib.h>
#include "sdkconfig.h"
#include "esp_console.h" 
#include "esp_check.h"
#include "esp_partition.h"
#include "driver/gpio.h"
#include "tinyusb.h"
#include "tinyusb_default_config.h"
#include "tinyusb_msc.h"
#include "sdmmc_cmd.h"
#include "diskio_impl.h"
#include "diskio_sdmmc.h"

#if SOC_USB_SERIAL_JTAG_SUPPORTED
#if !CONFIG_ESP_CONSOLE_SECONDARY_NONE
#warning "Channel for console secondary out must be set to NO."
#endif
#endif

static const char *TAG = "tusb_msc";

static esp_console_repl_t *repl = NULL;

/* 存储全局变量 */
tinyusb_msc_storage_handle_t storage_hdl = NULL;
tinyusb_msc_mount_point_t mp;
static SemaphoreHandle_t _wait_console_smp = NULL;

/* 模块初始化标志 */
static bool s_is_initialized = false;

/* TinyUSB描述符
   ********************************************************************* */
#define EPNUM_MSC       1
#define TUSB_DESC_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_MSC_DESC_LEN)
enum {
    ITF_NUM_MSC = 0,
    ITF_NUM_TOTAL
};

enum {
    EDPT_CTRL_OUT = 0x00,
    EDPT_CTRL_IN  = 0x80,

    EDPT_MSC_OUT  = 0x01,
    EDPT_MSC_IN   = 0x81,
};

static tusb_desc_device_t descriptor_config = {
    .bLength = sizeof(descriptor_config),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0200,
    .bDeviceClass = TUSB_CLASS_MISC,
    .bDeviceSubClass = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor = 0x303A, // 这是Espressif VID。这需要根据用户/客户进行更改
    .idProduct = 0x4002,
    .bcdDevice = 0x100,
    .iManufacturer = 0x01,
    .iProduct = 0x02,
    .iSerialNumber = 0x03,
    .bNumConfigurations = 0x01
};

//static 
uint8_t const msc_fs_configuration_desc[] = {
    // 配置编号、接口计数、字符串索引、总长度、属性、功率（mA）
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, TUSB_DESC_TOTAL_LEN, TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),
    // 接口号、字符串索引、EP输出和EP输入地址、EP大小
    TUD_MSC_DESCRIPTOR(ITF_NUM_MSC, 0, EDPT_MSC_OUT, EDPT_MSC_IN, 64),
};

static char const *string_desc_arr[] = {
    (const char[]) { 0x09, 0x04 },  // 0: 支持的语言为英语（0x0409）
    "TinyUSB",                      // 1: 制造商
    "TinyUSB Device",               // 2: 产品
    "123456",                       // 3: CDC虚拟串口
    "Example MSC",                  // 4. 大容量存储
};
/*********************************************************************** TinyUSB 描述符*/

#define BASE_PATH "/data" // 装载分区的基本路径

#define PROMPT_STR CONFIG_IDF_TARGET
static int console_unmount(int argc, char **argv);
static int console_read(int argc, char **argv);
static int console_write(int argc, char **argv);
static int console_size(int argc, char **argv);
static int console_status(int argc, char **argv);
static int console_exit(int argc, char **argv);

const esp_console_cmd_t cmds[] = {
    {
        .command = "read",
        .help = "read BASE_PATH/README.MD and print its contents",
        .hint = NULL,
        .func = &console_read,
    },
    {
        .command = "write",
        .help = "create file BASE_PATH/README.MD if it does not exist",
        .hint = NULL,
        .func = &console_write,
    },
    {
        .command = "size",
        .help = "show storage size and sector size",
        .hint = NULL,
        .func = &console_size,
    },
    {
        .command = "expose",
        .help = "Expose Storage to Host",
        .hint = NULL,
        .func = &console_unmount,
    },
    {
        .command = "status",
        .help = "Status of storage exposure over USB",
        .hint = NULL,
        .func = &console_status,
    },
    {
        .command = "exit",
        .help = "exit from application",
        .hint = NULL,
        .func = &console_exit,
    }
};

// 将装入点设置为应用程序，并按文件系统API列出BASE_PATH中的文件
static void _mount(void)
{
    ESP_LOGI(TAG, "Mount storage...");
    ESP_ERROR_CHECK(tinyusb_msc_set_storage_mount_point(storage_hdl, TINYUSB_MSC_STORAGE_MOUNT_APP));

    //列出此目录中的所有文件
    ESP_LOGI(TAG, "\nls command output:");
    struct dirent *d;
    DIR *dh = opendir(BASE_PATH);
    if (!dh) {
        if (errno == ENOENT) {
            // 如果找不到目录
            ESP_LOGE(TAG, "Directory doesn't exist %s", BASE_PATH);
        } else {
            // 如果目录不可读，则抛出错误并退出
            ESP_LOGE(TAG, "Unable to read directory %s", BASE_PATH);
        }
        return;
    }
    // 虽然下一个条目不可读，但打印目录文件
    while ((d = readdir(dh)) != NULL) {
        printf("%s\n", d->d_name);
    }
    return;
}

// 卸载存储（暴露给USB）
static int console_unmount(int argc, char **argv)
{
    ESP_ERROR_CHECK(tinyusb_msc_get_storage_mount_point(storage_hdl, &mp));
    if (mp == TINYUSB_MSC_STORAGE_MOUNT_USB) {
        ESP_LOGE(TAG, "Storage is already exposed");
        return -1;
    }
    ESP_LOGI(TAG, "Unmount storage...");
    ESP_ERROR_CHECK(tinyusb_msc_set_storage_mount_point(storage_hdl, TINYUSB_MSC_STORAGE_MOUNT_USB));
    return 0;
}

// 阅读BASE_PATH/README .MD并打印其内容
static int console_read(int argc, char **argv)
{
    ESP_ERROR_CHECK(tinyusb_msc_get_storage_mount_point(storage_hdl, &mp));
    if (mp == TINYUSB_MSC_STORAGE_MOUNT_USB) {
        ESP_LOGE(TAG, "Storage exposed over USB. Application can't read from storage.");
        return -1;
    }
    ESP_LOGD(TAG, "read from storage:");
    const char *filename = BASE_PATH "/README.MD";
    FILE *ptr = fopen(filename, "r");
    if (ptr == NULL) {
        ESP_LOGE(TAG, "Filename not present - %s", filename);
        return -1;
    }
    char buf[1024];
    while (fgets(buf, 1000, ptr) != NULL) {
        printf("%s", buf);
    }
    fclose(ptr);
    return 0;
}

// 创建文件BASE_PATH/README.MD（如果不存在）
static int console_write(int argc, char **argv)
{
    ESP_ERROR_CHECK(tinyusb_msc_get_storage_mount_point(storage_hdl, &mp));
    if (mp == TINYUSB_MSC_STORAGE_MOUNT_USB) {
        ESP_LOGE(TAG, "storage exposed over USB. Application can't write to storage.");
        return -1;
    }
    ESP_LOGD(TAG, "write to storage:");
    const char *filename = BASE_PATH "/README.MD";
    FILE *fd = fopen(filename, "r");
    if (!fd) {
        ESP_LOGW(TAG, "README.MD doesn't exist yet, creating");
        fd = fopen(filename, "w");
        fprintf(fd, "Mass Storage Devices are one of the most common USB devices. It use Mass Storage Class (MSC) that allow access to their internal data storage.\n");
        fprintf(fd, "In this example, ESP chip will be recognised by host (PC) as Mass Storage Device.\n");
        fprintf(fd, "Upon connection to USB host (PC), the example application will initialize the storage module and then the storage will be seen as removable device on PC.\n");
        fclose(fd);
    }
    return 0;
}

// 显示存储大小和扇区大小
static int console_size(int argc, char **argv)
{
    ESP_ERROR_CHECK(tinyusb_msc_get_storage_mount_point(storage_hdl, &mp));
    if (mp == TINYUSB_MSC_STORAGE_MOUNT_USB) {
        ESP_LOGE(TAG, "storage exposed over USB. Application can't access storage");
        return -1;
    }

    uint32_t sec_count;
    uint32_t sec_size;

    ESP_ERROR_CHECK(tinyusb_msc_get_storage_sector_size(storage_hdl, &sec_size));
    ESP_ERROR_CHECK(tinyusb_msc_get_storage_capacity(storage_hdl, &sec_count));

    // 计算大小（MB或KB）
    uint64_t total_bytes = (uint64_t)sec_size * sec_count;
    if (total_bytes >= (1024 * 1024)) {
        uint64_t total_mb = total_bytes / (1024 * 1024);
        printf("Storage Capacity %lluMB\n", total_mb);
    } else {
        uint64_t total_kb = total_bytes / 1024;
        printf("Storage Capacity %lluKB\n", total_kb);
    }
    return 0;
}

// 显示存储状态
static int console_status(int argc, char **argv)
{
    ESP_ERROR_CHECK(tinyusb_msc_get_storage_mount_point(storage_hdl, &mp));
    printf("storage exposed over USB: %s\n", (mp == TINYUSB_MSC_STORAGE_MOUNT_USB) ? "Yes" : "No");
    return 0;
}

// 退出应用程序
static int console_exit(int argc, char **argv)
{
    // 调用统一的停止函数
    tusb_msc_sdmmc_stop();
    return 0;
}

/**
 * @brief 存储装载更改回调
 *
 * @param 句柄存储句柄
 * @param 事件事件信息
 * @param 用户参数，在回调注册期间提供
 */
static void storage_mount_changed_cb(tinyusb_msc_storage_handle_t handle, tinyusb_msc_event_t *event, void *arg)
{
    switch (event->id) {
    case TINYUSB_MSC_EVENT_MOUNT_START:
        // 在卸载之前，请验证所有文件是否已关闭
        break;
    case TINYUSB_MSC_EVENT_MOUNT_COMPLETE:
        ESP_LOGI(TAG, "Storage mounted to application: %s", (event->mount_point == TINYUSB_MSC_STORAGE_MOUNT_APP) ? "Yes" : "No");
        break;
    case TINYUSB_MSC_EVENT_MOUNT_FAILED:
    case TINYUSB_MSC_EVENT_FORMAT_REQUIRED:
        ESP_LOGE(TAG, "Storage mount failed or format required");
        break;
    default:
        break;
    }
}

// ==================== 对外接口 ====================

esp_err_t tusb_msc_sdmmc_start(sdmmc_card_t *card)
{
    if (s_is_initialized) {
        ESP_LOGW(TAG, "MSC already started");
        return ESP_ERR_INVALID_STATE;
    }

    _wait_console_smp = xSemaphoreCreateBinary();
    if (_wait_console_smp == NULL) {
        ESP_LOGE(TAG, "Failed to create semaphore");
        return ESP_ERR_NO_MEM;
    }

    tinyusb_msc_storage_config_t storage_cfg = {
        .mount_point = TINYUSB_MSC_STORAGE_MOUNT_USB,
        .fat_fs = {
            .base_path = NULL,
            .config.max_files = 5,
            .format_flags = 0,
        },
    };
    storage_cfg.medium.card = card;   // 使用传入的卡句柄

    esp_err_t ret = tinyusb_msc_new_storage_sdmmc(&storage_cfg, &storage_hdl);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create storage: %s", esp_err_to_name(ret));
        vSemaphoreDelete(_wait_console_smp);
        _wait_console_smp = NULL;
        return ret;
    }

    ESP_ERROR_CHECK(tinyusb_msc_set_storage_callback(storage_mount_changed_cb, NULL));
    _mount();

    ESP_LOGI(TAG, "USB MSC initialization");

    tinyusb_config_t tusb_cfg = TINYUSB_DEFAULT_CONFIG();
    tusb_cfg.descriptor.device = &descriptor_config;
    tusb_cfg.descriptor.full_speed_config = msc_fs_configuration_desc;
    tusb_cfg.descriptor.string = string_desc_arr;
    tusb_cfg.descriptor.string_count = sizeof(string_desc_arr) / sizeof(string_desc_arr[0]);

    ret = tinyusb_driver_install(&tusb_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to install TinyUSB driver: %s", esp_err_to_name(ret));
        tinyusb_msc_delete_storage(storage_hdl);
        vSemaphoreDelete(_wait_console_smp);
        _wait_console_smp = NULL;
        return ret;
    }

    ESP_LOGI(TAG, "USB MSC initialization DONE");

    // 初始化控制台
    esp_console_repl_config_t repl_config = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    repl_config.prompt = PROMPT_STR ">";
    repl_config.max_cmdline_length = 64;

#if defined(CONFIG_ESP_CONSOLE_UART_DEFAULT) || defined(CONFIG_ESP_CONSOLE_UART_CUSTOM)
    esp_console_dev_uart_config_t hw_config = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
    ret = esp_console_new_repl_uart(&hw_config, &repl_config, &repl);
#elif defined(CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG) && defined(CONFIG_IDF_TARGET_ESP32P4)
    esp_console_dev_usb_serial_jtag_config_t hw_config = ESP_CONSOLE_DEV_USB_SERIAL_JTAG_CONFIG_DEFAULT();
    ret = esp_console_new_repl_usb_serial_jtag(&hw_config, &repl_config, &repl);
#else
#error Unsupported console type
#endif

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create REPL: %s", esp_err_to_name(ret));
        tinyusb_driver_uninstall();
        tinyusb_msc_delete_storage(storage_hdl);
        vSemaphoreDelete(_wait_console_smp);
        _wait_console_smp = NULL;
        return ret;
    }

    for (int count = 0; count < sizeof(cmds) / sizeof(esp_console_cmd_t); count++) {
        ESP_ERROR_CHECK(esp_console_cmd_register(&cmds[count]));
    }

    ret = esp_console_start_repl(repl);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start REPL: %s", esp_err_to_name(ret));
        repl->del(repl);
        repl = NULL;
        tinyusb_driver_uninstall();
        tinyusb_msc_delete_storage(storage_hdl);
        vSemaphoreDelete(_wait_console_smp);
        _wait_console_smp = NULL;
        return ret;
    }

    s_is_initialized = true;

    // 等待退出信号（由 tusb_msc_sdmmc_stop 或 console_exit 触发）
    xSemaphoreTake(_wait_console_smp, portMAX_DELAY);

    // 信号量被释放后，执行清理（如果还没被清理）
    if (s_is_initialized) {
        if (repl) {
            repl->del(repl);
            repl = NULL;
        }
        if (_wait_console_smp) {
            vSemaphoreDelete(_wait_console_smp);
            _wait_console_smp = NULL;
        }
        tinyusb_driver_uninstall();
        tinyusb_msc_delete_storage(storage_hdl);
        storage_hdl = NULL;
        s_is_initialized = false;
    }

    return ESP_OK;
}

esp_err_t tusb_msc_sdmmc_stop(void)
{
    if (!s_is_initialized) {
        ESP_LOGW(TAG, "MSC not started");
        return ESP_ERR_INVALID_STATE;
    }

    // 给出信号量，让控制台退出（如果正在运行）
    if (_wait_console_smp) {
        xSemaphoreGive(_wait_console_smp);
    }

    // 等待一小段时间，让控制台退出循环（可选）
    vTaskDelay(pdMS_TO_TICKS(100));

    // 执行清理（与 start 中的清理一致）
    if (repl) {
        repl->del(repl);
        repl = NULL;
    }
    if (_wait_console_smp) {
        vSemaphoreDelete(_wait_console_smp);
        _wait_console_smp = NULL;
    }
    tinyusb_driver_uninstall();
    tinyusb_msc_delete_storage(storage_hdl);
    storage_hdl = NULL;
    s_is_initialized = false;

    return ESP_OK;
}
