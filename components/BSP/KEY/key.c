#include "key.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "key";

#define KEY_POLL_MS                 10
#define KEY_DEBOUNCE_MS             20
#define KEY_LONG_PRESS_MS           800
#define KEY_DEBOUNCE_TICKS          (KEY_DEBOUNCE_MS / KEY_POLL_MS)

/* ESP_LOGI in this task needs more than 2 KB; overflow reboots USB. */
#define KEY_TASK_STACK_BYTES        4096
#define KEY_TASK_PRIORITY           3

typedef enum {
    KEY_ST_IDLE = 0,
    KEY_ST_DOWN,
    KEY_ST_LONG,
} key_fsm_t;

typedef struct {
    gpio_num_t gpio;
    const char *name;
    uint8_t debounce_cnt;
    uint8_t stable_pressed;
    key_fsm_t fsm;
    uint16_t hold_ms;
} key_slot_t;

static key_slot_t s_key[KEY_NUM] = {
    { .gpio = KEY1_GPIO, .name = "KEY1" },
    { .gpio = KEY2_GPIO, .name = "KEY2" },
    { .gpio = KEY3_GPIO, .name = "KEY3" },
};

static key_event_cb_t s_cb;
static uint8_t s_started;

static const char *s_evt_name[] = {
    "press",
    "release",
    "click",
    "long_press",
};

const char *key_name(key_id_t id)
{
    if (id >= KEY_NUM) {
        return "?";
    }
    return s_key[id].name;
}

const char *key_event_name(key_event_t evt)
{
    if ((unsigned)evt >= (sizeof(s_evt_name) / sizeof(s_evt_name[0]))) {
        return "?";
    }
    return s_evt_name[evt];
}

gpio_num_t key_gpio(key_id_t id)
{
    if (id >= KEY_NUM) {
        return GPIO_NUM_NC;
    }
    return s_key[id].gpio;
}

bool key_is_pressed(key_id_t id)
{
    if (id >= KEY_NUM) {
        return false;
    }
    return s_key[id].stable_pressed != 0;
}

uint8_t key_get_mask(void)
{
    uint8_t mask = 0;
    int i;

    for (i = 0; i < KEY_NUM; i++) {
        if (s_key[i].stable_pressed) {
            mask |= (uint8_t)(1u << i);
        }
    }
    return mask;
}

void key_set_callback(key_event_cb_t cb)
{
    s_cb = cb;
}

static void key_emit(key_id_t id, key_event_t evt)
{
    if (s_cb) {
        s_cb(id, evt);
    }
}

static void key_fsm_step(key_id_t id, uint8_t pressed)
{
    key_slot_t *k = &s_key[id];

    switch (k->fsm) {
    case KEY_ST_IDLE:
        if (pressed) {
            k->fsm = KEY_ST_DOWN;
            k->hold_ms = 0;
            key_emit(id, KEY_EVT_PRESS);
        }
        break;

    case KEY_ST_DOWN:
        if (!pressed) {
            k->fsm = KEY_ST_IDLE;
            key_emit(id, KEY_EVT_RELEASE);
            key_emit(id, KEY_EVT_CLICK);
        } else {
            k->hold_ms = (uint16_t)(k->hold_ms + KEY_POLL_MS);
            if (k->hold_ms >= KEY_LONG_PRESS_MS) {
                k->fsm = KEY_ST_LONG;
                key_emit(id, KEY_EVT_LONG_PRESS);
            }
        }
        break;

    case KEY_ST_LONG:
        if (!pressed) {
            k->fsm = KEY_ST_IDLE;
            key_emit(id, KEY_EVT_RELEASE);
        }
        break;

    default:
        k->fsm = KEY_ST_IDLE;
        break;
    }
}

static void key_poll_one(key_id_t id)
{
    key_slot_t *k = &s_key[id];
    uint8_t raw_pressed = (gpio_get_level(k->gpio) == 0) ? 1 : 0;

    if (raw_pressed == k->stable_pressed) {
        k->debounce_cnt = 0;
    } else {
        k->debounce_cnt++;
        if (k->debounce_cnt >= KEY_DEBOUNCE_TICKS) {
            k->stable_pressed = raw_pressed;
            k->debounce_cnt = 0;
        }
    }

    key_fsm_step(id, k->stable_pressed);
}

static void key_task(void *arg)
{
    (void)arg;
    while (1) {
        key_poll_one(KEY_1);
        key_poll_one(KEY_2);
        key_poll_one(KEY_3);
        vTaskDelay(pdMS_TO_TICKS(KEY_POLL_MS));
    }
}

esp_err_t key_init(void)
{
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << KEY1_GPIO) | (1ULL << KEY2_GPIO) | (1ULL << KEY3_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err;
    int i;

    if (s_started) {
        return ESP_OK;
    }

    err = gpio_config(&io);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "gpio_config failed: %s", esp_err_to_name(err));
        return err;
    }

    /*
     * GPIO46 is a strapping pin with a reset-time pull-down. Disable that
     * pull-down after boot and keep the software pull-up so KEY3 reads high
     * when released. Do not add a board-level pull-up to 3.3 V on GPIO46.
     */
    gpio_pulldown_dis(KEY3_GPIO);
    gpio_pullup_en(KEY3_GPIO);
    gpio_pullup_en(KEY1_GPIO);
    gpio_pullup_en(KEY2_GPIO);

    for (i = 0; i < KEY_NUM; i++) {
        s_key[i].stable_pressed = 0;
        s_key[i].fsm = KEY_ST_IDLE;
        s_key[i].debounce_cnt = 0;
        s_key[i].hold_ms = 0;
    }

    if (xTaskCreate(key_task, "key", KEY_TASK_STACK_BYTES, NULL,
                    KEY_TASK_PRIORITY, NULL) != pdPASS) {
        ESP_LOGE(TAG, "xTaskCreate(key) failed");
        return ESP_ERR_NO_MEM;
    }

    s_started = 1;
    ESP_LOGI(TAG, "init KEY1=GPIO%d KEY2=GPIO%d KEY3=GPIO%d (pull-up, pressed=low)",
             (int)KEY1_GPIO, (int)KEY2_GPIO, (int)KEY3_GPIO);
    return ESP_OK;
}
