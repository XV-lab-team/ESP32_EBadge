# SD 卡要拷进产品的内容

工程概况：`工程基本情况.md`  
LED 格式：`docs/LED样式文件-给接手AI.md`

仓库里这个目录是**打包盒**。不要把 `sdcard_kit` 这个名字拷到卡上。

把下面标了「拷进产品」的**文件夹整份**放到 SD 根目录（拔卡，或进配置模式当 U 盘）。当前 FAT 只有 **8.3 短文件名**，文件夹和文件都不要用长名。

---

## 拷进产品

把 `LED/` 拷到卡根目录。拷完后卡上应是：

```text
LED/
  LED.CFG       # 默认样式，ledplay 无参数就播这个
  CAL.CFG       # 五灯等 RGB 常亮，用来调白平衡
  RAINBOW.CFG   # 彩虹：12 色相沿灯 1→5 流动
  METEOR.CFG    # 流星：青白头+尾巴，1→5 再 5→1
  BREATH.CFG    # 呼吸：五灯同步青蓝明暗
  STAR.CFG      # 星光：暗底上单灯/双灯闪白
```

设备路径：

| 卡上文件 | 固件路径 |
|----------|----------|
| `LED/LED.CFG` | `/sdcard/LED/LED.CFG` |
| `LED/CAL.CFG` | `/sdcard/LED/CAL.CFG` |
| `LED/RAINBOW.CFG` | `/sdcard/LED/RAINBOW.CFG` |
| `LED/METEOR.CFG` | `/sdcard/LED/METEOR.CFG` |
| `LED/BREATH.CFG` | `/sdcard/LED/BREATH.CFG` |
| `LED/STAR.CFG` | `/sdcard/LED/STAR.CFG` |

不要只把 `LED.CFG` 丢在卡根目录；默认已经改到 `LED` 文件夹里。

---

## 怎么拷

1. 产品进**配置模式**当 U 盘，或把 SD 卡插电脑。
2. 复制本仓库的 `sdcard_kit/LED` 文件夹到 U 盘/卡的**根目录**。
3. 退出配置模式（U 盘会消失，下载口回来），再 shell：

```text
sd
ledplay
ledplay CAL.CFG
ledplay RAINBOW.CFG
ledplay METEOR.CFG
ledplay BREATH.CFG
ledplay STAR.CFG
ledstop
```

`ledplay` 不带参数 = `/sdcard/LED/LED.CFG`。只写文件名时会到 `LED/` 下找：`ledplay CAL.CFG` → `/sdcard/LED/CAL.CFG`。

---

## 白平衡

改 `LED/` 下各 `.CFG` 里同一组 `[cal]`：

```text
gain_r=100
gain_g=100
gain_b=100
```

先 `ledplay CAL.CFG` 看等 RGB（50 50 50）偏什么色，再按接手文档改 gain。测完各 `.CFG` 的 `[cal]` 请改成一样。偏差待定，先全 100。不要改 AW9523 电流档。
