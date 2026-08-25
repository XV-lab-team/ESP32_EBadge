# letter-shell 移植记录 — 给接手 AI

日期：2026-08-24  
工程概况：`工程基本情况.md`  
移植包（只读参考）：`letter_shell移植/`  
工程内组件：`components/letter_shell/`

> **已接入。** letter-shell **2.0.8** 跑在 **USB Serial/JTAG** 上（电脑侧同一个 COM：下载 + `ESP_LOG` + shell）。  
> **编码：全程 UTF-8**（源码无 BOM；终端也选 UTF-8）。不要再当 GB2312/GBK 转码。  
> **不要 TinyUSB / USB-OTG。** 那会抢走片内 USB PHY，下载口会没。

---

## 0. 接手后立刻遵守

1. **先问再改**；**USB 下载绝不能丢**（GPIO19/20，`CONFIG_USJ_ENABLE_USB_SERIAL_JTAG=y`）。
2. 编译烧录用 **VS Code + 乐鑫 ESP-IDF 扩展**。IDF：`D:/Espressif/frameworks/esp-idf-v5.5.4/`。
3. shell 传输层只用官方 `driver/usb_serial_jtag.h`，不要改成 UART0（板上没接到电脑），不要改成 TinyUSB CDC。
4. 命令继续用 `SHELL_EXPORT_CMD_EX` + 段导出；`linker.lf` 的 `KEEP` / `_shell_command_start` 不要删。
5. `SHELL_AUTO_PRASE` 保持 `0`；`SHELL_TASK_WHILE` 保持 `0`。命令原型：`int cmd(int argc, char *argv[])`。

---

## 1. 为什么走 USB 而不是 UART

本板 **UART0 没接到电脑**。能看见的 debug 口只有 USB Serial/JTAG。

letter-shell 不绑定 UART，只要 `shell.read` / `shell.write` 收发一字节。胶水里接到：

- `usb_serial_jtag_driver_install()`
- `usb_serial_jtag_read_bytes()` / `write_bytes()`
- `usb_serial_jtag_vfs_use_driver()`（让 `ESP_LOG` 也走驱动缓冲，避免和 shell 抢 FIFO）

`sdkconfig` 未改主控制台：仍是 UART0 主口 + USB 次控制台。交互输入靠自己装的 USJ 驱动，不靠 stdin。

---

## 2. 目录与职责

| 路径 | 职责 | 改不改 |
|------|------|--------|
| `letter_shell移植/` | 教具移植包（说明 + 原 `src/`） | 不要当产品代码改 |
| `components/letter_shell/src/` | 核心库拷贝（`shell.*` `ring.*` `shell_ext.*` `shell_cfg.h`） | 行为默认不改 |
| `components/letter_shell/shell_app.c` | USB 胶水、任务、`hello` | 板级可改 |
| `components/letter_shell/linker.lf` | `shellCommand` 段 KEEP + `_shell_command_start/_end` | 必须保留 |
| `main/main.c` | `shell_app_start()`；自测命令 `id` | 可加业务命令 |

`app_main()` **先** `shell_app_start()`，再 LCD。LCD 失败空转时 shell 仍可用。

ESP-IDF 的 `xTaskCreate` 栈单位是 **字节**（不是 STM32 CMSIS 的 word）。当前 shell 任务栈 **4096**，优先级 5。

---

## 3. 编码：UTF-8（不要 GB2312）

移植包文档写 `src/` 是 GB2312。**本仓库这份文件实际是 UTF-8**。

| 项 | 约定 |
|----|------|
| `components/letter_shell/src/*` | UTF-8，无 BOM |
| `shell_app.c` / `main.c` 字符串 | UTF-8 |
| USB 发出的字节 | 原样 UTF-8，不做 GBK 转换 |
| VS Code / Monitor / 串口助手 | 选 **UTF-8** |

曾误把 UTF-8 源码按 GBK 再转一次，中文注释花掉；已从 `letter_shell移植/src/` 按 UTF-8 重写回去。  
**以后不要对这组源码做 GBK↔UTF-8 转换。**

限制（库按 **字节** 处理，不是 Unicode 字符）：

- 命令名必须是 C 标识符，用英文
- 命令描述可以写中文 UTF-8，`help` 在 UTF-8 终端下能看
- 输入中文时退格按字节删，可能一次删不完一个汉字
- `SHELL_COMMAND_MAX_LENGTH` 仍是 50 **字节**

---

## 4. 相对教具源码的必要补丁

只在 **组件拷贝** 里，不改 `letter_shell移植/src/`。

| 文件 | 补丁 | 原因 |
|------|------|------|
| `src/shell.h` | GCC `SECTION` 加上 `used` | `--gc-sections` 否则可能丢掉命令对象 |
| `src/shell.c` | `shellDisplayValue` 的下标 `i`：`char` → `int` | ESP-IDF `-Werror=char-subscripts` |
| `CMakeLists.txt` | `-Wno-cast-function-type` 等 | 库把 `void help(...)` 转成 `int (*)()` |

链接：`linker.lf` 把段 `shellCommand` 放进 `flash_rodata`，`KEEP()` + `ALIGN(4, pre)` + `SURROUND(shell_command)`，对应 `shell.c` 的 `_shell_command_start` / `_shell_command_end`。  
任意已编译 `.c` 都可 `SHELL_EXPORT_CMD_EX`（`archive: *`）。`help` 里若只有 `help`/`cls`、没有 `hello`/`id`，先查这段被 gc 掉。

---

## 5. 怎么用

VS Code ESP-IDF：**Build → Flash → Monitor**（现用那个 COM，曾是 COM11）。

- 终端 **关本地回显**，编码 **UTF-8**
- USB 虚拟口波特率无意义
- 复位后应有 letter-shell ASCII 横幅、`Version:     2.0.8`、提示符 **`XV>>`**
- `help` → `help` / `cls` / `hello` / `id`
- `hello` → `hello letter-shell`
- `id` → `board ok`
- `ledplay` / `ledstop` → 播 SD 上 `LED/LED.CFG`，见 `docs/LED样式文件-给接手AI.md`

胶水会丢掉 CRLF 里紧跟 CR 的 LF，避免命令执行两次。  
日志和 shell 挤在同一条 COM 上，提示符里夹 `ESP_LOG` 正常。

加命令：在会被编译的 `.c` 里写 `int foo(int argc, char *argv[])`，然后 `SHELL_EXPORT_CMD_EX(foo, foo, "短描述", 长帮助);`。描述用 UTF-8 中文即可。不要开 `SHELL_AUTO_PRASE`。

---

## 6. 明确不要做的

- 不要把 `examples/本工程_shell_app.c` 整文件贴进来（UAV 教具命令）
- 不要用 TinyUSB 再做一个 CDC 当 shell
- 不要把控制台改成仅 UART0
- 不要关 `CONFIG_USJ_ENABLE_USB_SERIAL_JTAG`
- 不要把 `SHELL_USING_CMD_EXPORT` 改成 0（除非链接段实在做不通）
