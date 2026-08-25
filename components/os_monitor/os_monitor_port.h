#ifndef __OS_MONITOR_PORT_H_
#define __OS_MONITOR_PORT_H_

/*
 * ESP32-S3 / ESP-IDF port for os_monitor.
 *
 * Differences vs the STM32+HAL trainer kit:
 *  - No cmsis_os.h; no TIM6. Run-time stats use esp_timer
 *    (CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS +
 *     CONFIG_FREERTOS_RUN_TIME_STATS_USING_ESP_TIMER).
 *  - Dual-core idle tasks are IDLE0 / IDLE1, not a single "IDLE".
 *  - Heap is heap_caps (internal DRAM), not vanilla heap_4.
 *  - xTaskCreate stack is bytes (Xtensa StackType_t is uint8_t).
 *  - printf goes to UART0 (not wired). Dump writes USB Serial/JTAG.
 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#include <stdint.h>
#include <stddef.h>

#ifndef CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS
#error "os_monitor needs CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS=y (use ESP Timer, not CPU CCOUNT)"
#endif

#ifndef OS_MONITOR_TASK_PRIO
#define OS_MONITOR_TASK_PRIO  (tskIDLE_PRIORITY + 2)
#endif

#ifndef OS_MONITOR_COLOR_ENABLE
#define OS_MONITOR_COLOR_ENABLE  1
#endif

#if OS_MONITOR_COLOR_ENABLE
#ifndef OS_MONITOR_COLOR_YELLOW
#define OS_MONITOR_COLOR_YELLOW  "\033[40;33m"
#endif
#ifndef OS_MONITOR_COLOR_RED
#define OS_MONITOR_COLOR_RED     "\033[40;31m"
#endif
#ifndef OS_MONITOR_COLOR_BLUE
#define OS_MONITOR_COLOR_BLUE    "\033[40;36m"
#endif
#ifndef OS_MONITOR_COLOR_RESET
#define OS_MONITOR_COLOR_RESET   "\033[0m"
#endif
#else
#ifndef OS_MONITOR_COLOR_YELLOW
#define OS_MONITOR_COLOR_YELLOW  ""
#endif
#ifndef OS_MONITOR_COLOR_RED
#define OS_MONITOR_COLOR_RED     ""
#endif
#ifndef OS_MONITOR_COLOR_BLUE
#define OS_MONITOR_COLOR_BLUE    ""
#endif
#ifndef OS_MONITOR_COLOR_RESET
#define OS_MONITOR_COLOR_RESET   ""
#endif
#endif

/* Dual-core + LVGL/shell/ipc: 16 is too small. */
#ifndef OS_STATS_TASK_MAX
#define OS_STATS_TASK_MAX  24u
#endif

#ifndef WRITEBUFF_SIZE
#define WRITEBUFF_SIZE  5120u
#endif

/* ESP-IDF xTaskCreate stack unit is bytes, not 32-bit words. */
#ifndef OS_MONITOR_STACK
#define OS_MONITOR_STACK  6144u
#endif

#ifndef OS_STATS_PERIOD_MS
#define OS_STATS_PERIOD_MS  500u
#endif

int os_monitor_printf(const char *fmt, ...);
#define OS_MONITOR_PRINTF  os_monitor_printf

uint8_t os_monitor_task_is_idle(const char *name);
unsigned os_monitor_core_count(void);
size_t os_monitor_heap_total(void);
size_t os_monitor_heap_free(void);
size_t os_monitor_heap_min_free(void);
size_t os_monitor_stack_total_bytes(const TaskStatus_t *st);

#endif
