# letter-shell 移植包（给接手 AI）

本目录是一份**自包含移植包**：源码 + 说明。  
目标是把本教具工程里已经跑通的 **letter-shell 2.0.8** 接到**另一个 STM32 + FreeRTOS** 工程。

**只读本文件夹即可移植。** 不要去改教具仓库里的 `USER/letter_shell/`（那是产品库，禁改）。

---

## 0. 你现在该做什么

1. **先读** `00_给接手AI.md`（编码、边界、禁令）  
2. 再读 `01_架构与模块说明.md`  
3. 再读 `02_UART与任务接入.md`  
4. 按 `03_移植步骤.md` **一层一做**  
5. 链接脚本细节以 `05_链接脚本与命令导出.md` 为准  
6. 用 `06_验收清单.md` 自测后再说「移植完成」

文件清单见 `04_文件清单.md`。

---

## 1. 一句话架构

```
串口收到字节 → ring 环形缓冲 → shell.read
PC 终端 ← 轮询写 TDR ← shell.write
FreeRTOS 任务里周期调用 shellTask()
命令用 SHELL_EXPORT_CMD_EX() 链到独立段 shellCommand
```

本教具的跑通配置（必须保持，除非你明确知道后果）：

| 宏 | 本工程值 | 含义 |
|----|----------|------|
| `SHELL_USING_TASK` | `1` | 使用 `shellTask()` |
| `SHELL_TASK_WHILE` | `0` | **非阻塞轮询**（read 失败就返回） |
| `SHELL_USING_CMD_EXPORT` | `1` | 用段导出命令，不要命令表 |
| `SHELL_AUTO_PRASE` | `0` | 命令原型是 `int cmd(int argc, char *argv[])` |
| `SHELL_USING_VAR` | `0` | 不用变量功能 |
| `SHELL_USING_AUTH` | `0` | 不用密码 |

---

## 2. 拷什么、不拷什么

| 拷 | 不拷 |
|----|------|
| `src/` 全部（核心库） | `examples/本工程_shell_app.c` 里的 UAV 业务命令 |
| `glue/shell_app.c` 当板级草稿 | 教具 `USER/BSP/`、`Core/`、`mapping.h` |
| `glue/*shell*.inc` 链接片段 | `shell_logic.h`（本工程有头无 .c，是死代码） |

`examples/` 只是「本教具怎么接 UART4 DMA+IDLE」的参考，**不要整文件覆盖目标工程**。

---

## 3. 编码（读源码前必看）

| 文件 | 编码 |
|------|------|
| 本目录全部 `.md`、`glue/*.c`、`glue/*.h`、`glue/*.inc` | **UTF-8** |
| `src/*.c` `src/*.h`、`examples/本工程_*.c` | **GB2312 / GBK**（从教具 `USER/letter_shell` 原样拷贝） |

`src/` 里中文注释若显示成 `鏄�鍚︿娇鐢�`，是工具按 UTF-8 误读，**不是文件坏了**。逻辑以本目录 md 为准。目标工程若源码是 UTF-8，拷贝后可转码，但**不要改 `shell.c` 行为**。

---

## 4. 上游

- 库作者：Letter（NevermindZZT）  
- 本包版本字符串：`SHELL_VERSION` = `"2.0.8"`（见 `src/shell.h`）  
- 本包来源工程：无人机教具 STM32G474 + HAL + FreeRTOS + Keil ARMCC
