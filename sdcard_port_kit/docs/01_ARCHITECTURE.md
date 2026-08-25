# 架构与调用链

## 分层

原工程把 SD 相关代码拆成 4 块，从上到下：

```
main/main.c
        │  （当前已注释）
        ▼
APP/SDMMC_tusbmsc_fat          应用胶水：持有 card 句柄，互斥 MSC / FAT 标志
        │
        ├──► BSP/SDMMC         只做 SDMMC 硬件初始化，返回 sdmmc_card_t *
        │
        ├──► BSP/SDMMC_tusb_msc  把已有 card 交给 TinyUSB MSC
        │
        └──► BSP/SDMMC_FAT       【未完成草稿】本应挂 VFS FAT，现不可用
```

`sdmmc_card_t *` 是各层之间的唯一共享句柄。底层初始化一次，上层拿这个指针去挂 FAT 或开 USB MSC。

## 模块职责

### 1. `BSP/SDMMC`（可用，核心）

文件：`SDMMC.c` / `SDMMC.h`

- 配置 `sdmmc_host_t` + `sdmmc_slot_config_t`
- 调 `host.init()` → `sdmmc_host_init_slot()` → `sdmmc_card_init()`
- `malloc` 一块 `sdmmc_card_t`，成功则通过 `out_card` 返回
- `sdmmc_deinit()` 反初始化 host 并 `free(card)`
- **不**挂文件系统，**不**碰 USB

默认配置写在 `SDMMC.c` 的 `default_config`。`sdmmc_init(NULL, &card)` 使用这套默认值。

### 2. `BSP/SDMMC_tusb_msc`（可用，但会阻塞）

文件：`SDMMC_tusb_msc.c` / `SDMMC_tusb_msc.h`

基于 `espressif/esp_tinyusb` 2.x API：

1. `tinyusb_msc_new_storage_sdmmc()`：用传入的 `sdmmc_card_t *` 创建 MSC 存储
2. `_mount()`：把挂载点切到 APP，并 `opendir("/data")` 列目录
3. `tinyusb_driver_install()`：安装 USB 设备（MSC 描述符）
4. 启动 `esp_console` REPL，命令：`read` / `write` / `size` / `expose` / `status` / `exit`
5. `xSemaphoreTake(_wait_console_smp, portMAX_DELAY)` **一直阻塞**，直到 `tusb_msc_sdmmc_stop()` 或控制台 `exit`

控制台命令语义：

| 命令 | 作用 |
|------|------|
| `read` | 读 `/data/README.MD` |
| `write` | 若不存在则创建 `/data/README.MD` |
| `size` | 打印容量 |
| `expose` | 把存储交给 USB 主机（PC 当 U 盘） |
| `status` | 是否已暴露给 USB |
| `exit` | 停 MSC 并清理 |

MSC 与 APP 不能同时写同一块卡：暴露给 USB 后，本地 `fopen` 会失败。

### 3. `APP/SDMMC_tusbmsc_fat`（可用的胶水层）

文件：`SDMMC_tusbmsc_fat.c` / `.h`

```c
void SDMMC_tusbmsc_fat_init(void);      // 调 sdmmc_init
void SDMMC_tusbmsc_fat_SetTusbMsc(void); // 调 tusb_msc_sdmmc_start
```

内部 `flag`：

- `0` 空闲，可以开 MSC
- `1` 已开 MSC
- `2` 已开 SDMMC FAT（**没有任何函数会把 flag 设成 2**，FAT 路径没写完）

`SetTusbMsc()` 内部直接调用会阻塞的 `tusb_msc_sdmmc_start()`。

### 4. `BSP/SDMMC_FAT`（不可用）

- `sdmmc_fat.h` 几乎是空头文件，没有 API。
- `sdmmc_fat.c` 是 ESP-IDF `sdmmc` example 的残片：大量注释，且定义了 `void app_main(void)`。
- **禁止原样加入编译。** 若目标工程需要 FAT，按 `docs/05_INTEGRATION.md` 重写一个挂载函数。

## 原工程 CMake 组织方式

`components/BSP/CMakeLists.txt` 用 `SRC_DIRS` 把 LED/LCD/SDMMC 等全编进一个 BSP 组件：

```
SRC_DIRS: LED IIC LCD SYS_STATS SDMMC SDMMC_FAT SDMMC_tusb_msc NS2016 XL9555
REQUIRES: esp_lcd driver esp_psram esp_lcd_st7796 esp_tinyusb console
```

`components/APP/CMakeLists.txt`：

```
SRC_DIRS: SDMMC_tusbmsc_fat lvgl_app
REQUIRES: driver BSP lvgl
```

`main/main.c` include `"SDMMC_tusbmsc_fat.h"`，真正调用被注释。

移植时不必复制整个 BSP 组件。建议目标工程新建独立组件，例如 `components/sdcard/`，只放本包 `source/` 里需要的文件。

## TinyUSB 存储挂载点

`SDMMC_tusb_msc.c`：

```c
#define BASE_PATH "/data"
```

与 sdkconfig 一致：

```
CONFIG_TINYUSB_MSC_MOUNT_PATH="/data"
```

本地访问路径是 `/data/...`，不是 `/sdcard`。`sdmmc_fat.c` 草稿里写的是 `/sdcard`，两套约定不一致。移植时选定一个挂载点并全工程统一。

## 数据流（USB MSC）

```
PC USB Host
    ↕ USB MSC (Bulk IN/OUT EP 0x81 / 0x01)
TinyUSB MSC class
    ↕ tinyusb_msc_new_storage_sdmmc(card)
sdmmc_read_sectors / sdmmc_write_sectors
    ↕ SDMMC 1-bit, 40 MHz
SD 卡
```

当 `TINYUSB_MSC_STORAGE_MOUNT_APP` 时，esp_tinyusb 会把 FAT 注册到 VFS，APP 可用 POSIX 访问 `/data`。
当 `TINYUSB_MSC_STORAGE_MOUNT_USB` 时，扇区读写归 USB 主机，APP 不要碰这张卡。
