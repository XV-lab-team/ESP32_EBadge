# examples — 本教具参考（不要整文件移植）

这些文件是 **GB2312** 原样拷贝。

| 拷贝自 | 本目录文件 |
|--------|------------|
| `USER/Chip/aw9523.c` / `.h` | `本工程_aw9523.c` / `.h` |
| `USER/APP/exio.c` / `.h` | `本工程_exio.c` / `.h` |
| `USER/APP/io_virtual.c` / `.h` | `本工程_io_virtual.c` / `.h` |

用途：对照「三片 AW9523 的 AD、完整 GPIO/LED 表、队列任务、诊断任务」的写法。

**不要**把 `本工程_io_virtual.c` 拷进目标工程后直接编译。它依赖：

- `bsp.h`（`GPIO_OUT_SET` / `RESET`）
- `log.h`（`XK_LOGE` 等）
- `hi2c2`、`EXIO_RST_GPIO_Port` / `EXIO_RST_Pin`
- 教具新 PCB 网名（照明/电调/舵机/绞盘等）

目标工程应从 `src/` + `glue/` 起步；脚表按目标原理图改。只有硬件与教具相同时，才把 `本工程_io_virtual.c` 里的 `aw9523_dev_cfg` 和两张 `*_pin_cfg[]` 当对照。

教具任务创建（摘录，文件在仓库 `USER/Logic/logic.c`）：

```c
res = xTaskCreate(io_virtual_task, "io_virtual", 2048, NULL,
                  osPriorityNormal, NULL);
```

`io_virtual_chip_test_task`：周期对三片写 OUTPUT=0，打 OK/FAIL。诊断用，logic 里默认不创建。
