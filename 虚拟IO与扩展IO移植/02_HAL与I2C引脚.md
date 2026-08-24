# 02 — HAL 与 I2C 引脚

目标工程可以用任意 I2C 外设和 RST 脚，只要最后满足：

1. **I2C 主机**已 init，速率建议 100~400 kHz，SCL/SDA **外部上拉**
2. **RST** 是 MCU 推挽输出，接到所有 AW9523 的 RST（或每片都能被复位）
3. 每片 AD1/AD0 硬件绑死，和 `aw9523_dev_cfg.chip[].a0/a1` **一致**
4. `io_virtual_task` 在 FreeRTOS 跑起来之后创建

下面先写本教具的接法（参考），再写目标工程最小接法。

---

## 1. 本教具实际接法（参考，勿整文件抄 BSP）

| 项 | 值 |
|----|----|
| 外设 | I2C2（`hi2c2`） |
| 引脚 | PA9 SCL / PA8 SDA |
| Timing | `0x20BE1E7F`（CubeMX Fast ~300 kHz） |
| RST | **PC7** `EXIO_RST`，推挽输出，Cube 默认复位后为低 |
| 芯片数量 | **3** |
| 地址 | EXIO1=`0xB0`（AD=00）；EXIO2=`0xB2`（AD0=1, AD1=0）；EXIO4=`0xB6`（AD=11） |
| 未用地址 | `0xB4`（AD1=1, AD0=0）本板没有这片 |
| 任务 | `io_virtual_task`，栈 **2048 字**，`osPriorityNormal` |
| 创建位置 | `USER/Logic/logic.c` 的启动任务里，与 `hmi_task` / `uav_task` 并列 |

丝印是 EXIO1 / EXIO2 / **EXIO4**，代码下标必须是连续的 **0 / 1 / 2**，禁止 `chip=4`。

I2C MSP 必须开对应 GPIO 复用时钟。RST 必须在 `aw9523_init` 前已经 `HAL_GPIO_Init`。

---

## 2. 目标工程最小接法（推荐照这个写）

### 2.1 CubeMX（用户做，AI 不要手改 Core）

- 选一路 I2C：Master，7-bit，Fast 或 Standard
- 一脚 GPIO 输出：标签建议 `EXIO_RST`，推挽、无上下拉
- 生成代码后确认 `hi2cx` 和 `EXIO_RST_GPIO_Port` / `EXIO_RST_Pin` 存在

### 2.2 填 port 与胶水宏

`src/aw9523_port.h`：

- RST 宏默认已是 `HAL_GPIO_WritePin`
- 若目标有自己的 BSRR 宏，可覆盖 `AW9523_MCU_GPIO_OUT_*`
- 需要日志时把 `AW9523_LOG_E` / `EXIO_LOG_*` 接到目标 log

`glue/io_virtual.c` 顶部：

```c
#ifndef VIO_I2C
extern I2C_HandleTypeDef hi2c2;
#define VIO_I2C  hi2c2
#endif

#ifndef VIO_RST_PORT
#define VIO_RST_PORT  EXIO_RST_GPIO_Port
#endif
#ifndef VIO_RST_PIN
#define VIO_RST_PIN   EXIO_RST_Pin
#endif
```

目标句柄不叫 `hi2c2` 时，在包含 `io_virtual.c` 之前 `#define VIO_I2C hi2c1`，或直接改这两个宏。

### 2.3 任务骨架

与胶水相同：

```c
#define IO_VIRTUAL_TASK_STACK  2048u
xTaskCreate(io_virtual_task, "io_virtual", IO_VIRTUAL_TASK_STACK,
            NULL, osPriorityNormal, NULL);
```

栈不够再加。`exio_t` 是静态的，不要把大结构放在这个任务的栈上。

### 2.4 先确认芯片在总线（可选）

在正式任务起来之前，可用 Cube/HAL：

```c
uint8_t addr = 0xB0; /* 按 AD 改 */
uint8_t out[2] = {0, 0};
HAL_I2C_Mem_Write(&hi2cx, addr, 0x02, I2C_MEMADD_SIZE_8BIT, out, 2, 10);
```

`HAL_OK` = 该地址有 ACK。教具诊断任务见 `examples/本工程_io_virtual.c` 的 `io_virtual_chip_test_task`（写 OUTPUT=0；**不要和 io_virtual_task 长期并行**）。

### 2.5 上拉与布线

AW9523 I2C 需要 SCL/SDA 上拉（常见 2.2k~4.7k 到 VCC）。没上拉会超时。多片共总线，AD 必须互不相同。
