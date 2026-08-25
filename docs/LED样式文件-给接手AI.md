# LED 样式文件 — 给接手 AI

日期：2026-08-25  
工程概况：`工程基本情况.md`  
AW9523 / RGB：`docs/AW9523虚拟IO移植记录-给接手AI.md`  
SD 卡：`docs/SD卡接入记录-给接手AI.md`  
拷进产品的文件夹：`sdcard_kit/`（把其中的 `LED/` 整份放到 SD 根目录）

> **已接入解析 + 播放任务。不上电自动播。** 用 USB Serial/JTAG 上的 letter-shell：`ledplay` / `ledstop`。  
> 文件在 SD 卡 **`LED/`** 文件夹里，默认 **`/sdcard/LED/LED.CFG`**（8.3 短名；当前 `CONFIG_FATFS_LFN_NONE`）。  
> 往卡里拷：把仓库 `sdcard_kit/LED` 拷到 U 盘/卡根目录。拷完退出配置再 `ledplay`。U 盘期间会 `led_script_stop`。  
> 不要改 AW9523 电流档。不要关 `CONFIG_USJ_ENABLE_USB_SERIAL_JTAG`。

---

## 0. 接手后立刻遵守

1. **先问再改**；**USB 下载绝不能丢**。
2. 编译烧录用 **VS Code + 乐鑫 ESP-IDF 扩展**。
3. 改格式/指令只动 `main/led_script.c`。写灯只走 `io_virtual_rgb_set`，不要自己调 `aw9523_*`。
4. 新指令加在 `parse_cmd` / `run_script`，**旧固件遇到未知命令会解析失败**（故意的，避免 silently 丢步）。未知 **段**（`[foo]`）会跳过，方便以后加元数据。
5. 播放在独立任务 `led_script`（栈 4096，优先级 3），不要放到 shell 任务里死等 `wait`。

---

## 1. 文件放哪

| 项 | 约定 |
|----|------|
| 默认路径 | `/sdcard/LED/LED.CFG` |
| 卡上位置 | 文件夹 **`LED/`**，文件 **`LED.CFG`** / **`CAL.CFG`**（都是 8.3）。不要 `led_style.json` |
| 仓库打包 | `sdcard_kit/LED/`（把这个文件夹整份拷到卡根，不要把 `sdcard_kit` 这个名字拷上去） |
| 编码 | UTF-8，键和指令英文；`#` 到行末是注释 |
| 无卡 / 无文件 | `ledplay` 报错，不崩、不格式化卡 |
| 谁改文件 | 配置模式 U 盘，或拔卡 |

只写文件名会到 `LED/` 下找：`ledplay CAL.CFG` → `/sdcard/LED/CAL.CFG`。  
带斜杠的相对路径拼到挂载点：`ledplay LED/LED.CFG` → `/sdcard/LED/LED.CFG`。

---

## 2. 五条指令（v1，用户 2026-08-25 定）

线性执行。`set` / `off` 立刻改灯；**真正停住给人看的是 `wait`**。

| 指令 | 含义 |
|------|------|
| `set <灯> <r> <g> <b>` | 立刻改颜色。灯 = `1`–`5` 或 `all`。RGB 0–255 |
| `wait <ms>` | 保持当前颜色。0–60000 毫秒 |
| `loop <n>` | 跳回脚本第一条。`0` = 无限；`n>=1` 表示整段共跑 n 遍 |
| `end` | 停播，灯保持最后一帧 |
| `off` | 五灯全灭（等于 `set all 0 0 0`） |

以后要加 `fade` 等：先问用户，再改解析器。不要先做复杂时间轴。

没有 `loop` / `end` 时，跑完最后一条就停（隐式 `end`）。`loop` / `end` 请放在脚本末尾；后面的行不会执行。  
`loop` 每次回跳会先 `wait` 20 ms，避免没有 `wait` 的脚本把 I2C 队列打满。

连续多条 `set` 中间不要插 `wait`，五灯会几乎同时变。

---

## 3. 文件结构

```text
ver=1
name=demo

[cal]
gain_r=100
gain_g=100
gain_b=100

[script]
off
wait 200
set 1 255 0 0
wait 300
off
loop 0
```

- `[cal]`：白平衡百分比 0–200，100 = 不改。写到芯片的 DIM ≈ `RGB × gain / 100`，再限到 0–255。**偏差待定，先全 100。** 不要把 `50 50 50` 当成白。
- `[script]`：只放上面五条指令。
- 可以没有段名：整文件当脚本（仍可在文件头写 `gain_r=`）。
- `ver>1`：打日志仍尝试解析。未知 `key=` 忽略。

硬限制：最多 **128** 条指令；行长 **95** 字节。

---

## 4. Shell

终端 UTF-8，关本地回显。先插卡、`sd` 能看到文件夹 `LED`。

```text
ledplay              # 播 /sdcard/LED/LED.CFG
ledplay LED.CFG      # 同上（光文件名会到 LED/ 下找）
ledplay CAL.CFG      # 等 RGB 常亮，用来调 gain
ledplay /sdcard/LED/LED.CFG
ledstop              # 停；灯保持当前，不强制灭
```

`ledplay` 在解析成功后才会停掉正在播的旧脚本再开新的。解析失败则旧脚本继续。

`vio_rgb` / `vio_led` 仍是立刻写 DIM，**不走** `[cal]`。调试单灯用它们；播样式用 `ledplay`。

---

## 5. 代码位置

| 文件 | 职责 |
|------|------|
| `main/led_script.c` / `.h` | 解析、播放任务、shell 命令 |
| `main/main.c` | `io_virtual_start()` 之后 `led_script_init()`；**不**自动 `ledplay` |
| `sdcard_kit/LED/` | 拷到 SD 根目录的文件夹（`LED.CFG`、`CAL.CFG`） |

不要把播放逻辑塞进 `components/BSP/EXIO/`。

---

## 6. 明确不要做的

- 不要上电自动播（用户要求先 shell）
- 不要为了更白去改 `ledmode_isel`（保持 3）
- 往卡里拷时拷 **`LED/` 整个文件夹**（`sdcard_kit/LED`），不要只丢一个 `LED.CFG` 在卡根；拷完退出配置再 `ledplay`
- 不要在 shell 任务里 `vTaskDelay` 整段 `wait`（会卡住输入）
- 不要开长文件名就为了 `led_style.json`；要长名须先问用户改 `sdkconfig`
