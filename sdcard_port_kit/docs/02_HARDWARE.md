# 硬件与引脚

原工程芯片：`CONFIG_IDF_TARGET="esp32s3"`。

接口是 **SDMMC Host**（ESP32-S3 SD/MMC 控制器），不是 SPI 模式的 SD 卡。目标芯片必须支持 `SOC_SDMMC_HOST_SUPPORTED`。ESP32（非 S3）的 SDMMC 引脚固定，ESP32-S3 可用 GPIO Matrix 任意映射。

## 默认引脚（写在 `SDMMC.h`）

| 信号 | GPIO | 说明 |
|------|------|------|
| CD   | 8    | Card Detect，低有效（IDF 默认） |
| CLK  | 16   | 时钟 |
| CMD  | 15   | 命令 |
| D0   | 17   | 数据线 0，1-bit 模式必需 |
| D1   | 18   | 4-bit 才用 |
| D2   | 6    | 4-bit 才用 |
| D3   | 7    | 4-bit 才用 |
| WP   | 未用 | 代码里写死 `gpio_wp = -1` |

## 默认电气参数（写在 `SDMMC.c` 的 `default_config`）

```c
.bus_width = 1,              // 实际跑 1-bit，不是 4-bit
.max_freq_khz = 40 * 1000,   // 40 MHz
.internal_pullup = true,     // 打开内部上拉
```

注意：D1/D2/D3 虽已定义，但 `bus_width = 1` 时不会按 4 线工作。若目标板是 4-bit 接线，移植时把 `bus_width` 改成 `4`。

## 上拉

代码会设 `SDMMC_SLOT_FLAG_INTERNAL_PULLUP`。ESP-IDF 官方说明：**内部上拉不够**，CLK/CMD/DAT 应有约 10 kΩ 外部上拉（很多模组板已做）。没有外部上拉时，初始化可能报 `ESP_ERR_TIMEOUT` / `ESP_ERR_INVALID_RESPONSE`。

## USB（仅 MSC 需要）

ESP32-S3 使用内置 USB-OTG。默认 PHY 引脚：

- USB D- = GPIO19
- USB D+ = GPIO20

本业务代码没有改 USB PHY 引脚。目标板若 USB 不在 19/20，需要在 TinyUSB / USB PHY 配置里改，而不是改 SDMMC 代码。

MSC 描述符（`SDMMC_tusb_msc.c`）：

- VID `0x303A`（Espressif）
- PID `0x4002`
- FS 配置：1 个 MSC 接口，EP `0x01` OUT / `0x81` IN，包长 64
- 设备类：Misc / IAD

## 移植时必须改的硬件相关量

在目标工程中打开 `SDMMC.h` / `SDMMC.c`，按原理图改：

1. `SDMMC_PIN_*` 宏
2. `default_config.bus_width`（1 或 4）
3. `default_config.max_freq_khz`（初始化不稳先降到 20 MHz 或 10 MHz）
4. 若没有 CD 脚：把 `cd_pin` 设为 `-1`（`SDMMC_SLOT_CONFIG_DEFAULT()` 的 CD 也是 GPIO 脚号，`-1` 表示不用）
5. 确认目标芯片有 SDMMC Host；若只有 SPI，需要整层改成 `SDSPI`（本包代码不能直接用）

## 与 LCD 的引脚冲突（原工程风险）

原工程同时有 RGB LCD（ST7796）和 SDMMC。移植到带屏幕的板子时，**逐脚核对 CLK/CMD/D0 是否和 LCD 数据/时钟复用**。本包不包含 LCD 代码，但引脚冲突会导致 SD 初始化失败或花屏。
