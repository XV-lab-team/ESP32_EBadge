# SD 卡接入记录 — 给接手 AI

日期：2026-08-25  
工程概况：`工程基本情况.md`  
移植包（只读参考）：`sdcard_port_kit/`  
工程内代码：`components/BSP/SDMMC/`、`components/BSP/SDMMC_FAT/`

> **已接入本地 SDMMC 4-bit + FAT，挂载点 `/sdcard`。**  
> **U 盘走配置模式 `main/usb_msc.c`，必须成对退出并恢复 Serial/JTAG。** 不要关 `CONFIG_USJ_ENABLE_USB_SERIAL_JTAG`。  
> **不要把移植包默认脚（8/15/16/17/18/6/7）烧到这块板**，会撞按键和屏。  
> **不要把 `sdcard_port_kit/source/BSP/SDMMC_FAT/sdmmc_fat.c` 原样编进来**（里面有第二个 `app_main`）。

---

## 0. 接手后立刻遵守

1. **先问再改**；**USB 下载绝不能丢**（GPIO19/20，`CONFIG_USJ_ENABLE_USB_SERIAL_JTAG=y`）。
2. 编译烧录用 **VS Code + 乐鑫 ESP-IDF 扩展**。IDF：`D:/Espressif/frameworks/esp-idf-v5.5.4/`。
3. SD 底层只走官方 `sdmmc` + `fatfs`。U 盘才用 TinyUSB MSC（先卸 FAT）。不要 Arduino SD 库。
4. 改脚只动 `components/BSP/SDMMC/sdmmc.h` 的 `SDMMC_PIN_*`。GPIO1 是 **SD_CD**，不要再当 LED。
5. 无卡或非 FAT 时 `sdmmc_fat_start()` 只打警告，**不要空转**，屏和 USB 仍要起来。

---

## 1. 相对移植包必须改的差异

| 项 | `sdcard_port_kit` 原工程 | 本板 ESP32-S3 |
|----|--------------------------|---------------|
| 引脚 | CD=8 CLK=16 CMD=15 D0=17 D1=18 D2=6 D3=7 | 见下表 |
| 总线 | 默认 1-bit | **4-bit**（D0–D3 都接到了） |
| 时钟 | 40 MHz | 先 **20 MHz**（不稳再降 10；稳了可升 40） |
| USB MSC | TinyUSB 暴露 U 盘 | **不移植** |
| FAT | 草稿带 `app_main`，不可用 | 按 IDF：已 init 的 `sdmmc_card_t` 上 `ff_diskio_register_sdmmc` + `f_mount` |
| 挂载点 | MSC 用 `/data` | **`/sdcard`** |
| 失败策略 | 原 `main` 未真正调用 | 失败继续启动 |

用户 2026-08-25 给出的脚：

| 信号 | GPIO |
|------|------|
| SD_CD | 1 |
| SD_D2 | 2 |
| SD_D3 | 42 |
| SD_CMD | 41 |
| SD_CLK | 40 |
| SD_D0 | 39 |
| SD_D1 | 38 |

和屏（4/5/6/7/15/16/17）、键（8/18/46）、AW9523（3/14/21）、USB（19/20）、模组内部 PSRAM（35/36/37）都不冲突。

---

## 2. 目录与 API

| 路径 | 职责 |
|------|------|
| `sdcard_port_kit/` | 原工程抽出的参考，只读。MSC 相关文件不要拷进产品 |
| `components/BSP/SDMMC/sdmmc.c` | 官方 SDMMC host + `sdmmc_card_init`，返回 `sdmmc_card_t *` |
| `components/BSP/SDMMC_FAT/sdmmc_fat.c` | 把已 init 的卡挂到 VFS FAT |
| `main/main.c` | `key_init()` 之后 `sdmmc_fat_start()`；shell 命令 `sd` |

`sdmmc_fat_start()`：内部 `sdmmc_init(NULL)` + 挂 `/sdcard`。  
不要再对同一张卡调用 `esp_vfs_fat_sdmmc_mount()`，那会把 host 初始化第二次。

LVGL 在 `lvgl_port_init()` 里会注册官方 **stdio** 驱动：盘符 **`S:`** 前缀拼到 `/sdcard/`。`lv_init()` 自动 `lv_fs_stdio_init()`。  
例：`lv_image_set_src(img, "S:photo.bin")` → `fopen("/sdcard/photo.bin")`。  
**不要**再开 `LV_USE_FS_FATFS`（会绕过 VFS 再挂一次）。PNG/JPG 解码器还没开，先用 LVGL `.bin` 图或自己读文件。

**不要自动格式化**（`format_if_mount_failed` 未开）。卡不是 FAT 时挂载失败，用户数据不会被擦。

**不要自动格式化**（`format_if_mount_failed` 未开）。卡不是 FAT 时挂载失败，用户数据不会被擦。

当前 `sdkconfig`：`CONFIG_FATFS_LFN_NONE=y`，**只有 8.3 短文件名**。Windows 长文件名设备端可能看不见。要长名时再改 `CONFIG_FATFS_LFN_HEAP`（先问）。LED 样式在 **`LED/LED.CFG`**，打包见 `sdcard_kit/`。

CLK/CMD/DAT 应有约 10 kΩ 外部上拉；内部上拉只是辅助。CD 按 IDF 默认 **低电平 = 有卡**。若插着卡仍 `Card init failed`，先查上拉和 CD 极性。

---

## 3. 怎么验收

VS Code ESP-IDF：**Build → Flash → Monitor**。终端 UTF-8。

插卡复位后日志应有 `sdmmc:` 的 Name/Type/Size，以及 `FAT mounted at /sdcard`，随后 `LVGL FS S: -> /sdcard`。shell：

```text
sd              # 容量 + /sdcard 根目录
```

屏上左侧调试字最后一行：`sd S:` 成功，`sd --` 无卡，`sd fail` 卡挂上了但 LVGL 打不开，`sd usb` 正在当 U 盘（FAT 已卸）。

无卡时允许 `sdmmc_fat_start 失败`，随后 LCD/LVGL 仍应起来，USB 仍可再下载。

---

## 4. 明确没做的

- CDC
- PNG/JPG 解码
- 插拔热插拔任务
- 插拔热插拔任务
- 把 SD 里的图交给 LVGL
- 改 FAT 长文件名 / 代码页 936
