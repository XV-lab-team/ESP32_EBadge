#include "os_monitor_port.h"

#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#include "driver/usb_serial_jtag.h"
#include "esp_heap_caps.h"
#include "esp_private/freertos_debug.h"
#include "freertos/idf_additions.h"

#define OS_MONITOR_PRINT_BUF  (WRITEBUFF_SIZE + 256u)
#define OS_MONITOR_USB_CHUNK  512u

int os_monitor_printf(const char *fmt, ...)
{
	static char buf[OS_MONITOR_PRINT_BUF];
	va_list ap;
	int n;
	size_t len;
	size_t off;

	if (fmt == NULL)
		return 0;

	va_start(ap, fmt);
	n = vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);

	if (n <= 0)
		return n;

	len = ((size_t)n >= sizeof(buf)) ? (sizeof(buf) - 1u) : (size_t)n;
	off = 0u;
	while (off < len) {
		size_t chunk = len - off;
		int w;

		if (chunk > OS_MONITOR_USB_CHUNK)
			chunk = OS_MONITOR_USB_CHUNK;
		w = usb_serial_jtag_write_bytes(buf + off, chunk, pdMS_TO_TICKS(200));
		if (w <= 0)
			break;
		off += (size_t)w;
	}
	return n;
}

uint8_t os_monitor_task_is_idle(const char *name)
{
	if (name == NULL)
		return 0u;
	if (strcmp(name, "IDLE") == 0)
		return 1u;
	/* ESP-IDF dual-core: IDLE0 / IDLE1 */
	if (strncmp(name, "IDLE", 4) == 0
	    && name[4] >= '0' && name[4] <= '9'
	    && name[5] == '\0')
		return 1u;
	return 0u;
}

unsigned os_monitor_core_count(void)
{
	unsigned n = (unsigned)configNUMBER_OF_CORES;

	return (n > 0u) ? n : 1u;
}

size_t os_monitor_heap_total(void)
{
	return heap_caps_get_total_size(MALLOC_CAP_INTERNAL);
}

size_t os_monitor_heap_free(void)
{
	return heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
}

size_t os_monitor_heap_min_free(void)
{
	return heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
}

static size_t os_monitor_stack_fallback_bytes(const char *name)
{
	if (name == NULL)
		return 0u;
	if (os_monitor_task_is_idle(name))
		return (size_t)configMINIMAL_STACK_SIZE * sizeof(StackType_t);
	if (strcmp(name, "Tmr Svc") == 0)
		return (size_t)configTIMER_TASK_STACK_DEPTH * sizeof(StackType_t);
	return 0u;
}

size_t os_monitor_stack_total_bytes(const TaskStatus_t *st)
{
	TaskSnapshot_t snap;
	uint8_t *start;
	const uint8_t *end;
	size_t bytes;

	if (st == NULL)
		return 0u;
	if (st->xHandle == NULL)
		return os_monitor_stack_fallback_bytes(st->pcTaskName);

	start = pxTaskGetStackStart(st->xHandle);
	if (start == NULL || vTaskGetSnapshot(st->xHandle, &snap) != pdTRUE
	    || snap.pxEndOfStack == NULL)
		return os_monitor_stack_fallback_bytes(st->pcTaskName);

	end = (const uint8_t *)snap.pxEndOfStack;
	if (end >= start)
		bytes = (size_t)(end - start) + sizeof(StackType_t);
	else
		bytes = (size_t)(start - end) + sizeof(StackType_t);

	/* Task stacks here are hundreds of bytes to tens of KB, not PSRAM-sized. */
	if (bytes >= 256u && bytes <= (256u * 1024u))
		return bytes;

	return os_monitor_stack_fallback_bytes(st->pcTaskName);
}
