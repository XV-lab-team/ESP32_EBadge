# OS 监控移植记录 — 给接手 AI

日期：2026-08-25  
工程概况：`工程基本情况.md`  
移植包（只读参考）：`os监控移植/`  
工程内组件：`components/os_monitor/`

> **已接入。** 教具 STM32+HAL 的 `os_monitor` 接到本板 **ESP-IDF FreeRTOS**。  
> 表打到 **USB Serial/JTAG**（和 letter-shell 同一 COM）。默认 **不**自动刷新。  
> **不要 TinyUSB / USB-OTG。** 不要占用 GPIO19/20。运行时统计走官方 `esp_timer`，不要再加硬件 TIM。

---

## 0. 接手后立刻遵守

1. **先问再改**；**USB 下载绝不能丢**（GPIO19/20，`CONFIG_USJ_ENABLE_USB_SERIAL_JTAG=y`）。
2. 编译烧录用 **VS Code + 乐鑫 ESP-IDF 扩展**。IDF：`D:/Espressif/frameworks/esp-idf-v5.5.4/`。
3. 不要把 `os监控移植/examples/` 整文件贴进来（依赖教具 `log.h`）。
4. 不要抄教具 `glue/os_runtime.c` 的 `HAL_TIM_*`。ESP32 没有那套 TIM6。
5. 不要关 `CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS` / `CONFIG_FREERTOS_USE_TRACE_FACILITY`。
6. 不要把运行时时钟改成 CPU CCOUNT（240 MHz 下约 17 s 溢出，CPU% 会乱）。保持 **ESP Timer**。
7. 监视输出用 `usb_serial_jtag_write_bytes`，不要指望 `printf`（主控制台仍是没接到电脑的 UART0）。

---

## 1. 相对 STM32 教具必须改的差异

| 项 | 教具 STM32 | 本板 ESP32-S3 / ESP-IDF |
|----|------------|-------------------------|
| 运行时计数 | TIM6 ~10 kHz IRQ，`FreeRTOSRunTimeTicks++` | `esp_timer_get_time()`（µs）。`CONFIG_FREERTOS_RUN_TIME_STATS_USING_ESP_TIMER=y` |
| 钩子 | 自己实现 `configureTimerForRunTimeStats` | IDF `portGET_RUN_TIME_COUNTER_VALUE()` 已提供，**不要再定义** |
| Idle 名 | `"IDLE"` | 双核 **`IDLE0` / `IDLE1`**。只认 `"IDLE"` 会让概览假报 ~100% |
| CPU% 分母 | 墙钟 `total` | `total * configNUMBER_OF_CORES`（两核都在跑） |
| 堆 | `configTOTAL_HEAP_SIZE` + heap_4 | `heap_caps_* (MALLOC_CAP_INTERNAL)`，**不含 8MB PSRAM** |
| 总栈列 | 读 heap_4 块头 | `pxTaskGetStackStart` + `vTaskGetSnapshot`；失败再退回 IDLE/Tmr Svc 配置栈 |
| `xTaskCreate` 栈 | **字**（1024 字 = 4 KB） | **字节**。监视任务栈 **6144** |
| 打印口 | UART printf | USB Serial/JTAG（`os_monitor_printf`） |
| CMSIS | `osPriorityNormal` | `tskIDLE_PRIORITY + 2`，无 `cmsis_os.h` |
| 计数宽度 | 32 位，10 kHz ~5 天回绕 | 开了 **U64**，避免 esp_timer 32 位约 71 分钟溢出 |

表头、着色（35~80 黄、>80 红、IDLE 无色）、`os_stats_dump` / `os_stats_set_auto` / `os_monitor_start` 三个 API 与教具一致。

---

## 2. 目录与 sdkconfig

| 路径 | 职责 |
|------|------|
| `os监控移植/` | 教具移植包，只读 |
| `components/os_monitor/os_monitor.c` | 快照、着色、dump、监视任务 |
| `components/os_monitor/os_monitor_port.*` | ESP-IDF 堆/栈/Idle/USB 打印 |
| `components/os_monitor/os_monitor_cmd.c` | shell：`os_stats` / `os_stats_auto` |
| `main/main.c` | `shell_app_start()` 之后 `os_monitor_start()` |

`sdkconfig` 已开：

- `CONFIG_FREERTOS_USE_TRACE_FACILITY=y`
- `CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS=y`
- `CONFIG_FREERTOS_USE_STATS_FORMATTING_FUNCTIONS=y`（被 select）
- `CONFIG_FREERTOS_RUN_TIME_STATS_USING_ESP_TIMER=y`
- `CONFIG_FREERTOS_RUN_TIME_COUNTER_TYPE_U64=y`

**不要**开 `CONFIG_FREERTOS_RUN_TIME_STATS_USING_CPU_CLK`。

---

## 3. 怎么用

VS Code ESP-IDF：**Build → Flash → Monitor**。终端 UTF-8，关本地回显。

复位后 `help` 应有 `os_stats`、`os_stats_auto`。

```text
os_stats            # 追加打一整块（不依赖 VT100）
os_stats_auto 1     # 约 0.5s 原地刷新（要 ANSI 终端）
os_stats_auto 0     # 停
```

默认 auto 关。原地刷新会和 shell 提示符抢同一条 USB，先用 `os_stats` 看表是否正常。

应能看到 `IDLE0`/`IDLE1`、`Tmr Svc`、`main`、`shell`、`os_monitor` 以及 LVGL / 按键 / 虚拟 IO 等任务。CPU 不应长期全 0，也不应空闲时概览假 100%。内存条是 **内部 DRAM**，总量大约几百 KB，不是 8 MB PSRAM。

---

## 4. 明确不要做的

- 不要初始化 TinyUSB / 占用 GPIO19/20
- 不要为运行时统计再配一个 GPTimer 中断（官方 esp_timer 已够）
- 不要把 `OS_MONITOR_STACK` 改回 1024（那是字，在 ESP-IDF 里只有 1024 **字节**，dump 会炸栈）
- 不要在 ISR / `vTaskSuspendAll` 里调 `os_stats_dump()`
- 不要把内存概览改成含 PSRAM 的 `MALLOC_CAP_DEFAULT`（8 MB 会让进度条永远几乎空）

`os_stats_dump_internal()` 里 `used_raw` / `used_str` 必须 ≥ 32 字节。2026-08-25：24 字节会触发 `-Werror=format-truncation`（`%u.%02uKB(%u.%u%%)` 最坏 28 字节），已改 32。不要再缩回去。
