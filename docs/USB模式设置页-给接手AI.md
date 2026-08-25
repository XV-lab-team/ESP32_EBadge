# USB 模式设置页 — 给接手 AI

日期：2026-08-25  
工程概况：`工程基本情况.md`  
LVGL 写屏：`docs/LVGL接入记录-给接手AI.md`  
三键：`docs/按键绑定-给接手AI.md`

> **配置模式已接 USB MSC（U 盘）。** 进配置会卸掉 `/sdcard` 和 LVGL 的 `S:`，把 SD 交给电脑；退出后停 TinyUSB，PHY 还给 USB Serial/JTAG，再挂回 FAT。  
> **CDC 这次仍不做。**  
> **`CONFIG_USJ_ENABLE_USB_SERIAL_JTAG` 必须保持开，不要烧 USB 相关 eFuse。** 卡住时：长按确定退出，或拔插 USB，或上电按住向上键（GPIO18）强制正常模式。  
> **没有图片素材。** U 盘图标是 LVGL 矩形。  
> **屏上文案只用英文或简体中文。** 不要用繁体凑字。

---

## 0. 接手后立刻遵守

1. **先问再改**；**USB 下载绝不能丢**（GPIO19/20，`CONFIG_USJ_ENABLE_USB_SERIAL_JTAG=y`）。TinyUSB 只允许经 `usb_msc_enter` / `usb_msc_exit` 成对调用。不要关 USJ，不要烧 eFuse。
2. 编译烧录用 **VS Code + 乐鑫 ESP-IDF 扩展**。IDF：`D:/Espressif/frameworks/esp-idf-v5.5.4/`。
3. 改界面只动 `main/ui_usb_mode.c`。中文用 LVGL 自带 CJK 字库。**屏上文案只用英文或简体中文**，缺字不要改繁体。不要再写 `gen_ui_font.py` / 雅黑点阵。不要把 UI 塞进 `components/BSP/LCD/`。
4. 所有 `lv_*`（除 flush）必须在 `lvgl_port_lock` / `unlock` 里。按键回调在 KEY 任务，**只投递队列**，不要在回调里改 LVGL。
5. 不要开 Music demo。sysmon 叠层已按用户要求挂回设置页（顶/底/左，圆内边距 8/8/4），改位置只动 `lvgl_app.c`。

---

## 1. 这次实际做了什么（按时间）

用户要两种 USB 模式的设置界面：

| 模式 | 产品语义 | 当前固件真实行为 |
|------|----------|------------------|
| 正常模式 | USB = 下载 + Debug | USB Serial/JTAG；SD 挂 `/sdcard`，LVGL `S:` 可用 |
| 配置模式 | USB = U 盘 | TinyUSB MSC；先 `led_script_stop` + 卸 FAT，再把卡交给电脑。CDC 不做 |

流程：

1. 先画交互稿（Cursor Canvas，不进固件）：圆屏 360×360、三键、四页、确认页默认「取消」。
2. 用户说「没问题，写代码吧」。
3. 换成设置页。用户立刻编译烧录仍看到 demo —— 当时代码还没写完。
4. 补上 LVGL 四页 + 三键 + 中文字库；关掉 Music demo / sysmon。
5. 用户问图标是不是图片：不是，见第 5 节。
6. 再编时 `os_monitor.c` `snprintf` 缓冲区太小，`-Werror=format-truncation` 失败；`used_raw` / `used_str` 从 24 改到 32。与 USB 无关。
7. 用户要求设置页也叠回 demo 那套调试字；sysmon 已挂回（顶 FPS/CPU、底 LVGL 堆、左 ESP 堆）。
8. 用户反馈中文乱码。改用 LVGL 自带 `SOURCE_HAN_SANS_SC_16_CJK`，删掉自制雅黑点阵。缺字改文案，不要再造字库。
9. 用户 2026-08-25 明确要求：**界面只用英文或简体中文**，不要用繁体（「確定」「長押」等）凑字库。

---

## 1.1 界面文案（用户硬性要求）

**屏上所有用户可见文字：英文，或简体中文。二选一或混用都可以。不要繁体。**

| 允许 | 不允许 |
|------|--------|
| English（`USB`、`Debug`、`U Disk`、`CDC`） | 繁体凑字：確定、選擇、長按写成長押、進入 |
| 简体：模式、取消、进入、正常 | 日文假名、其它语言 |

字库缺某个简体时：

1. 整句改成英文，或
2. 换成这份 CJK 里**已有**的简体，

**不要**用对应繁体顶上。日志、注释、文档不受这条限制。sysmon 调试层本身是英文数字，保持即可。

---

## 2. 四页与按键（必须按这个表）

无触摸。列表只有上下两项，不用左右布局。浏览用 **单击**，不用 `PRESS`。单击在短按松开后立刻上报。

| 页 | 内容 | 向上 / 向下单击 | 确定单击 | 长按确定 |
|----|------|-----------------|----------|----------|
| USB 设置 | 正常模式 / 配置模式；「当前」标在已选模式上 | 两项之间切焦点 | 已是当前 → toast；否则进确认页 | toast：上级菜单尚未做 |
| 确认进入 | 说明会暂时没有下载口；CDC 这次不做。默认焦点在取消 | 取消 ↔ 进入 | 取消回设置（焦点停配置）；进入 → 配置就绪 | 同取消 |
| U 盘就绪 | 「U 盘已就绪」+ 两个矩形当磁盘图标 | 无 | toast：单击无作用，长按退出 | 确认退出 |
| 确认退出 | 默认取消 | 取消 ↔ 退出 | 取消回就绪页；退出 → 设置且当前=正常 | 同取消 |

上电若 NVS 记的是配置模式，直接进「U 盘就绪」页。上电时 **GPIO18（向上）为低** 则强制正常模式并写回 NVS。

---

## 3. 文件

| 路径 | 职责 |
|------|------|
| `main/ui_usb_mode.c` / `.h` | 四页状态机、NVS、按键队列、toast |
| `main/usb_msc.c` / `.h` | TinyUSB MSC + 退出后恢复 USB Serial/JTAG PHY |
| `main/lvgl_app.c` | port + FULL flush；调用 `ui_usb_mode_start()` + sysmon 叠层 |
| `main/main.c` | `nvs_flash_init()` 后才 LCD/LVGL |
| `main/CMakeLists.txt` | `SRCS` 含 `ui_usb_mode.c` `usb_msc.c`；`REQUIRES esp_tinyusb` |
| `sdkconfig` | `CONFIG_USJ_ENABLE_USB_SERIAL_JTAG=y` **保持开**。`CONFIG_TINYUSB_MSC_ENABLED=y`。CDC 关 |

交互稿（不进固件）：Cursor 工程 canvases 目录下的 `usb-mode-ui.canvas.tsx`。

NVS：命名空间 `usbui`，键 `mode`，`0` 正常 / `1` 配置。

按键：`key_set_callback` → FreeRTOS 队列 → LVGL `lv_timer` 20 ms 取出再改界面。

进入配置：`led_script_stop` → `sdmmc_fat_unmount` → TinyUSB MSC。电脑 COM 会消失，出现可移动磁盘。  
退出配置：`tinyusb_driver_uninstall` → `usb_new_phy(USB_PHY_CTRL_SERIAL_JTAG)` → 再挂 `/sdcard`。电脑需重新识别下载口。

卡住恢复：长按确定退出；或拔插 USB；或上电按住向上键（GPIO18）强制正常模式（不进 MSC）。

---

## 4. 中文字库（经验：不要重复造轮子）

**用 LVGL 组件自带的思源黑体 CJK，不要自己从雅黑 / GDI / lv_font_conv 再做一份点阵。**

| 项 | 值 |
|----|-----|
| 字库 | `lv_font_source_han_sans_sc_16_cjk`（`managed_components/lvgl__lvgl/src/font/`） |
| 打开方式 | `sdkconfig` 里 `CONFIG_LV_FONT_SOURCE_HAN_SANS_SC_16_CJK=y` |
| 界面用法 | `lv_obj_set_style_text_font(..., &lv_font_source_han_sans_sc_16_cjk, 0)` |
| 14px 那份 | **不要同时开** `SOURCE_HAN_SANS_SC_14_CJK`，字面一样、Flash 会再占一份 |

这不是完整 GB2312。源文件头里 `--symbols` 是大约一千个中日韩常用字 + ASCII `0x20-0x7F`，简体常缺「盘、确、选、载、志、稍、暂、按、默、击」等。缺字会上屏方块。**按第 1.1 节：改成英文，或换成已有简体；禁止用繁体凑**（不要「確定」「長押」）。例如「U 盘」写成 `U Disk`。不要再生成 `ui_font_zh_16.c`。

曾用 Windows GDI + 微软雅黑自制 1bpp 字库，屏上中文乱码：LVGL 9.5 的 1bpp 解包和自制行对齐不一致，而且是重复造轮子。那套 `gen_ui_font.py` / `ui_font_zh_16.c` **已删，不要加回来。**

LVGL 9.5.0 这份 CJK `.c` 仍带已删除的 `lv_font_fmt_txt_glyph_cache_t` / `.cache`。`main/CMakeLists.txt` 在配置时剥掉这两处。升级 `lvgl/lvgl` 后若又报 cache，让这段 CMake 跑一遍即可，不要改回自制字库。

sysmon 调试字仍用 Montserrat 12（ASCII），不要把默认字体改成 CJK。

---

## 5. 「图片」是怎么做的

**没有图文件，没有手绘再贴进去的位图。**

配置就绪页的 U 盘样子：外框 `lv_obj` 圆角描边 + 内部一块灰色矩形，代码在 `ui_build_config_live()`。每次进入该页现场创建。

中文是点阵字形，不是每页一张图。

以后吧唧图案 / 相册再走 SPIFFS、FAT 或 LVGL 图转换；不要把设置页改成读 PNG。

---

## 6. 不要做的事

- 不要初始化 TinyUSB / `esp_tinyusb` / MSC / CDC 来「完成」配置模式，除非用户另批，并写清：如何退出、上电按住向上键强制 JTAG、烧完仍能用同一 USB 口下载。
- 不要关 USB Serial/JTAG，不要把控制台改成仅 UART0。
- 不要占用 GPIO19/20。
- 不要把按键回调里的 `lv_*` 直接改对象。
- 不要恢复 `lv_demo_music()` 当主界面。
- 不要自制中文点阵（GDI / 雅黑 / `gen_ui_font.py`）。缺简体改英文或换已有简体，**不要用繁体凑字**。
- 不要在屏上写繁体 / 日文假名。界面只用英文或简体中文。
- 不要给 GPIO46 加板上拉到 3.3 V。
- 花屏时不要换 LCD init；flush 退回一线，见 LVGL 接入记录。

---

## 7. 还没做（要另问）

- 用户看屏确认设置页（含中文、三键、圆屏裁切）
- 真正的 U 盘（TinyUSB MSC + SD）和 CDC
- 上级菜单（长按确定目前只 toast）
- 触摸
- 圆屏 `rounder_cb` / mask
