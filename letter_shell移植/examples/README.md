# examples — 本教具参考（不要整文件移植）

这两个文件是 **GB2312** 原样拷贝，来自 `USER/letter_shell/shell_app.c` / `shell_app.h`。

用途：对照「DMA IDLE 收包 → ring → 轮询 `shellTask`」以及 `SHELL_EXPORT_CMD_EX` 的写法。

**不要**把 `本工程_shell_app.c` 拷进目标工程后直接编译。它依赖：

- `bsp.h` / `USART_bsp` / `UART_NUM_DEBUG` / `huart_Debug`
- `uav.h`（`uav_motor_set_scale` 等）
- `logic.h` / `os_stats_*` / `HAL_NVIC_SystemReset`

目标工程应从 `glue/shell_app.c` 改 UART 钩子，业务命令在目标自己的 `.c` 里新增。

教具任务创建（摘录，文件在仓库 `USER/Logic/logic.c`）：

```c
#define SHELL_TASK_STACK  512u
xTaskCreate(shell_app_task, "shell_app_task", SHELL_TASK_STACK,
            NULL, osPriorityNormal, NULL);
```
