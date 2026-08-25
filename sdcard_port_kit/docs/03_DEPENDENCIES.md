# 依赖、CMake、menuconfig

## ESP-IDF 与组件版本

| 项 | 原工程值 |
|----|----------|
| IDF | 5.5.x（sdkconfig 头注释为 5.5.4） |
| 目标芯片 | esp32s3 |
| `espressif/esp_tinyusb` | `^2.1.1`（`main/idf_component.yml`） |
| TinyUSB 本体 | 由 esp_tinyusb 传递依赖 `tinyusb >= 0.17.0~2` |

MSC 代码用的是 **esp_tinyusb 2.x** API：

- `tinyusb_msc.h`：`tinyusb_msc_new_storage_sdmmc`
- `tinyusb_default_config.h`：`TINYUSB_DEFAULT_CONFIG()`
- `tinyusb_config_t` 的 `descriptor.device / full_speed_config / string`

**不要**用 1.x 的 `tinyusb_msc_storage_init_sdmmc()`（已 deprecated）。目标工程若已有旧版 esp_tinyusb，先升级到 2.1.x。

## 目标工程必须声明的组件

### 只做 SD 卡初始化

CMake `REQUIRES`：

```
driver
sdmmc
```

### 再加本地 FAT

```
driver
sdmmc
fatfs
```

头文件：`esp_vfs_fat.h`

### 再加 USB MSC（与原工程一致）

```
driver
sdmmc
fatfs
esp_tinyusb
console
```

`console` 是因为 `SDMMC_tusb_msc.c` 里用了 `esp_console` REPL。若移植时删掉控制台代码，可以不再依赖 `console`。

BSP 原 CMake **没有显式写 `sdmmc` / `fatfs`**，靠 `esp_tinyusb` 传递。目标工程建议写全，避免链接期缺符号。

## Component Manager

在目标工程 `main/idf_component.yml` 或对应组件的 yml 中加入（只要 MSC）：

```yaml
dependencies:
  idf: '>=5.0'
  espressif/esp_tinyusb: ^2.1.1
```

然后在工程根执行 `idf.py reconfigure`。不要拷贝原工程 `managed_components/`。

完整片段见 `snippets/idf_component.yml`。

## menuconfig / sdkconfig 必选项

原工程已打开、移植 MSC 时必须对齐的项：

```
CONFIG_TINYUSB_MSC_ENABLED=y
CONFIG_TINYUSB_MSC_BUFSIZE=512
CONFIG_TINYUSB_MSC_MOUNT_PATH="/data"

CONFIG_ESP_CONSOLE_UART_DEFAULT=y
CONFIG_ESP_CONSOLE_SECONDARY_NONE=y
```

`SDMMC_tusb_msc.c` 有编译期检查：

```c
#if SOC_USB_SERIAL_JTAG_SUPPORTED
#if !CONFIG_ESP_CONSOLE_SECONDARY_NONE
#warning "Channel for console secondary out must be set to NO."
#endif
#endif
```

以及：

```c
#if defined(CONFIG_ESP_CONSOLE_UART_DEFAULT) || defined(CONFIG_ESP_CONSOLE_UART_CUSTOM)
    // uart repl
#elif defined(CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG) && defined(CONFIG_IDF_TARGET_ESP32P4)
    // 仅 ESP32-P4
#else
#error Unsupported console type
#endif
```

因此在 ESP32-S3 上，控制台必须是 **UART**，不能是 USB Serial/JTAG。原因：USB-OTG 已被 MSC 占用。

FATFS 原工程相关项（可按需改）：

```
CONFIG_FATFS_VOLUME_COUNT=2
CONFIG_FATFS_SECTOR_4096=y
CONFIG_FATFS_CODEPAGE_437=y
CONFIG_FATFS_LFN_NONE=y
```

`FATFS_LFN_NONE` 表示 **不支持长文件名**。若 PC 写入长文件名，设备端可能看不到。移植时若要兼容 Windows 长文件名，改成 `CONFIG_FATFS_LFN_HEAP` 或 `CONFIG_FATFS_LFN_STACK`，并考虑 `CONFIG_FATFS_CODEPAGE_936`（简体中文）。

建议的 `sdkconfig.defaults` 见 `snippets/sdkconfig.defaults`。

## 建议的目标组件 CMake

见 `snippets/CMakeLists_sdcard.txt`。推荐独立组件 `components/sdcard`，不要把 `sdmmc_fat.c` 加进 `SRCS`。

原工程 `component_compile_options` 有一处疑似笔误：

```
-Wno-error=format=-Who-format
```

不要复制这行。APP 组件里写的是 `-Wno-error=format -Wno-format`，这个才对。

## 头文件依赖关系

```
SDMMC_tusbmsc_fat.c
    #include "sdmmc.h"            → BSP/SDMMC
    #include "SDMMC_tusb_msc.h"   → BSP/SDMMC_tusb_msc
    #include "sdmmc_fat.h"        → 空头文件，可删

SDMMC_tusb_msc.c
    #include "tinyusb.h"
    #include "tinyusb_default_config.h"
    #include "tinyusb_msc.h"
    #include "esp_console.h"
    #include "diskio_impl.h"
    #include "diskio_sdmmc.h"

SDMMC.c
    #include "driver/sdmmc_host.h"
    #include "sdmmc_cmd.h"
```

`diskio_*.h` 来自 IDF `fatfs` 组件。即使 APP 自己不直接挂 FAT，MSC 路径也会用到 fatfs diskio。
