# LCD 点亮排查记录 — 给接手 AI

日期：2026-08-24  
屏型号：**T180BV-C20-02**（金逸晨 GoldenMorning，1.8 寸圆屏，ST77916，QSPI，360×360）  
规格书：`docs/T180BV-C20-02-V1-产品规格书-20260311.pdf`  
规格摘要：`docs/T180BV-C20-02屏规格摘要.md`  
工程概况：`工程基本情况.md`

> 本文件记录 2026-08-24 当天 LCD 点亮调试的**全部操作、现象、结论和未完成项**。  
> 点亮**尚未成功**。请先读完再改代码。

---

## 0. 接手后立刻遵守

1. **先问再改**：改代码 / sdkconfig / 引脚 / 编译烧录前先说明方案和变砖风险，等用户同意。规则在 `.cursor/rules/ask-before-act-esp-official.mdc`。
2. **USB 下载绝不能丢**：本板只有原生 USB（GPIO19=DM / GPIO20=DP）能下载。不要 TinyUSB、不要占用 GPIO19/20、不要关 `CONFIG_USJ_ENABLE_USB_SERIAL_JTAG`、不要烧相关 eFuse。规则在 `.cursor/rules/usb-download-must-survive.mdc`。
3. **编译烧录用 VS Code + 乐鑫 ESP-IDF 扩展**，不要在 Cursor 里擅自跑 `idf.py build/flash/monitor`。IDF：`D:/Espressif/frameworks/esp-idf-v5.5.4/`。
4. **驱动栈保持乐鑫官方**：`esp_lcd` + 托管组件 `espressif/esp_lcd_st77916` ^2.0.2。可以**抄别人的寄存器表**进 `lcd_init_cmds.h`，不要把 Arduino / TFT_eSPI 引进工程。
5. 用户用 VS Code 扩展 Build → Flash。复位只能拔插 USB。

---

## 1. 硬件与驱动是否匹配（已核对）

### 匹配（芯片驱动选对了）

| 项目 | 屏规格 | 固件 |
|------|--------|------|
| IC | ST77916 | `esp_lcd_st77916` v2.0.2 |
| 接口 | QSPI，无 DC | `use_qspi_interface=1`，`dc_gpio_num=-1` |
| 写像素首字节 | 规格书第 9 节 `first byte=0x32` | 驱动 `LCD_OPCODE_WRITE_COLOR = 0x32` |
| 写寄存器 | 1 线 addr | 驱动 opcode `0x02` |
| 分辨率 | 360×RGB×360 | `LCD_H_RES/V_RES = 360` |
| QSPI 脚 | D0–D3 / SCL / CS | GPIO4/5/6/7 / 15 / 16 |
| 背光 | LED-A | GPIO17 PWM，高电平亮，默认 95% |

结论：**不要换成 ST7789 或普通 4 线 SPI。** 问题不在芯片型号。

### 不匹配 / 风险（花屏的真正方向）

| 项目 | 说明 |
|------|------|
| 厂商初始化表 | 规格书**没有** init 表。同一颗 ST77916、不同玻璃的 `0xF0` 厂商页（电源、gamma、栅极/源极）不同。 |
| 曾用 VoCat 表 | 乐鑫 ESP-VoCat 的 **UE018HV** 用 `0xF0=0x28`，Arduino_GFX 把这路标成 **1.5 寸**。T180BV 是 **1.8 寸**。 |
| 色深 | 封面写 16.7M、机械图写 262K、时序三种都支持。固件试过 16-bit 和 18-bit，**都花屏**。 |
| RESX | 规格写「must be applied to properly initialize」，低有效。仓库代码仍是 `LCD_PIN_RST=NC`（软复位）。用户说自己加过硬复位，**花屏依旧**。 |
| 电源 | VDD 典型 2.8 V（最大 3.3 V），VDDI 典型 1.8 V（最大 3.3 V）。板上按 3.3 V 理解，未用原理图核对。 |
| 原理图 | 仓库没有 PCB 原理图，脚定义来自规格书 + 排查记录，不保证走线和 FPC 一一对应。 |

`esp_lcd_st77916` 的 `tx_param` / `tx_color`（组件缓存路径，只读参考）：

`C:\Users\Admin\AppData\Local\Espressif\ComponentManager\Cache\service_d92d8f1e\espressif__esp_lcd_st77916_2.0.2_c0a565af\esp_lcd_st77916_spi.c`

- 命令：`opcode 0x02 << 24 | cmd << 8`
- 像素：`opcode 0x32 << 24 | 0x2C << 8`
- `bits_per_pixel=16` → COLMOD `0x55`，每像素 2 字节
- `bits_per_pixel=18` → COLMOD `0x66`，每像素 **3 字节**（R/G/B 各占一字节高 6 位）

---

## 2. 当前仓库代码状态（停在这里）

**1.8 寸 init 表已经写入，用户还没回报烧录后的画面。** 不要当成已经点亮成功。

| 文件 | 现状 |
|------|------|
| `main/main.c` | `lcd_init()` 后 `lcd_fill_color(255,0,0)`，然后空转。**不再**红绿蓝白循环。 |
| `components/BSP/LCD/lcd.h` | 16-bit RGB565，`lcd_fill_color(uint8_t r,g,b)`，RST=NC，PCLK=10 MHz |
| `components/BSP/LCD/lcd.c` | QSPI + ST77916；init 后 `invert_color(true)` + `disp_on_off(true)`；填色按 40 行条带 `draw_bitmap`；RGB565 做 `__builtin_bswap16` 再 DMA |
| `components/BSP/LCD/lcd_init_cmds.h` | **Arduino_GFX `st77916_180_init_operations`**（`0xF0=0x08`），只抄寄存器，无 Arduino 库。含模拟泵、屏外 1 行 `0x4C` kick、`0x21` invert、`0x3A=0x55`、`0x11`、`0x29` |
| `main/idf_component.yml` | `espressif/esp_lcd_st77916: "^2.0.2"` |

引脚（`lcd.h`，未改 USB）：

- D0=IO4，D1=IO5，D2=IO6，D3=IO7，SCK=IO15，CS=IO16，BL=IO17，RST=NC

---

## 3. 操作时间线与结果

下列实验都**没动 USB / TinyUSB / GPIO19/20 / eFuse / sdkconfig 下载相关项**。编译烧录由用户在 VS Code 里做。

### 更早（本日之前 / 同日更早会话，背景）

当时 `main` 是红/绿/蓝/白色块循环。init 用 VoCat `0xF0=0x28` 表。曾出现：

- 全黑：补了模拟电源泵（`0xA0/0xA3/0xA5`），并用寄存器 `0x4C` 填绿做命令通路测试。
- 注释里的判断：有绿 = 命令通；色块切不过去 = QSPI `0x32` 写显存有问题。
- 官方/组件默认的 `0x4C` 是填 **屏外 1 行 y=360**（`0x2B = 0x01,0x68,0x01,0x68`），不是全屏刷色。

### 实验 A — 只要整屏纯红（GRAM 写 RGB565）

- **改**：`main.c` 去掉色块循环，`lcd_fill_color(LCD_COLOR_RED)` 后空转。
- **用户现象**：不是纯色，红/黑/绿碎块，乱七八糟。
- **结论**：不是单纯红蓝反了。绿色很像 init 里 `0x4C` 填绿的残留，红色是部分像素写进去，黑色是空洞。命令通路和像素 DMA 叠在一起。

### 实验 B — 不用 GRAM，用 `0x4C` 全屏填红

- **改**：`0x4D=0xFC, 0x4E=0, 0x4F=0`，全屏窗口 `0–359`，`0x4C=01` 只等 **10 ms** 再停；关掉 invert；`main` 不再 `draw_bitmap`。
- **用户现象**：画面闪（**背光稳定**），仍不是纯色，感觉 RGB 三个颜色分开写、分散开。
- **结论**：`0x4C` 全屏 + 10 ms 会被截断。官方用法是屏外 1 行 kick。全屏扫 R/G/B 时停掉，就会闪、颜色散开。也说明 **不要再用 `0x4C` 当全屏纯色测试**。

### 实验 C — 恢复官方 `0x4C` kick + 再走 GRAM 写红

- **改**：`0x4C` 改回屏外 1 行；init 末尾 `0x3A=0x55`；打开 invert；`lcd_fill_color` 红。
- **用户**：未单独细报这一版画面；随后要求核对驱动是否适用，并说整体仍花。

### 实验 D — 核对驱动适用性（只分析，未改）

- **结论**：芯片 QSPI 驱动适用；VoCat 初始化表**不能**当成 T180BV 官方表。
- 建议两条：先改色深 18-bit，或补 RESX。用户选了**先改色深**。

### 实验 E — RGB666（18-bit / COLMOD `0x66`）

- **改**：`LCD_BITS_PER_PIXEL=18`，每像素 3 字节，`0x3A=0x66`，`lcd_fill_color(255,0,0)`。
- **用户现象**：**还是 RGB 分散 / 花屏**；**中心偏红、外侧偏白**。用户自己加了手动 RST，**仍不行**。
- **结论**：不是 16/18-bit 对不齐。中心/边缘色差更像 **VCOM / 电源泵 / 栅极源极表不对**（圆屏、厂商页错玻璃）。硬复位也排除不了「表不对」。

### 实验 F — 换成 1.8 寸 init 表（已写入，未确认画面）

- **改**：`lcd_init_cmds.h` 换成 Arduino_GFX **`st77916_180`**（`0xF0=0x08`，与组件默认 1.8 寸表同一路）。色深改回 RGB565。`main` 仍整屏写红。
- **用户现象**：**尚未回报**（写完表后用户要求把过程记进 md）。
- 接手后第一步：问用户这版烧进去是什么画面。

---

## 4. 已排除 / 不要再原地打转

| 已试或已否定 | 不要再当成第一怀疑 |
|--------------|-------------------|
| 芯片驱动选错（ST7789 / 普通 SPI） | 否，IC+QSPI+0x32 对得上 |
| 单纯 RGB/BGR 对调 | 对调应是整屏纯色变色，不是碎块 |
| 单纯字节序（bswap） | 错序一般是整屏错成另一种纯色 |
| 用 `0x4C` 全屏当纯色测试 | 会闪、RGB 散开，官方不是这么用的 |
| 只改 16-bit ↔ 18-bit | 用户已试，仍花，中心红外侧白 |
| 用户侧硬复位 | 用户说加了仍不行（仓库 RST 仍是 NC） |

---

## 5. 接手后建议顺序

1. **先问用户实验 F 烧完的画面**（1.8 寸 `st77916_180` 表）：
   - 整屏纯红 → 点亮成功，再谈 UI。
   - 整屏纯青 → 表基本对，关掉 `invert_color` / 去掉 `0x21`。
   - 仍中心红、外边白 / RGB 散 → 这套 1.8 寸公共表也不是金逸晨这块玻璃。
2. 若 F 仍失败：**向金逸晨要 T180BV-C20-02 的 QSPI 初始化代码**，只替换 `lcd_init_cmds.h`。
3. 不要引入 Arduino 库。`st77916_150`（`0xF0=0x28`，即旧 VoCat 表）已经用过，失败。
4. 若要动 RST：先问用户接到哪根 GPIO（避开 GPIO19/20 和 strapping：0/3/45/46），改 `LCD_PIN_RST`。规格是低有效；VoCat 板子曾用 `reset_active_high=1`，本板未确认。
5. 若怀疑 QSPI D1–D3 接反：命令（1 线）能通、像素（4 线）花。应用厂家表仍花时再查原理图/FPC。当前「中心红外侧白」更像模拟/gamma，不像单纯数据线接反（接反通常是全屏细条纹）。
6. 需要对照 1.5 寸表时：Arduino_GFX `st77916_150_init_operations`（`0xF0=0x28`），不要和 180 搞混。

公开表位置（只抄寄存器）：

- 1.8 寸：https://github.com/moononournation/Arduino_GFX/blob/master/src/display/Arduino_ST77916.h （`st77916_180_init_operations`）— **已在仓库**
- 1.5 寸 / VoCat：同文件 `st77916_150`；VoCat：`esp-bsp` `bsp/esp_vocat/priv_include/disp_init_data.h` — **已试过，花屏**
- 组件默认表：`esp_lcd_st77916_spi.c` 里 `vendor_specific_init_default`（也是 `0xF0=0x08` 一路，和 180 很像但不完全相同）

---

## 6. 关键代码行为（避免重复踩坑）

### `0x4C` 寄存器填色

- `0x4D/0x4E/0x4F` 是填色 RGB，`0x4C=1` 启动。
- 组件默认和 180 表：窗口设成 **y=360 一行**（屏外），delay 10 ms，再 `0x4C=0`，然后把 `0x2A/0x2B` 拉回 `0–359`。
- **禁止**把 `0x2B` 设成全屏再 `0x4C=1` 只等 10 ms。

### 像素写入

- `esp_lcd_panel_draw_bitmap` → CASET/RASET + RAMWR(0x2C) + QSPI 0x32。
- RGB565：小端 MCU 要 `bswap` 成总线上 `0xF8,0x00` 这种大端。
- RGB666：3 字节/像素，**不要**再按 uint16 bswap。
- `tx_color` 是异步 DMA；同缓冲刷纯色一般没问题。最后一笔可能还在飞，空转 delay 即可。

### invert

- 180 表里已有 `0x21`。`lcd.c` 里又调用了 `esp_lcd_panel_invert_color(true)`（仍是 INVON，不是关）。
- 若出现整屏纯青而不是纯红：先关 invert 再判断。

### VoCat vs 180 开头（认表用）

```
VoCat / 1.5 寸：  0xF0=0x28, 0xF2=0x28, 0x73=0xF0, ...
1.8 寸 / 180：    0xF0=0x08, 0xF2=0x08, 0x9B=0x51, 0x86=0x53, ...
```

当前仓库是 **第二行**。

---

## 7. 不要改的东西

- GPIO19 / GPIO20、USB Serial/JTAG、TinyUSB、eFuse、Secure Boot、Flash 加密
- 不要把控制台改成仅 UART0（板上 UART0 没接到电脑）
- 不要为了 LCD 去改 strapping（GPIO0/3/45/46）
- 不要在 Cursor 终端执行 `idf.py flash`

---

## 8. 用户原话摘要（便于对照）

1. 「改成 lcd 一直红色，感觉颜色不对劲」→ 做成整屏红。
2. 「不是纯色，红黑绿块块，乱七八糟」→ 实验 A。
3. 「屏幕发闪（背光稳定），好像 RGB 三个色分散了」→ 实验 B。
4. 「感觉是驱动问题，检查屏幕和驱动适不适用」→ 实验 D，结论：IC 驱动适用，init 表不一定适用。
5. 「先改色深试试」→ 实验 E，失败。
6. 「还是 RGB 分散/花屏，中心偏红外侧偏白，加了手动 rst 还是不行」→ 排除色深和用户侧 RST。
7. 「把刚才所有操作和结果记到 md，让其他 AI 接替」→ 本文件。
