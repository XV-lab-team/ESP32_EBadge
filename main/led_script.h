#ifndef LED_SCRIPT_H
#define LED_SCRIPT_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define LED_SCRIPT_DIR              "/sdcard/LED"
#define LED_SCRIPT_DEFAULT_PATH     LED_SCRIPT_DIR "/LED.CFG"

esp_err_t led_script_init(void);
esp_err_t led_script_play(const char *path);
esp_err_t led_script_stop(void);
int led_script_is_playing(void);
const char *led_script_last_error(void);
const char *led_script_path(void);

#ifdef __cplusplus
}
#endif

#endif
