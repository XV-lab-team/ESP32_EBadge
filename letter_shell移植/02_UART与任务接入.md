# 02 — UART 与任务接入

目标工程可以用任何 UART 收发方式，只要最后满足：

1. **RX 字节进入 `ring`**  
2. **`shell.write` 能发出一个 `char`**  
3. **一个 FreeRTOS 任务里周期调用 `shellTask(&shell)`**

下面先写本教具的接法（参考），再写目标工程最小接法。

---

## 1. 本教具实际接法（参考，勿整文件抄 BSP）

| 项 | 值 |
|----|-----|
| 外设 | UART4（`huart4`，宏 `huart_Debug`） |
| 引脚 | PC10 TX / PC11 RX |
| 波特率 | **2000000** 8N1 |
| RX | DMA + IDLE（`USART_bsp.rx_start(UART_NUM_DEBUG)`） |
| TX（shell） | **不用 DMA**。轮询 `USART_ISR` 后写 `TDR` |
| 任务 | `shell_app_task`，栈 **512 字**，优先级 `osPriorityNormal` |
| 创建位置 | `USER/Logic/logic.c` 的 `logic_task`，在其它业务任务之后、`osDelay(500)` 之后 |

RX 状态机（教具 `bsp_usart`）：

- `rxstate == RX_OVER`：DMA IDLE 收完一包  
- 胶水把 `rxdata[0 .. rxsize)` 逐字节 `ring_push`  
- 再把 `rxstate` 置回 `RX_ING`

**TX 注意：** 同一 UART 若另有 DMA 发送（例如 printf 重定向），和 shell 轮询写 `TDR` 会打架。教具 shell 输出走轮询；移植时 debug 口建议 **shell 独占 TX**，或统一走同一发送路径。

教具 `myShellWrite` 等的是 `ISR & 0x40`（G4 上这是 **TC** 不是 TXE）。能用，但偏慢。目标板应改成该系列手册里的 **TXE / TXFNF**。

---

## 2. 目标工程最小接法（推荐照这个写）

### 2.1 三个钩子

```c
signed char myShellRead(char *c);   /* 0=读到一字节并写入 *c；非0=现在没数据 */
void myShellWrite(const char c);    /* 发出一个字节，可轮询阻塞 */
```

然后：

```c
shell.read  = myShellRead;
shell.write = myShellWrite;
shellInit(&shell);
```

`shellInit` 之前必须已经赋好 `read`/`write`。`write` 为空则横幅发不出去；`read` 为空则 `shellTask` 会打印错误并 `while(1)`。

### 2.2 RX 三种常见实现（选一种）

**A. 中断每字节入 ring（最简单）**

UART RXNE 中断里 `ring_push`。任务里只调 `shellTask`。注意 ring 是非线程安全的简易实现：若任务也 `ring_push`，不要和 ISR 同时 push。教具是任务里 push、任务里 poll，无 ISR 碰 ring。

**B. DMA + IDLE（与教具同类）**

IDLE 回调或任务看到「一帧完成」后，把缓冲区拷进 ring。适合突发粘包。

**C. 任务里阻塞 `HAL_UART_Receive` 1 字节**

仅当把 `SHELL_TASK_WHILE` 改为 `1` 且 `read` **阻塞直到有字节** 时才合理。本包默认 **不要改** `SHELL_TASK_WHILE`。

### 2.3 TX

最小实现：等 TXE，写 `TDR`。也可以 `HAL_UART_Transmit(&huart, (uint8_t *)&c, 1, timeout)`，在 shell 任务里调用即可，不要在 ISR 里调 HAL 发送。

### 2.4 任务骨架

与 `glue/shell_app.c` 相同：

```c
void shell_app_task(void *arg)
{
    /* start RX */
    ring_init(&uart_rx_ring, ring_buf, sizeof(ring_buf));
    shell.read = myShellRead;
    shell.write = myShellWrite;
    shellInit(&shell);
    for (;;) {
        /* if RX ready: shell_feed_bytes(...) */
        shellTask(&shell);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
```

启动任务（教具在业务启动任务里创建，不在 `app_freertos.c` 的 128 字 `main_task` 里跑 shell 初始化）：

```c
xTaskCreate(shell_app_task, "shell", 512, NULL, osPriorityNormal, NULL);
```

栈单位是 **FreeRTOS 的 word**（Cortex-M 上 1 字 = 4 字节）。`help` / Tab 补全较吃栈，**小于 256 字容易踩 TCB**。

教具在 `shellInit` 前后用过 `vTaskSuspendAll` / `xTaskResumeAll`，为了初始化时不被打断。目标工程可选。

### 2.5 终端

- 8N1，波特率 = CubeMX 该 UART 的值  
- 发 **CR 或 LF** 都能回车执行（库对 `0x0D`/`0x0A` 都当 Enter）  
- 需要 ANSI 清屏（`cls`）时用支持 VT100 的终端  
- 本地回显：shell 自己回显输入，终端应 **关闭 local echo**，否则每个字符双份

---

## 3. 不要接到错误的串口

教具有三路串口：Debug=shell、HMI=串口屏、IBUS=接收机。  
目标工程同样：**shell 必须独占一条给人用的 Debug 口**，不要接到屏或遥控协议口上。
