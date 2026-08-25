# 三键（向上 / 确定 / 向下）— 给接手 AI

日期：2026-08-25  
工程概况：`工程基本情况.md`  
驱动代码：`components/BSP/KEY/`  
用户确认（第二次更正）：**KEY1 向上、KEY2 确定、KEY3 向下**；驱动已接入，**USB 模式设置页已绑定**（见 `docs/USB模式设置页-给接手AI.md`）。

> **驱动已接入。** 三个 MCU GPIO 输入，内部上拉，**低电平为按下**。  
> **不要 TinyUSB / USB-OTG。** 不要占用 GPIO19/20。不要给 GPIO46 加板上硬件上拉到 3.3 V。

---

## 0. 接手后立刻遵守

1. **先问再改**；**USB 下载绝不能丢**（GPIO19/20，`CONFIG_USJ_ENABLE_USB_SERIAL_JTAG=y`）。
2. 编译烧录用 **VS Code + 乐鑫 ESP-IDF 扩展**。IDF：`D:/Espressif/frameworks/esp-idf-v5.5.4/`。
3. 按键只走 `components/BSP/KEY/`，用官方 `driver/gpio`。不要引入第三方按键库，不要把这三脚当输出。
4. **绑定 UI 时改 `main/lvgl_app.c`（或新建输入胶水），不要改 KEY 驱动的引脚和极性。**
5. GPIO46 是 strapping（**向下**键）。固件启动后再关内部下拉、开上拉。板上不要加 3.3 V 硬件上拉。

---

## 1. 语义绑定（必须按这个表）

用户 2026-08-25 当面更正（以这次为准，**不要用第一次写反的表**）：

| 功能 | `key_id_t` | 别名 | 日志名 | 板上标签 | GPIO | 电气 |
|------|------------|------|--------|----------|------|------|
| **向上** | `KEY_1` | `KEY_UP` | `KEY1` | kaiguan1 | **GPIO18** | 输入 + 内部上拉，按下 = 低 |
| **确定** | `KEY_2` | `KEY_ENTER` | `KEY2` | kaiguan2 | **GPIO8** | 同上 |
| **向下** | `KEY_3` | `KEY_DOWN` | `KEY3` | kaiguan3 | **GPIO46** | 同上；strapping，复位时内部下拉 |

不要把 GPIO18/8/46 绑成别的功能，也不要和 SD / I2S / USB / LCD 脚混用。

绑 UI 时用 `key.h` 里的 `KEY_UP` / `KEY_ENTER` / `KEY_DOWN`，不要散落 GPIO 号。

---

## 2. 驱动已经提供什么

`key_init()` 在 `app_main()` 里、`io_virtual_start()` 之后调用。独立 FreeRTOS 任务 10 ms 轮询。

| 项 | 值 |
|----|----|
| 消抖 | 20 ms |
| 单击 | 短按松开后立刻上报（不再等双击窗口） |
| 长按 | 按住 800 ms（只报一次） |
| 另有 | `press` / `release` 边沿 |

API（`key.h`）：

- `key_init()`
- `key_is_pressed(id)` / `key_get_mask()`（bit0=向上，bit1=确定，bit2=向下）
- `key_set_callback(cb)`：`(key_id_t id, key_event_t evt)`
- 事件：`KEY_EVT_PRESS` / `RELEASE` / `CLICK` / `LONG_PRESS`
- letter-shell：`key` 打印当前按下/松开

USB 监视器示例：`KEY1 click` = 向上单击；`KEY2 click` = 确定单击；`KEY3 press` = 向下按下。

---

## 3. 已绑到 USB 模式设置页

实现在 `main/ui_usb_mode.c`。KEY 回调只往队列丢事件，LVGL 定时器里再改对象。详细四页表见 `docs/USB模式设置页-给接手AI.md`。

| 键 | 事件 | 设置页行为 |
|----|------|------------|
| 向上 `KEY_UP` | `KEY_EVT_CLICK` | 焦点上一项 |
| 向下 `KEY_DOWN` | `KEY_EVT_CLICK` | 焦点下一项 |
| 确定 `KEY_ENTER` | `KEY_EVT_CLICK` | 进入 / 确认焦点项 |
| 确定长按 | `KEY_EVT_LONG_PRESS` | 返回；在 U 盘就绪页则打开退出确认 |

1. 在 LVGL 锁内操作对象。回调来自 `key` 任务，**不要**在回调里直接改 LVGL。
2. 菜单浏览用 **单击**，不要用 `PRESS`。
3. 不要在 KEY 任务里画屏；不要占用 GPIO19/20；不要为了按键去改 strapping 上电电平。

---

## 4. GPIO46（向下键）注意

- 复位采样需要 IO46 为低才能正常 SPI 启动；内部默认下拉。下载模式还要求 IO0=0 且 IO46=0。
- **禁止**板上把 GPIO46 上拉到 3.3 V。
- 固件 `key_init()` 会 `gpio_pulldown_dis` + `gpio_pullup_en`。若向下键一上电就连续 `press` / `long_press`，是内部上拉没压过下拉，先问用户再改（不要擅自加硬件上拉）。

---

## 5. 不要做的事

- 不要把三键接到 AW9523 扩展 IO（它们是 MCU 脚）。
- 不要改 `KEY1_GPIO` / `KEY2_GPIO` / `KEY3_GPIO`，除非原理图证明接错。
- 不要把向上/确定/向下绑反。第一次文档写错过（曾把 KEY2 当向下、KEY3 当确认），**以本节表格为准**。
- 不要为按键去动 USB Serial/JTAG、TinyUSB、eFuse。
