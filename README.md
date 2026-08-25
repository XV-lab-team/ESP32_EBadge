# ESP32_EBadge

自制 **电子吧唧（E-Badge）**：ESP32-S3 + 1.8 寸圆屏 + 五组 RGB + SD 卡。  
当前固件已能点亮圆屏（LVGL 9）、三键切 USB 模式、配置模式当 U 盘、SD 上播 LED 样式。没有无线，没有吧唧菜单。

详细板级与源码说明见 **[工程基本情况.md](工程基本情况.md)**。协作方式见 **[开发者协作指南(必读).md](开发者协作指南(必读).md)**。

---

## 硬件

| 项目 | 规格 |
|------|------|
| 模组 | ESP32-S3-WROOM-1-N16R8（16 MB Flash + 8 MB OPI PSRAM） |
| 屏 | T180BV-C20-02，360×360，ST77916，QSPI；固件 RGB565 一线写 |
| 扩展 IO | 1 片 AW9523（地址 `0x5B`），5 组共阳 RGB |
| 存储 | SDMMC 4-bit，FAT 挂 `/sdcard` |
| 按键 | 向上 GPIO18 / 确定 GPIO8 / 向下 GPIO46（上拉，按下为低） |
| 下载口 | **只有原生 USB**（GPIO19=DM / GPIO20=DP），没有接到电脑的 UART0，没有复位键 |

仓库里暂无原理图。引脚、strapping、屏规格对照以 `工程基本情况.md` 为准。

---

## 硬约束：USB 下载绝不能丢

烧进去之后必须仍能用同一 USB 口再次 `idf.py flash`。做不到就不要改、不要烧。

- 保持 `CONFIG_USJ_ENABLE_USB_SERIAL_JTAG=y`
- 不要把 GPIO19 / GPIO20 当普通 GPIO
- 不要烧关掉 USB 下载 / JTAG 的 eFuse，不要开 Secure Boot / Flash 加密
- TinyUSB 只允许配置模式 MSC：`usb_msc_enter()` / `usb_msc_exit()` 必须成对；退出后 PHY 还给 Serial/JTAG
- 卡住时：长按确定退出配置模式，或拔插 USB，或上电按住向上键强制正常模式

日志和 letter-shell 都走 USB Serial/JTAG 虚拟 COM 口。Monitor / 串口助手编码选 **UTF-8**，关掉本地回显。复位方式：拔插 USB。

---

## 当前固件能做什么

- **圆屏**：ST77916 QSPI + LVGL 9，USB 模式设置页（英文或简体中文）
- **两种 USB 模式**
  - 正常：USB = 下载 + Debug（Serial/JTAG）
  - 配置：USB = U 盘（TinyUSB MSC，把 SD 交给电脑）。CDC 未做
- **三键**：单击浏览，长按确定返回/退出；已绑到设置页
- **SD**：挂 `/sdcard`；无卡只警告，不挡住屏
- **RGB**：AW9523 虚拟 IO；样式文件在卡上 `LED/`，shell `ledplay` / `ledstop`（不上电自动播）
- **letter-shell 2.0.8** + **os_monitor**（默认不自动刷表）

未做：无线、触摸、吧唧菜单、CDC。

---

## 编译与烧录

用 **VS Code + Espressif ESP-IDF 扩展**（Build / Flash / Monitor），不要把 Cursor 当主构建环境。

| 项目 | 值 |
|------|------|
| IDF | v5.5.4（本机常见路径 `D:/Espressif/frameworks/esp-idf-v5.5.4/`） |
| 目标 | ESP32-S3 |
| 口 | USB Serial/JTAG 虚拟 COM（会随插口变化） |
| CMake 工程名 | `00_basic`（教程残留） |

烧录失败或空片时，需要 IO0 对地 + 拔插 USB 才能进下载。空 Flash 或非法镜像会看门狗复位，电脑侧 USB 周期性掉线，见 `USB周期性掉线排查记录.md`。

---

## 上板怎么用

1. 插 USB，电脑出现虚拟 COM：日志 + shell。
2. 屏上是 USB 设置页：向上 / 向下单击切焦点，确定单击进入；长按确定按当前页语义返回或退出。
3. 进配置模式后当 U 盘；退出后下载口回来，SD 再挂回 `/sdcard`。
4. 往卡根目录拷本仓库 `sdcard_kit/LED/`（不要拷 `sdcard_kit` 这个名字）。当前 FAT 只有 **8.3 短文件名**。
5. shell 里：

```text
sd
ledplay
ledplay CAL.CFG
ledstop
key
os_stats
```

`ledplay` 不带参数 = `/sdcard/LED/LED.CFG`。打包说明见 [sdcard_kit/README.md](sdcard_kit/README.md)。

上电按住向上键（GPIO18）会强制正常模式并写回 NVS。

---

## 仓库里改哪里

| 目录 | 作用 |
|------|------|
| `main/` | 启动、LVGL 应用、USB 设置页、MSC、LED 脚本 |
| `components/BSP/` | LCD、KEY、SDMMC、AW9523 虚拟 IO |
| `components/letter_shell/` | USB Serial/JTAG 上的 shell |
| `components/os_monitor/` | CPU / 堆 / 任务表 |
| `sdcard_kit/` | 拷进产品 SD 的内容 |
| `docs/` | 接手记录与规格摘要 |
| `letter_shell移植/` 等 | 教具移植包，只读参考，不要当产品代码改 |

组件优先用 ESP-IDF 自带和乐鑫托管（`espressif/`）。没有 Arduino。

---

## 文档

| 文件 | 内容 |
|------|------|
| [工程基本情况.md](工程基本情况.md) | 板级、引脚、软件栈、启动顺序、易踩点 |
| [开发者协作指南(必读).md](开发者协作指南(必读).md) | 分支 + PR，不要直接推 `main` |
| [docs/LCD点亮排查记录-给接手AI.md](docs/LCD点亮排查记录-给接手AI.md) | 屏点亮（HD18004C18 + INVON + 一线 16-bit） |
| [docs/LVGL接入记录-给接手AI.md](docs/LVGL接入记录-给接手AI.md) | LVGL 9 接法 |
| [docs/USB模式设置页-给接手AI.md](docs/USB模式设置页-给接手AI.md) | 四页 UI、配置模式 MSC |
| [docs/按键绑定-给接手AI.md](docs/按键绑定-给接手AI.md) | 三键脚与语义 |
| [docs/SD卡接入记录-给接手AI.md](docs/SD卡接入记录-给接手AI.md) | SDMMC 4-bit、`/sdcard` |
| [docs/LED样式文件-给接手AI.md](docs/LED样式文件-给接手AI.md) | `LED/*.CFG` 五条指令 |
| [docs/AW9523虚拟IO移植记录-给接手AI.md](docs/AW9523虚拟IO移植记录-给接手AI.md) | RGB 脚表；等 DIM 视觉上不是白，偏差待定 |
| [docs/letter-shell移植记录-给接手AI.md](docs/letter-shell移植记录-给接手AI.md) | shell 接 USB Serial/JTAG |
| [docs/OS监控移植记录-给接手AI.md](docs/OS监控移植记录-给接手AI.md) | `os_stats` |
| [docs/T180BV-C20-02屏规格摘要.md](docs/T180BV-C20-02屏规格摘要.md) | 圆屏规格摘录 |

改 USB / 下载 / 控制台 / 引脚复用 / eFuse 之前，先保证烧完仍可用同一 USB 口下载。说不清就停。
