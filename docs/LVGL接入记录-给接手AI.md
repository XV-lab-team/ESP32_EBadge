# LVGL 接入记录 — 给接手 AI

日期：2026-08-25  
工程概况：`工程基本情况.md`  
LCD 点亮（一线写屏前提）：`docs/LCD点亮排查记录-给接手AI.md`  
三键尚未绑到 LVGL：`docs/按键绑定-给接手AI.md`  
当前板级 UI：`docs/USB模式设置页-给接手AI.md`

> **已接入并经用户确认。** VS Code ESP-IDF 编译通过；屏上 **黑底白字 `Hello LVGL`**（一线路径）。  
> **2026-08-25 下午：** LVGL flush 改为官方 `esp_lcd_panel_draw_bitmap` / `tx_color` DMA（opcode `0x32` + 四线 QIO）。Init 仍是 HD18004C18 + INVON。点亮阶段 QIO 花屏发生在 **init 还不对的时候**；正确 init 之后 QIO **尚未验收**。一线函数仍保留作退路。  
> **不要**用 `lvgl_port_add_disp()`（仍自己 `flush_cb`，便于等 DMA；FULL 缓冲禁止原地 bswap）。  
> **不要 TinyUSB / USB-OTG / USB HID。** 会抢片内 USB PHY，下载口会没。

---

## 0. 接手后立刻遵守

1. **先问再改**；**USB 下载绝不能丢**（GPIO19/20，`CONFIG_USJ_ENABLE_USB_SERIAL_JTAG=y`）。不要为 LVGL 去动 USB、控制台、strapping、eFuse。
2. 编译烧录用 **VS Code + 乐鑫 ESP-IDF 扩展**。IDF：`D:/Espressif/frameworks/esp-idf-v5.5.4/`。不要在 Cursor 里擅自跑 `idf.py build/flash`。
3. 组件只用乐鑫官方：`espressif/esp_lvgl_port` + `lvgl/lvgl` ^9 + 现有 `esp_lcd` / `esp_lcd_st77916`。不要引进 Arduino / TFT_eSPI / 第三方 UI 库。
4. 改界面只动 `main/lvgl_app.c`（或另加 UI 文件）。**不要**把 LVGL 塞进 `components/BSP/LCD/`。
5. 所有 `lv_*` 调用（除 flush 回调本身）必须包在 `lvgl_port_lock` / `lvgl_port_unlock` 里。
6. 不要退回 VoCat / `st77916_180` / 组件默认 init。当前 flush 走 QIO `draw_bitmap`；若花屏，改回 `lcd_draw_rgb565_1wire_area()`，不要换 init 表。

---

## 1. 为什么这样接

这块屏点亮成功的组合是：HD18004C18 init + RGB565 16-bit + **INVON**。当时像素走一线 opcode `0x02` 写 `0x2C`，是为了用 `RAMRD` 证明 GRAM 写得进。点亮阶段的花屏（隔点 / 隔行）发生在 **init 表还不对** 的时候；QIO 在正确 init 之后没有复测过。

`espressif/esp_lvgl_port` 的 `lvgl_port_add_disp()` 默认 flush 也会调 `esp_lcd_panel_draw_bitmap()`，但会绑死 port 的 flush，不便自己 bswap 和等 `s_color_done`。因此仍自己建 display。

| 用官方 port 做什么 | 自己做什么 |
|--------------------|------------|
| `lvgl_port_init()`：LVGL 任务、tick、锁 | `lv_display_create` + 自定义 `flush_cb` |
| `lvgl_port_lock` / `unlock` | flush 里 `lcd_draw_rgb565()`（`RGB565_SWAPPED`，不再原地 bswap） |

**不要**调用 `lvgl_port_add_disp()`。  
**不要**把 `esp_lvgl_port` 的 USB HID 加进依赖（会拉 `usb_host_hid`，抢 USB PHY）。

---

## 2. 目录与职责

| 路径 | 职责 | 改不改 |
|------|------|--------|
| `main/idf_component.yml` | `esp_lcd_st77916`、`esp_lvgl_port ^2.6.3`、`lvgl/lvgl ^9` | 版本钉住；不要去掉 lvgl 的 `^9` |
| `main/lvgl_app.c` / `lvgl_app.h` | port 初始化、FULL flush、启动 USB 设置页 | 板级 UI 在 `ui_usb_mode.c` |
| `main/main.c` | `lcd_init()` 成功后 `lvgl_app_start()` | 不要再调 `lcd_test_start()` 抢屏；SD 必须在 LVGL 之前挂上 |
| `main/CMakeLists.txt` | `SRCS` 含 `lvgl_app.c`；`REQUIRES esp_lvgl_port` | 加 UI 源文件时改这里 |
| `components/BSP/LCD/lcd.c` | `lcd_draw_rgb565()` 官方 DMA blit；一线函数保留 | 写屏路径当前是 QIO `draw_bitmap` |
| `main/lcd_test.c` | 旧准星循环 | **保留但不调用** |

`app_main()` 顺序：USB shell → AW9523 → 三键 → **`sdmmc_fat_start()`（`/sdcard`）** → `lcd_init()` → `lvgl_app_start()`。LCD / LVGL 失败则空转打日志，shell 仍在，避免看门狗把 USB 打掉。SD 失败只警告，继续出屏。

---

## 3. 实际刷屏方式（2026-08-25 改为官方 DMA；同日改为 FULL 一帧）

**结论：LVGL flush 走 `esp_lcd_st77916` 的 `draw_bitmap` → `tx_color` DMA。**  
命令阶段仍 polling；像素阶段 `spi_device_queue_trans`，完成时 `on_color_trans_done` 释放 `s_color_done`。flush 里等到 DMA 完成再 `lv_display_flush_ready()`（单全屏缓冲，不是双缓冲异步重叠绘制）。

Music demo 大面积动画时，40 行 PARTIAL 会把一帧拆成多条「画 → DMA」，屏扫 GRAM 时出现横向撕裂。现改为 **FULL：先画完整 360×360，再一次 `draw_bitmap`**。TEP 未接 GPIO，不能场同步；撕裂会轻很多，仍可能剩一条淡扫线。

```
LVGL 任务（lv_timer_handler）
  → 软件绘制整帧到全屏缓冲（RGB565_SWAPPED，已是总线大端）
  → flush_cb(整屏 area, px_map)
  → lcd_draw_rgb565()   // 禁止原地 bswap；PSRAM → 内部 bounce 分条
  → memcpy bounce（最多 40 行）+ draw_bitmap + 等 DMA，直到整帧刷完
  → esp_lcd_panel_draw_bitmap(..., bounce)  // end 开区间
  → CASET/RASET + tx_color(opcode 0x32, RAMWR 0x2C, 像素)
  → IDF panel_io_spi_tx_color：spi_device_queue_trans（QIO，源在内部 DMA）
  → on_color_trans_done → s_color_done
  → lcd_wait_color_dma()
  → lv_display_flush_ready()
```

启动时 `lcd_prepare_1wire()` **只做一次**（COLMOD `0x55` + invert on + 全屏窗口）。flush 里不要反复 init。一线函数 `lcd_draw_rgb565_1wire_area()` **仍保留**，花屏时把 flush 改回去即可。色序若反了，不要改 init：退回 `RGB565` 并 bounce 拷贝再 swap。

### 3.1 和一线 polling 的差别

| 路径 | 当前 LVGL flush | 一线退路 `lcd_draw_rgb565_1wire_area` |
|------|-----------------|--------------------------------------|
| API | `esp_lcd_panel_draw_bitmap` → `tx_color` | `esp_lcd_panel_io_tx_param` |
| IDF 实现 | `spi_device_queue_trans` | `spi_device_polling_transmit` |
| 完成方式 | `on_color_trans_done` + `s_color_done` | 函数返回即传完 |
| QSPI 像素线 | 四线 QIO（opcode `0x32`） | 一线（opcode `0x02`） |
| 这块屏 | **待验收**（init 已对，QIO 点亮后未复测） | 点亮 + Hello LVGL 已确认 |

`lcd.c` 里注册的 `lcd_on_color_trans_done` / `s_color_done` 现给 `lcd_fill_rect()` 和 `lcd_draw_rgb565()` 共用。

全屏缓冲 64 字节对齐：优先内部 `MALLOC_CAP_DMA`，不够则 PSRAM（当前会落到 PSRAM）。**不要**把 PSRAM 指针直接交给 `draw_bitmap`：SPI master 会再申请同等大小的内部 priv TX，整帧 259KB 会 `ESP_ERR_NO_MEM`（`setup_dma_priv_buffer`）。`lcd_draw_rgb565()` 先 memcpy 到启动时申请的内部 DMA bounce（40 行），再 `draw_bitmap`。FULL 仍是先画完整帧再连续刷条带，中间没有 LVGL 绘制。缓冲必须等 DMA 完成才能还给 LVGL。

### 3.2 缓冲

显示配置（`lvgl_app.c` / `lcd.c`）：

| 项 | 值 |
|----|----|
| 分辨率 | 360×360 |
| 色深 | `LV_COLOR_FORMAT_RGB565_SWAPPED` |
| 渲染 | `LV_DISPLAY_RENDER_MODE_FULL` |
| LVGL 缓冲 | 360 × 360 × 2 = 259200 字节，64 对齐；内部 DMA 失败则 PSRAM；单缓冲 |
| DMA bounce | 40 行 × 360 × 2 = 28800 字节，内部 `MALLOC_CAP_DMA`，`lcd_init` 时申请 |
| 传输上限 | `LCD_MAX_TRANSFER_SZ` = 40 行（与 bounce 相同） |

传入 `lcd_draw_rgb565()` 的像素必须已经是大端 RGB565；函数内不再 swap。区域越界直接返回错误（源缓冲以传入的 `w` 为行跨距，裁剪会错位）。

双缓冲、在回调里才 `flush_ready`、接 TEP 场同步要另问。

---

## 4. 当前画面与验收

`lvgl_app_start()` 在建好 display 后调用 `ui_usb_mode_start()`：**USB 模式四页**，不是 Music demo。详见 `docs/USB模式设置页-给接手AI.md`。

`sdkconfig`：Music demo / `LV_BUILD_DEMOS` **已关**。`SYSMON` / `PERF_MONITOR` / `MEM_MONITOR` **已开**（叠在设置页上）。Montserrat 12/14/16 开着（sysmon 用 12）。设置页中文用自带 `CONFIG_LV_FONT_SOURCE_HAN_SANS_SC_16_CJK`，不要自制点阵。**屏上文案只用英文或简体中文，不要繁体凑字**，见 USB 设置页文档第 1.1 节和第 4 节。`LV_MEM_SIZE` 96 KB。

触摸未用。三键已绑到设置页（队列，不是 LVGL indev）。圆屏四角没有 mask（物理圆屏挡住方缓冲四角即可）。

用户 2026-08-25 确认（**一线路径**）：**编译 OK，屏上黑底白字 Hello LVGL。** USB 下载口仍可用。Music Round 已能播，用户反馈 **PARTIAL 40 行横向撕裂**；改为 FULL 后首次烧录 **PSRAM 整帧 DMA 失败**（`setup_dma_priv_buffer` / `ESP_ERR_NO_MEM`）。已改为内部 40 行 bounce。**bounce 路径尚未经用户看屏确认。**

**QIO DMA 路径用户已在 Music demo 上看过画面**（能播、有撕裂）。设置页本身 **尚未经用户看屏确认**。若花屏（隔行点），flush 改回 `lcd_draw_rgb565_1wire_area()`。若色序反了，退回 `RGB565` + bounce swap，不要改 init。

### 4.1 屏上调试（sysmon）

用户 2026-08-25 要求设置页也叠回 Music demo 那套调试字。`sdkconfig` 已开官方 `LV_USE_SYSMON`；ESP 堆由 `lvgl_create_debug_overlay()` 自刷。

| 位置 | 内容 |
|------|------|
| 顶中（往下 8px） | FPS、CPU%、render/flush 耗时（`LV_USE_PERF_MONITOR`） |
| 底中（往上 8px） | LVGL 内置堆已用 / 峰值 / 碎片（`LV_USE_MEM_MONITOR`） |
| 左中（往右 4px） | ESP 堆：internal / PSRAM / DMA 剩余 KB（`lvgl_app.c` 自刷） |

LVGL 9.5 的 `lv_conf_kconfig.h` 把 `TOP_MID` 宏名字写错，Kconfig 的顶中不会生效，perf 会落到默认右下被圆切掉。位置在 `lvgl_place_debug_labels()` 里改，不要只靠 sdkconfig。字号 Montserrat 12。底中会和设置页底部提示叠一点，这是调试层。

CPU% 是 LVGL 任务占用，不是双核整机。不要开 `LV_USE_REFR_DEBUG`（会给脏区涂色）。USB 未动。关监视：把上述 Kconfig 关掉，并去掉 `lvgl_create_debug_overlay()`。

---

## 4.2 SD / FAT → LVGL 文件系统

开机先 `sdmmc_fat_start()` 把卡挂到 VFS `/sdcard`，再 `lvgl_port_init()`。`lv_init()` 会自动 `lv_fs_stdio_init()`。

| 项 | 值 |
|----|----|
| Kconfig | `CONFIG_LV_USE_FS_STDIO=y` |
| 盘符 | `'S'`（83） |
| 前缀 | `/sdcard/` |
| 读缓存 | 512 字节 |
| 默认盘符 | `'S'`（路径可省略 `S:`） |

LVGL 路径：`S:hello.bin` → `fopen("/sdcard/hello.bin")`。  
**不要**开 `LV_USE_FS_FATFS`。PNG/JPG 解码还没开；要显示照片再另开 `LV_USE_LODEPNG` / `LV_USE_TJPGD`。

`lvgl_sd_fs_probe()` 在建 display 之后用 `lv_fs_dir_open("S:")` 列根目录打日志。左侧调试字第四行：`sd S:` / `sd --` / `sd fail`。

---

## 5. 改 UI 时怎么写

```c
if (!lvgl_port_lock(0)) {
    /* 超时或失败，不要继续调 lv_* */
}
/* 创建/修改对象 */
lvgl_port_unlock();
```

`flush_cb` 由 LVGL 任务调用，里面不要再 lock。  
按键回调在 KEY 任务里，**不要**在回调里直接改 LVGL 对象，先投递再在 LVGL 任务里处理。详见 `docs/按键绑定-给接手AI.md`。

---

## 6. 不要做的事

- 不要 `lvgl_port_add_disp()`（自己 flush 已走 `draw_bitmap`）。
- 不要加 `espressif/esp_lvgl_port` 的 USB HID、不要 TinyUSB。
- 不要关 USB Serial/JTAG，不要把控制台改成仅 UART0。
- 不要把 GPIO19/20 当普通 GPIO。
- 不要把 LVGL 推进 BSP；LCD 只负责像素。
- 不要同时跑 `lcd_test_start()` 和 LVGL。
- 不要在未问用户时做圆屏 mask、吧唧相册、触摸、TinyUSB。
- 三键已绑 USB 设置页；改菜单另问，见 `docs/USB模式设置页-给接手AI.md`
- 不要为 LVGL 去改 strapping（GPIO0 / 3 / 45 / 46）或烧 eFuse。
- 花屏时不要换 init / 关 invert；先退回一线 blit。
- 不要对 FULL 全屏缓冲原地 bswap（下一帧会花）。色序不对就 bounce 拷贝。
- 不要把 PSRAM 帧直接 `draw_bitmap`（SPI 会再申请同等内部 priv TX，整帧会 `ESP_ERR_NO_MEM`）。

---

## 7. 还没做（要另问）

- 用户看屏确认 USB 设置页
- 真正的 U 盘 / CDC（TinyUSB，会抢 PHY，必须另批）
- 圆屏 clip / `rounder_cb`
- 真实吧唧 UI（图、分页）
- 双缓冲 + 回调里 `flush_ready`（绘制与 DMA 重叠）
- 接 TEP 做场同步（FPC pin6，板上未接 GPIO）
