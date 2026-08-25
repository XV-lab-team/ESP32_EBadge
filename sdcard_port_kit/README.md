# SD 卡代码移植包（给其他 AI 用）

本目录是从工程 `ESP32_LCDLVGL` 抽出的 **全部 SD 卡相关业务代码 + 移植说明书**。

请先读本文件，再按 `docs/` 顺序执行。不要去原工程翻 `components/lvgl`、`managed_components` 或 LCD/触摸代码，那些与 SD 卡无关。

---

## 你的任务

把本目录 `source/` 中的代码移植到目标 ESP-IDF 工程，使目标板能：

1. 用 **SDMMC 4 线/1 线主机** 初始化 SD 卡（得到 `sdmmc_card_t *`）。
2. （可选）把 SD 卡通过 **USB MSC** 暴露给 PC，当成 U 盘。
3. （可选）把 SD 卡挂到 VFS FAT，用 POSIX `fopen`/`fread` 访问。

原工程当前 **USB MSC 调用在 `main.c` 里被注释掉了**，FAT 挂载模块 **未完成**。移植时按本包文档实现，不要假设原工程运行时一定已经在用 SD 卡。

---

## 先读这些文件（按顺序）

| 顺序 | 文件 | 内容 |
|------|------|------|
| 1 | 本 README | 总览、源码清单、禁止项 |
| 2 | `docs/01_ARCHITECTURE.md` | 分层、调用链、模块职责 |
| 3 | `docs/02_HARDWARE.md` | 芯片、引脚、总线宽度、频率 |
| 4 | `docs/03_DEPENDENCIES.md` | IDF 组件、menuconfig、CMake |
| 5 | `docs/04_API.md` | 对外 API 与用法 |
| 6 | `docs/05_INTEGRATION.md` | 逐步接入目标工程的清单 |
| 7 | `docs/06_KNOWN_ISSUES.md` | 必须修或必须规避的坑 |

代码在 `source/`，可直接复制的片段在 `snippets/`。

---

## 本包包含什么 / 不包含什么

### 包含（业务代码，需要移植）

```
source/
  BSP/SDMMC/                 底层：SDMMC host + 卡初始化/反初始化
  BSP/SDMMC_FAT/             FAT 挂载草稿（不完整，见已知问题）
  BSP/SDMMC_tusb_msc/        USB MSC：把已初始化的 SD 卡暴露给 PC
  APP/SDMMC_tusbmsc_fat/     应用封装：先 init 卡，再开 MSC
```

### 不包含（不要复制进目标工程）

- `managed_components/espressif__esp_tinyusb`：用 Component Manager 拉依赖，不要拷贝。
- `managed_components/espressif__tinyusb`：同上。
- ESP-IDF 自带的 `sdmmc` / `fatfs` / `esp_vfs_fat`：目标工程用 IDF 组件即可。
- LVGL、LCD、触摸、LED、I2C。

### 原工程位置对照

| 本包路径 | 原工程路径 |
|----------|------------|
| `source/BSP/SDMMC/` | `components/BSP/SDMMC/` |
| `source/BSP/SDMMC_FAT/` | `components/BSP/SDMMC_FAT/` |
| `source/BSP/SDMMC_tusb_msc/` | `components/BSP/SDMMC_tusb_msc/` |
| `source/APP/SDMMC_tusbmsc_fat/` | `components/APP/SDMMC_tusbmsc_fat/` |

原工程把前三个编进 `components/BSP`，第四个编进 `components/APP`。目标工程可以保持同样拆分，也可以合成一个 `sdcard` 组件，但 **不要改 API 语义**，除非文档要求修复已知问题。

---

## 原工程运行环境（事实，不要猜）

- 芯片：`esp32s3`
- ESP-IDF：`5.5.4`（`CONFIG_IDF_INIT_VERSION="5.5.3"`）
- TinyUSB 组件：`espressif/esp_tinyusb` **^2.1.1**
- 存储接口：SDMMC（不是 SDSPI）
- 默认总线宽度：**1 bit**（虽然 D1/D2/D3 引脚已定义）
- 默认时钟：**40 MHz**
- 当前 `app_main` 里 SD 初始化被注释：

```c
//SDMMC_tusbmsc_fat_init();
//SDMMC_tusbmsc_fat_SetTusbMsc();
```

---

## 给移植 AI 的硬性规则

1. **先改引脚和总线宽度**，使之匹配目标硬件，不要原样烧到另一块板。
2. **不要把 `sdmmc_fat.c` 原样编进目标工程**。该文件里有 `void app_main(void)`，会和真正的 `main.c` 链接冲突。见 `docs/06_KNOWN_ISSUES.md`。
3. Linux/macOS 下头文件大小写敏感：实现文件叫 `SDMMC.h`，但业务代码写的是 `#include "sdmmc.h"`。移植时统一成一种大小写。
4. USB MSC 的 `tusb_msc_sdmmc_start()` **会阻塞**（内部 `xSemaphoreTake(..., portMAX_DELAY)`）。不要在 LVGL 任务或 `app_main` 主循环里直接调用，除非你改成非阻塞。
5. 不要把 LCD / LVGL / 触摸代码一并移植。
6. 需要 USB MSC 时才加 `esp_tinyusb`；若目标工程只要本地读写 SD 卡，只移植 `SDMMC` + 自行完成 FAT 挂载即可。

---

## 建议的最小移植集

按目标需求选：

| 需求 | 必须移植的源码 | 必须加的 IDF 组件 |
|------|----------------|-------------------|
| 只初始化 SD 卡 | `BSP/SDMMC` | `driver`, `sdmmc` |
| 本地 FAT 读写 | `BSP/SDMMC` + 自己写 FAT 挂载（不要用未完成的 `sdmmc_fat.c`） | 上面 + `fatfs` |
| USB 当 U 盘 | `BSP/SDMMC` + `BSP/SDMMC_tusb_msc` + `APP/SDMMC_tusbmsc_fat` | 上面 + `esp_tinyusb`, `console` |

---

## 验收标准

移植完成后应满足：

- [ ] 能编译链接，无第二个 `app_main`。
- [ ] 串口能看到 `sdmmc_card_print_info` 打出的 CID/CSD（卡初始化成功）。
- [ ] 若启用 MSC：PC 能识别可移动磁盘，读写不丢数据。
- [ ] 若启用 FAT：`fopen("/sdcard/...")` 或文档约定的挂载点可读写。
- [ ] 引脚、总线宽度、频率已按目标板修改，而不是照抄 GPIO 15/16/17。
- [ ] 头文件 include 在大小写敏感文件系统上能通过。
