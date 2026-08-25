# API 与用法

## 层 1：SDMMC 卡初始化

头文件：`source/BSP/SDMMC/SDMMC.h`

```c
typedef struct {
    int clk_pin;
    int cmd_pin;
    int d0_pin;
    int d1_pin;
    int d2_pin;
    int d3_pin;
    int cd_pin;
    int bus_width;         // 1 或 4
    int max_freq_khz;      // 400 ~ 40000
    bool internal_pullup;
} sdmmc_config_t;

esp_err_t sdmmc_init(const sdmmc_config_t *config, sdmmc_card_t **out_card);
esp_err_t sdmmc_deinit(sdmmc_card_t *card);
```

### 使用默认引脚

```c
sdmmc_card_t *card = NULL;
esp_err_t ret = sdmmc_init(NULL, &card);
if (ret != ESP_OK) {
    ESP_LOGE(TAG, "sdmmc_init: %s", esp_err_to_name(ret));
}
```

成功后会 `sdmmc_card_print_info(stdout, card)`。

### 使用自定义引脚

```c
sdmmc_config_t cfg = {
    .clk_pin = 14,
    .cmd_pin = 15,
    .d0_pin  = 2,
    .d1_pin  = 4,
    .d2_pin  = 12,
    .d3_pin  = 13,
    .cd_pin  = -1,
    .bus_width = 4,
    .max_freq_khz = 20000,
    .internal_pullup = true,
};
sdmmc_card_t *card = NULL;
ESP_ERROR_CHECK(sdmmc_init(&cfg, &card));
```

4-bit 时 `d1/d2/d3` 不能为 `-1`，否则返回 `ESP_ERR_INVALID_ARG`。
`clk/cmd/d0` 任一为 `-1` 同样失败。

### 释放

```c
sdmmc_deinit(card);
card = NULL;
```

调用 MSC 期间不要 deinit。先 `tusb_msc_sdmmc_stop()`，再 deinit。

---

## 层 2：USB MSC

头文件：`source/BSP/SDMMC_tusb_msc/SDMMC_tusb_msc.h`

```c
esp_err_t tusb_msc_sdmmc_start(sdmmc_card_t *card);
esp_err_t tusb_msc_sdmmc_stop(void);
```

`card` 必须已经 `sdmmc_init` 成功。

**阻塞行为：** `start()` 在 USB 和控制台都起来之后，会 `xSemaphoreTake(..., portMAX_DELAY)`，直到 `stop()` 或 REPL 输入 `exit`。因此：

```c
// 错误：放在 app_main 里会卡住整个启动
tusb_msc_sdmmc_start(card);

// 正确：单独任务
static void msc_task(void *arg) {
    tusb_msc_sdmmc_start((sdmmc_card_t *)arg);
    vTaskDelete(NULL);
}
xTaskCreate(msc_task, "msc", 8192, card, 5, NULL);
```

原 APP 层 `SDMMC_tusbmsc_fat_SetTusbMsc()` **没有**建任务，直接调用 `start()`。若在 `app_main` 里按原样调用，后面的 `while(1)` 不会执行。原工程把这两行注释掉了，所以没踩坑。

重复 `start()` 返回 `ESP_ERR_INVALID_STATE`。未 start 就 `stop()` 同样返回该错误。

---

## 层 3：应用胶水

头文件：`source/APP/SDMMC_tusbmsc_fat/SDMMC_tusbmsc_fat.h`

```c
void SDMMC_tusbmsc_fat_init(void);
void SDMMC_tusbmsc_fat_SetTusbMsc(void);
```

两者都是 `void`，失败只打 log。`init` 失败后 `card` 仍为 NULL，再调 `SetTusbMsc` 会把 NULL 传给 TinyUSB。

原 `main.c` 用法（已注释）：

```c
#include "SDMMC_tusbmsc_fat.h"

void app_main(void) {
    // ... 其他初始化 ...
    SDMMC_tusbmsc_fat_init();
    SDMMC_tusbmsc_fat_SetTusbMsc();  // 注意：会阻塞
}
```

---

## 层 4：FAT（原工程没有可用 API）

`sdmmc_fat.h` 是空的。目标工程若要本地读写，不要调用这个模块。自行实现，例如：

```c
esp_err_t sdmmc_fat_mount(sdmmc_card_t *card, const char *base_path);
esp_err_t sdmmc_fat_unmount(void);
```

推荐做法（IDF 5.x）：卡已经由 `sdmmc_init` 初始化过时，用 `esp_vfs_fat_sdmmc_mount` 会 **再次 init host**，冲突。应改为：

- 要么不用 `sdmmc_init`，全程 `esp_vfs_fat_sdmmc_mount`（它内部会 init host+card）；
- 要么保持 `sdmmc_init`，用 FatFS diskio 手动 `ff_diskio_register_sdmmc` + `esp_vfs_fat_register` + `f_mount`（与 TinyUSB MSC 内部类似）。

**不要同时** `esp_vfs_fat_sdmmc_mount` 和 `sdmmc_init` 各 init 一次 host。

与 USB MSC 互斥：MSC 暴露给 PC 时不要本地 mount；本地 mount 时不要 expose。
