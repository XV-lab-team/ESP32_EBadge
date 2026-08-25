#ifndef KEY_H
#define KEY_H

#include <stdbool.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "esp_err.h"

/* kaiguan1/2/3. Input + pull-up, pressed = low.
 * KEY1 GPIO18 = 向上, KEY2 GPIO8 = 确定, KEY3 GPIO46 = 向下.
 */
#define KEY1_GPIO                   GPIO_NUM_18
#define KEY2_GPIO                   GPIO_NUM_8
#define KEY3_GPIO                   GPIO_NUM_46

typedef enum {
    KEY_1 = 0,  /* 向上 GPIO18 */
    KEY_2,      /* 确定 GPIO8 */
    KEY_3,      /* 向下 GPIO46 */
    KEY_NUM
} key_id_t;

#define KEY_UP                      KEY_1
#define KEY_ENTER                   KEY_2
#define KEY_DOWN                    KEY_3

typedef enum {
    KEY_EVT_PRESS = 0,
    KEY_EVT_RELEASE,
    KEY_EVT_CLICK,
    KEY_EVT_LONG_PRESS,
} key_event_t;

typedef void (*key_event_cb_t)(key_id_t id, key_event_t evt);

esp_err_t key_init(void);
bool key_is_pressed(key_id_t id);
uint8_t key_get_mask(void);
void key_set_callback(key_event_cb_t cb);
const char *key_name(key_id_t id);
const char *key_event_name(key_event_t evt);
gpio_num_t key_gpio(key_id_t id);

#endif
