# AW9523 虚拟 IO 移植记录 — 给接手 AI

日期：2026-08-24  
工程概况：`工程基本情况.md`  
移植包（只读参考）：`虚拟IO与扩展IO移植/`  
工程内代码：`components/BSP/EXIO/`

> **已接入。** 1 片 **AW9523BTQR**，I2C 走官方 `driver/i2c_master`，逻辑层是 `io_virtual` 队列任务。  
> **RGB 为共阳，灯规格每路最多 20 mA。当前电流档不改：`ledmode_isel = 3`（1/4 Imax，满亮约 9 mA）。**  
> **不要 TinyUSB / USB-OTG。** 不要占用 GPIO19/20。

---

## 0. 接手后立刻遵守

1. **先问再改**；**USB 下载绝不能丢**（GPIO19/20，`CONFIG_USJ_ENABLE_USB_SERIAL_JTAG=y`）。
2. 编译烧录用 **VS Code + 乐鑫 ESP-IDF 扩展**。IDF：`D:/Espressif/frameworks/esp-idf-v5.5.4/`。
3. **不要改** `aw9523.c` / `exio.c` 的寄存器行为。脚表、地址、电流档只改 `io_virtual.c` / `aw9523_port.h`。
4. 不要创建 `io_virtual_chip_test_task`（会和正式任务抢 I2C）。
5. 虚拟 IO 不要拿去做 PWM / 高频翻转。

---

## 1. 板级接法

| 信号 | GPIO | 说明 |
|------|------|------|
| SCL | **14** | I2C0 |
| SDA | **21** | I2C0 |
| RSTN | **3** | 推挽；strapping，上电不宜高阻。固件启动立刻配成输出 |
| 片数 | 1 | AW9523BTQR |
| AD1 / AD0 | **1 / 1** | 7 位地址 **0x5B**（8 位写地址 `0xB6`） |
| I2C 速率 | 400 kHz | 内部上拉作备份 |

P1.5 悬空，不进 GPIO/LED 表。没有扩展 GPIO 输出（`VIO_GPIO_NUM = 0`）。

---

## 2. LED 脚表（共阳 RGB × 5）

AW9523 走 **LED 电流沉**。共阳：DIM 0=灭，255=该档最大电流。  
逻辑 ID 下标 = `exio_led_pin_cfg[]` 下标。

| id | 灯 | 芯片脚 | id | 灯 | 芯片脚 |
|----|----|--------|----|----|--------|
| 0 | R1 | P1.7 | 9 | R4 | P0.6 |
| 1 | G1 | P1.6 | 10 | G4 | P0.5 |
| 2 | B1 | P1.4 | 11 | B4 | P0.7 |
| 3 | R2 | P1.3 | 12 | R5 | P1.0 |
| 4 | G2 | P0.0 | 13 | G5 | P1.1 |
| 5 | B2 | P0.3 | 14 | B5 | P1.2 |
| 6 | R3 | P0.1 | | | |
| 7 | G3 | P0.2 | | | |
| 8 | B3 | P0.4 | | | |

业务 API：`io_virtual_led_set` / `io_virtual_rgb_set(1~5, r, g, b)`。只投递队列，不要自己调 `aw9523_*`。

Shell（UTF-8，关本地回显）：

```text
vio_rgb 1 128 0 0
vio_led 0 128
```

启动日志应有 `exio_init ok` 和 `I2C 0x5b ACK`（`0x58~0x5a` nack 正常）。

---

## 3. 电流：20 mA 灯 vs 驱动档（2026-08-24 已核对，先不改）

外接 RGB **每路最多 20 mA**。

AW9523B 手册：LED 模式是共阳恒流；默认 **Imax 典型 37 mA**（最大约 43 mA）。GCR `ISEL`（代码里 `ledmode_isel`）决定 DIM=255 时的上限，DIM 0~255 只在这一档内线性调：

| `ledmode_isel` | 档位 | DIM=255 大约电流 | 相对 20 mA |
|----------------|------|------------------|------------|
| 0 | Imax | ~37 mA（最差可到 ~43 mA） | **会超，禁止** |
| 1 | 3/4 | ~28 mA | **会超，禁止** |
| 2 | 2/4 | ~18.5 mA（芯片偏上限时可能略超 20） | 临界 |
| **3（当前）** | **1/4** | **~9.3 mA（最差约 11 mA）** | **安全** |

**用户决定：电流档先不改，保持 `ledmode_isel = 3`。**  
`vio_led` / `vio_rgb` 写 255 也只有约 9 mA，不是 20 mA。

坑：`aw9523_init` 在 `ledmode_isel == 0` 时**不写 GCR**。Bring-up 必须用 1~3。若以后要更亮，最多改到 **2**，不要改成 0 或 1。改档只动 `io_virtual.c` 里 `aw9523_dev_cfg.chip[].ledmode_isel`。

---

## 4. 相对移植包的必要补丁（只在 `components/BSP/EXIO/`）

| 文件 | 补丁 | 原因 |
|------|------|------|
| `aw9523_port.h` / `.c` | HAL 薄封装：`gpio_set_level` + `i2c_master_transmit`（8 位地址右移成 7 位） | 目标是 ESP-IDF，不是 STM32 HAL |
| RST 宏 | 低/高各 `vTaskDelay(1 ms)` | 原驱动无延时，S3 上可能太快 |
| `io_virtual.c` | **不要** `vTaskSuspendAll()` 包住 `exio_init` | ESP-IDF I2C 依赖调度器 |
| 任务栈 | **4096 字节** | IDF 的 `xTaskCreate` 单位是字节，不是 STM32 的 word |

`虚拟IO与扩展IO移植/` 不要当产品代码改。

---

## 5. 明确不要做的

- 不要把 `examples/本工程_io_virtual.c` 整文件贴进来（教具三片脚表）
- 不要把 `ledmode_isel` 改成 0 或 1（会超过 20 mA 灯）
- 不要占用 GPIO19/20，不要关 USB Serial/JTAG
- 不要把 GPIO3 配成高阻；启动后必须是推挽
