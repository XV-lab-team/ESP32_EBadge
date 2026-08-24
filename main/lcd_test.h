#ifndef LCD_TEST_H
#define LCD_TEST_H

#include "esp_err.h"

/* 创建 LCD 准星四色循环测试任务。调用前必须 lcd_init 成功。 */
esp_err_t lcd_test_start(void);

#endif
