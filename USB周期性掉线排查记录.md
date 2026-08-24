# ESP32-S3 USB 周期性掉线排查记录

日期：2026-08-23  
模组：`ESP32-S3-WROOM-1-N16R8`  
USB：原生 OTG 脚（GPIO19 = DM / D-，GPIO20 = DP / D+）  
结论：**空 Flash 启动失败 → TG0 看门狗复位 → 片内 USB PHY 跟着掉线。** 烧入有效固件后恢复正常。

---

## 现象

板子插入电脑 USB 后，设备周期性出现 / 消失。  
3.3 V（SY8089A1AAC）用万用表看是稳的；屏和 SD 卡拆掉后仍掉线。  
芯片当时**没有烧录过程序**。

---

## 根因

ESP32-S3 的 USB 在芯片内部，复位就会断开枚举。  
Flash 为空时 ROM 读到的镜像头是 `0xffffffff`，无法进入应用，约 1 秒后 `TG0WDT_SYS_RST`，USB 掉线再重连。

外挂 CH340/CP2102 的普通 ESP32 上看不到这个现象（USB 在转接芯片上）；S3 走 OTG 脚时，复位 = USB 掉线。

供电被拉垮、屏/SD 电流不是这次的原因。

---

## 串口证据

通过 USB-Serial-JTAG 的 COM 口抓到（节选）：

```
ESP-ROM:esp32s3-20210327
Build:Mar 27 2021
rst:0x7 (TG0WDT_SYS_RST),boot:0x9 (SPI_FAST_FLASH_BOOT)
Saved PC:0x40049b21
invalid header: 0xffffffff
```

| 日志 | 含义 |
|---|---|
| `invalid header: 0xffffffff` | Flash 空（擦除态全 0xFF） |
| `SPI_FAST_FLASH_BOOT` | 正在从 Flash 启动，未进下载模式 |
| `TG0WDT_SYS_RST` | 读镜像失败后看门狗复位 |

`boot` 曾在 `0x19` / `0x9` / `0xb` 之间跳，说明部分 strapping 脚在漂（主要是 GPIO3）。

烧入本仓库程序后，复位环消失，USB 稳定，且仍可用同一 OTG 口再次下载。

---

## 当时接线（与本次相关）

| 脚 | 连接 | 备注 |
|---|---|---|
| EN | 0 Ω 上拉到 3.3 V | 无 RC、无复位键，只能拔插 USB 复位 |
| IO0 | BOOT，悬空 | 内部弱上拉，能启动；建议以后加 10 k 上拉 + 按键到地 |
| IO3 | AW9523 RSTN，无上拉 | GPIO3 无内部上下拉，上电不能高阻 |
| IO19 / IO20 | DM1 / DP1 | 正确，原生 USB |
| IO45 | 原接 ADC，已断开悬空 | strapping，上电为高会把 VDD_SPI 打成 1.8 V |
| IO46 | kaiguan3，悬空 | 内部下拉，本次可接受 |
| IO1 | SD_CD | 与仓库 demo 里「GPIO1 = LED」冲突 |
| IO17 | LED_CON | LED 应改到此脚 |
| IO35 / 36 / 37 | 原理图悬空 | N16R8 的 OPI PSRAM 在模组内部，对外无这些脚 |

已排除：IO45 拉高、屏/SD 负载、3.3 V 明显跌落。

---

## 处理步骤（已验证）

1. 关掉占用 COM 的串口助手。
2. **IO0 对地短路并按住**。
3. 拔掉 USB，再插上（代替 EN 复位）。
4. 保持 IO0 为低，用 USB JTAG/serial 口烧录：

```bash
idf.py -p COMx flash
```

5. 烧完松开 IO0，再拔插一次 USB。

成功后不应再刷屏 `invalid header`，USB 不再周期性掉线。

空 Flash 阶段必须按住 IO0。程序跑起来之后，USB-Serial-JTAG 还在，一般直接 `idf.py -p COMx flash` 即可，不必再按 BOOT。

---

## 本仓库配置与再次下载

当前工程**不会**占用 USB PHY，烧完仍可用 OTG 脚下载。

- 代码只翻转 GPIO1，未初始化 TinyUSB / USB-OTG。
- `CONFIG_USJ_ENABLE_USB_SERIAL_JTAG=y`
- 次控制台：`CONFIG_ESP_CONSOLE_SECONDARY_USB_SERIAL_JTAG=y`
- Secure Boot / Flash 加密未开，未烧禁止 USB 下载的 eFuse。

以后这些操作才会让「运行中一键烧录」失效（按住 IO0 + 复位通常还能进 ROM 下载）：

- 关闭 USB Serial/JTAG
- 初始化 TinyUSB / USB-OTG 抢走 PHY
- 把 IO19 / IO20 当普通 GPIO
- 烧 eFuse 关掉 USB JTAG / USB 下载

模组是 N16R8（16 MB 八线 Flash + 8 MB 八线 PSRAM）。PSRAM 已配 OCT；若以后出现烧完起不来，检查是否打开 `CONFIG_ESPTOOLPY_OCT_FLASH`。这不影响 USB 下载口本身。

---

## 后续硬件建议（未改板也可先用）

1. **EN**：改为 10 k 上拉 + 1 µF 到地，并加复位键到地。不要 0 Ω 死接到 3.3 V。
2. **IO0**：10 k 上拉 + BOOT 键到地。
3. **IO3**：10 k **下拉**到地；AW9523 RSTN 改到普通 GPIO（如 IO17 / IO47 / IO48）。
4. **IO45**：不要接上电可能为高的 ADC；必须用时保证复位瞬间为低。
5. **IO46**：开关不要上拉到 3.3 V；下载模式需要 IO0=0 且 IO46=0。
6. 仓库 LED demo 不要直接当成品用：GPIO1 是 SD_CD，LED 应走 IO17。
