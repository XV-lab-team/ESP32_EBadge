#include "os_monitor.h"
#include "os_monitor_cmd.h"
#include "os_monitor_port.h"

#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>

/* 栈/CPU 告警：35%~80% 黄，>80% 红（IDLE 不着色） */
#define OS_STATS_ALERT_YELLOW_MIN	35u
#define OS_STATS_ALERT_YELLOW_MAX	80u
/* 概览进度条内部格数（不含方括号；相对初始 20 的 1.5 倍） */
#define OS_STATS_BAR_WIDTH		30u
/* 概览固定行数：标题+CPU/内存条+总占用+内存汇总+空行 */
#define OS_STATS_OVERVIEW_LINES		6u

typedef enum {
	OS_STATS_ALERT_NONE = 0,
	OS_STATS_ALERT_YELLOW,
	OS_STATS_ALERT_RED
} os_stats_alert_t;

/* 与「============概览/详细============」同行总长一致（含中文名） */
static const char s_os_banner_overview[] = "============概览============";
static const char s_os_banner_detail[] = "============详细============";

static char s_os_write_buf[WRITEBUFF_SIZE];
static TaskStatus_t s_os_task_status[OS_STATS_TASK_MAX];
static volatile uint8_t s_os_stats_auto;
static volatile uint8_t s_os_dump_busy;
static portMUX_TYPE s_os_dump_lock = portMUX_INITIALIZER_UNLOCKED;
/* 上次原地刷新占用的行数；0=下次不回退（首次或手动追加后） */
static uint16_t s_os_inplace_lines;

/**
 * 打印与概览/详细标题同宽的全 '=' 收尾行
 * @param inplace 非0：只换一行（便于原地刷新）；0：多空两行便于手动追加阅读
 */
static void os_stats_print_end_banner(uint8_t inplace)
{
	char line[sizeof(s_os_banner_overview) + 2u];
	size_t n = sizeof(s_os_banner_overview) - 1u;
	size_t i;

	if (n >= sizeof(line))
		n = sizeof(line) - 1u;
	for (i = 0u; i < n; i++)
		line[i] = '=';
	line[n] = '\0';
	if (inplace)
		OS_MONITOR_PRINTF("%s\r\n", line);
	else
		OS_MONITOR_PRINTF("%s\r\n\r\n\r\n", line);
}

/**
 * 统计字符串中的 '\\n' 个数（用于原地回退行数）
 */
static unsigned os_stats_count_lf(const char *s)
{
	unsigned n = 0u;

	if (s == NULL)
		return 0u;
	for (; *s != '\0'; s++) {
		if (*s == '\n')
			n++;
	}
	return n;
}

/**
 * 在缓冲末尾写入截断提示，并确保末尾有 '\0'
 */
static void os_stats_mark_truncated(char *buf, size_t capacity)
{
	static const char mark[] = "\r\n...(truncated)\r\n";
	size_t mlen = sizeof(mark) - 1u;

	if (buf == NULL || capacity <= mlen + 1u)
		return;

	memcpy(buf + capacity - 1u - mlen, mark, mlen);
	buf[capacity - 1u] = '\0';
}

/**
 * 向缓冲区追加格式化文本，不越过 capacity
 * @return 追加后已用长度
 */
static size_t os_stats_buf_append(char *buf, size_t capacity, size_t used, const char *fmt, ...)
{
	va_list ap;
	int n;

	if (buf == NULL || capacity < 2u || used >= capacity - 1u)
		return used;

	va_start(ap, fmt);
	n = vsnprintf(buf + used, capacity - used, fmt, ap);
	va_end(ap);

	if (n < 0)
		return used;

	if ((size_t)n >= capacity - used) {
		buf[capacity - 1u] = '\0';
		return capacity - 1u;
	}

	return used + (size_t)n;
}

/**
 * 任务状态转 vTaskList 同款单字符
 */
static char os_stats_state_char(eTaskState state)
{
	switch (state) {
	case eRunning:   return 'X';
	case eReady:     return 'R';
	case eBlocked:   return 'B';
	case eSuspended: return 'S';
	case eDeleted:   return 'D';
	default:         return '?';
	}
}

/**
 * 按创建编号升序排列（最先创建的在前）
 */
static void os_stats_sort_by_create_order(TaskStatus_t *arr, UBaseType_t n)
{
	UBaseType_t i;
	UBaseType_t j;

	if (arr == NULL || n < 2u)
		return;

	for (i = 1u; i < n; i++) {
		TaskStatus_t key = arr[i];

		j = i;
		while (j > 0u && arr[j - 1u].xTaskNumber > key.xTaskNumber) {
			arr[j] = arr[j - 1u];
			j--;
		}
		arr[j] = key;
	}
}

/**
 * 字节转 KB*100（四舍五入，避免依赖 printf 浮点）
 */
static uint32_t os_stats_bytes_to_kb_x100(size_t bytes)
{
	return ((uint32_t)bytes * 100u + 512u) / 1024u;
}

/**
 * 生成 ASCII 进度条，形如 [####------]
 * @param out 输出缓冲
 * @param out_sz 缓冲大小（至少 width+3）
 * @param pct_x10 占用百分比*10（一位小数）
 * @param width 条内格子数
 */
static void os_stats_format_bar(char *out, size_t out_sz, uint32_t pct_x10, unsigned width)
{
	unsigned filled;
	unsigned i;

	if (out == NULL || out_sz < (size_t)width + 3u)
		return;

	if (pct_x10 > 1000u)
		pct_x10 = 1000u;
	/* 四舍五入到格子 */
	filled = (unsigned)((pct_x10 * (uint32_t)width + 500u) / 1000u);
	if (filled > width)
		filled = width;

	out[0] = '[';
	for (i = 0u; i < width; i++)
		out[1u + i] = (i < filled) ? '#' : '-';
	out[1u + width] = ']';
	out[2u + width] = '\0';
}

/**
 * 由 IDLE 运行时间推算系统 CPU 占用（100% - IDLE%），精度 0.1%
 * ESP32 双核：IDLE0+IDLE1，容量 = 墙钟 * 核数，否则会假报近 100%。
 */
static uint32_t os_stats_cpu_busy_pct_x10(const TaskStatus_t *arr, UBaseType_t n,
					 configRUN_TIME_COUNTER_TYPE total_time)
{
	UBaseType_t i;
	uint64_t idle_counter = 0ull;
	uint64_t capacity;
	uint32_t idle_pct_x10;
	unsigned cores;

	if (arr == NULL || n == 0u || total_time == 0)
		return 0u;

	cores = os_monitor_core_count();
	capacity = (uint64_t)total_time * (uint64_t)cores;
	if (capacity == 0ull)
		return 0u;

	for (i = 0u; i < n; i++) {
		if (os_monitor_task_is_idle(arr[i].pcTaskName))
			idle_counter += (uint64_t)arr[i].ulRunTimeCounter;
	}

	idle_pct_x10 = (uint32_t)((idle_counter * 1000ull
				   + capacity / 2ull)
				  / capacity);
	if (idle_pct_x10 > 1000u)
		idle_pct_x10 = 1000u;
	return 1000u - idle_pct_x10;
}

/**
 * 按占用百分比取告警等级（35~80 黄，>80 红）
 * @param pct 占用百分比整数
 * @param skip 非0强制无色（如 IDLE）
 */
static os_stats_alert_t os_stats_alert_level(uint32_t pct, uint8_t skip)
{
	if (skip)
		return OS_STATS_ALERT_NONE;
	if (pct > OS_STATS_ALERT_YELLOW_MAX)
		return OS_STATS_ALERT_RED;
	if (pct >= OS_STATS_ALERT_YELLOW_MIN)
		return OS_STATS_ALERT_YELLOW;
	return OS_STATS_ALERT_NONE;
}

/**
 * 按 0.1% 精度取告警等级（与概览一位小数百分比一致）
 * @param pct_x10 占用百分比*10
 */
static os_stats_alert_t os_stats_alert_level_x10(uint32_t pct_x10)
{
	if (pct_x10 > (OS_STATS_ALERT_YELLOW_MAX * 10u))
		return OS_STATS_ALERT_RED;
	if (pct_x10 >= (OS_STATS_ALERT_YELLOW_MIN * 10u))
		return OS_STATS_ALERT_YELLOW;
	return OS_STATS_ALERT_NONE;
}

/**
 * 告警色 ANSI 开色串（无告警返回空串）
 */
static const char *os_stats_alert_color_open(os_stats_alert_t alert)
{
	if (alert == OS_STATS_ALERT_YELLOW)
		return OS_MONITOR_COLOR_YELLOW;
	if (alert == OS_STATS_ALERT_RED)
		return OS_MONITOR_COLOR_RED;
	return "";
}

/**
 * 告警色 ANSI 复位串（无告警返回空串）
 */
static const char *os_stats_alert_color_close(os_stats_alert_t alert)
{
	if (alert != OS_STATS_ALERT_NONE)
		return OS_MONITOR_COLOR_RESET;
	return "";
}

/**
 * 字段前景色：自身告警优先；否则整行有告警则蓝色；否则无
 */
static const char *os_stats_field_open(os_stats_alert_t field_alert, uint8_t row_alert)
{
	if (field_alert == OS_STATS_ALERT_YELLOW)
		return OS_MONITOR_COLOR_YELLOW;
	if (field_alert == OS_STATS_ALERT_RED)
		return OS_MONITOR_COLOR_RED;
	if (row_alert)
		return OS_MONITOR_COLOR_BLUE;
	return "";
}

/**
 * 字段结束复位：开过色才复位
 */
static const char *os_stats_field_close(os_stats_alert_t field_alert, uint8_t row_alert)
{
	if (field_alert != OS_STATS_ALERT_NONE || row_alert)
		return OS_MONITOR_COLOR_RESET;
	return "";
}

/**
 * 有界打印：概览（CPU/内存）+ 详细（任务栈/CPU，含占用阈值着色）；有截断保护
 * @param inplace 非0：ANSI 上移后原地刷新；0：追加打印（Shell 手动用）
 */
static void os_stats_dump_internal(uint8_t inplace)
{
	UBaseType_t task_num;
	UBaseType_t got;
	configRUN_TIME_COUNTER_TYPE total_time = 0;
	uint64_t total_div100;
	size_t used;
	size_t heap_total;
	size_t heap_free;
	size_t heap_min_free;
	size_t heap_used;
	size_t heap_max_used;
	UBaseType_t i;
	UBaseType_t rows;
	uint8_t truncated;
	unsigned lines;

	taskENTER_CRITICAL(&s_os_dump_lock);
	if (s_os_dump_busy) {
		taskEXIT_CRITICAL(&s_os_dump_lock);
		return;
	}
	s_os_dump_busy = 1u;
	taskEXIT_CRITICAL(&s_os_dump_lock);

	task_num = uxTaskGetNumberOfTasks();
	if (task_num == 0u) {
		taskENTER_CRITICAL(&s_os_dump_lock);
		s_os_dump_busy = 0u;
		taskEXIT_CRITICAL(&s_os_dump_lock);
		return;
	}
	if (task_num > OS_STATS_TASK_MAX)
		task_num = OS_STATS_TASK_MAX;

	memset(s_os_task_status, 0, sizeof(s_os_task_status));
	got = uxTaskGetSystemState(s_os_task_status, task_num, &total_time);
	os_stats_sort_by_create_order(s_os_task_status, got);

	/* ------ 概览：进度条 + 汇总数值（内部 DRAM，不含 8MB PSRAM） ------ */
	heap_total = os_monitor_heap_total();
	heap_free = os_monitor_heap_free();
	heap_min_free = os_monitor_heap_min_free();
	heap_used = (heap_total > heap_free) ? (heap_total - heap_free) : 0u;
	heap_max_used = (heap_total > heap_min_free) ? (heap_total - heap_min_free) : 0u;
	{
		uint32_t total_x100 = os_stats_bytes_to_kb_x100(heap_total);
		uint32_t used_x100 = os_stats_bytes_to_kb_x100(heap_used);
		uint32_t max_used_x100 = os_stats_bytes_to_kb_x100(heap_max_used);
		uint32_t used_pct_x10 = 0u;
		uint32_t max_used_pct_x10 = 0u;
		uint32_t cpu_busy_pct_x10;
		os_stats_alert_t cpu_alert;
		os_stats_alert_t mem_alert;
		const char *o_cpu;
		const char *c_cpu;
		const char *o_mem;
		const char *c_mem;
		char cpu_bar[OS_STATS_BAR_WIDTH + 4u];
		char mem_bar[OS_STATS_BAR_WIDTH + 4u];

		cpu_busy_pct_x10 = os_stats_cpu_busy_pct_x10(s_os_task_status, got, total_time);
		if (heap_total > 0u) {
			used_pct_x10 = ((uint32_t)heap_used * 1000u + (uint32_t)heap_total / 2u)
				/ (uint32_t)heap_total;
			max_used_pct_x10 = ((uint32_t)heap_max_used * 1000u + (uint32_t)heap_total / 2u)
				/ (uint32_t)heap_total;
		}

		os_stats_format_bar(cpu_bar, sizeof(cpu_bar), cpu_busy_pct_x10, OS_STATS_BAR_WIDTH);
		os_stats_format_bar(mem_bar, sizeof(mem_bar), used_pct_x10, OS_STATS_BAR_WIDTH);
		cpu_alert = os_stats_alert_level_x10(cpu_busy_pct_x10);
		mem_alert = os_stats_alert_level_x10(used_pct_x10);
		o_cpu = os_stats_alert_color_open(cpu_alert);
		c_cpu = os_stats_alert_color_close(cpu_alert);
		o_mem = os_stats_alert_color_open(mem_alert);
		c_mem = os_stats_alert_color_close(mem_alert);

		if (inplace && s_os_inplace_lines > 0u)
			OS_MONITOR_PRINTF("\033[%uA", (unsigned)s_os_inplace_lines);
		else if (!inplace)
			OS_MONITOR_PRINTF("\r\n");

		OS_MONITOR_PRINTF("%s\r\n"
		       "CPU占用：\t%s%s%s\t%s%u.%u%%%s\r\n"
		       "内存占用：\t%s%s%s\t%s%u.%u%%%s\r\n"
		       "CPU总占用=%u.%u%%\r\n"
		       "内存总量=%u.%02uKB\t已用=%u.%02uKB(%u.%u%%)\t历史最大占用=%u.%02uKB(%u.%u%%)\r\n\r\n",
		       s_os_banner_overview,
		       o_cpu, cpu_bar, c_cpu,
		       o_cpu, (unsigned)(cpu_busy_pct_x10 / 10u), (unsigned)(cpu_busy_pct_x10 % 10u), c_cpu,
		       o_mem, mem_bar, c_mem,
		       o_mem, (unsigned)(used_pct_x10 / 10u), (unsigned)(used_pct_x10 % 10u), c_mem,
		       (unsigned)(cpu_busy_pct_x10 / 10u), (unsigned)(cpu_busy_pct_x10 % 10u),
		       (unsigned)(total_x100 / 100u), (unsigned)(total_x100 % 100u),
		       (unsigned)(used_x100 / 100u), (unsigned)(used_x100 % 100u),
		       (unsigned)(used_pct_x10 / 10u), (unsigned)(used_pct_x10 % 10u),
		       (unsigned)(max_used_x100 / 100u), (unsigned)(max_used_x100 % 100u),
		       (unsigned)(max_used_pct_x10 / 10u), (unsigned)(max_used_pct_x10 % 10u));
	}

	/* ------ 详细：任务栈 + CPU（含阈值着色） ------ */
	used = 0u;
	truncated = 0u;
	rows = 0u;
	s_os_write_buf[0] = '\0';
	{
		unsigned cores = os_monitor_core_count();
		total_div100 = ((uint64_t)total_time * (uint64_t)cores) / 100ull;
	}
	used = os_stats_buf_append(s_os_write_buf, WRITEBUFF_SIZE, used,
		"%-16s  %-4s  %-6s  %-4s  %-8s  %-16s  %-8s\r\n",
		"任务名", "编号", "优先级", "状态", "总栈", "栈使用", "CPU占用");
	for (i = 0u; i < got; i++) {
		size_t before = used;
		const char *name = s_os_task_status[i].pcTaskName;
		configRUN_TIME_COUNTER_TYPE counter = s_os_task_status[i].ulRunTimeCounter;
		uint32_t cpu_pct;
		size_t stack_total;
		char name_str[20];
		char num_str[8];
		char prio_str[8];
		char state_str[8];
		char total_raw[16];
		char used_raw[32];
		char cpu_raw[12];
		char total_str[16];
		char used_str[32];
		char cpu_str[12];
		os_stats_alert_t stk_alert = OS_STATS_ALERT_NONE;
		os_stats_alert_t cpu_alert;
		uint8_t skip_color;
		uint8_t row_alert;
		const char *o_name;
		const char *c_name;
		const char *o_num;
		const char *c_num;
		const char *o_prio;
		const char *c_prio;
		const char *o_state;
		const char *c_state;
		const char *o_total;
		const char *c_total;
		const char *o_used;
		const char *c_used;
		const char *o_cpu;
		const char *c_cpu;

		if (name == NULL)
			name = "?";

		skip_color = os_monitor_task_is_idle(name);

		if (total_div100 > 0ull)
			cpu_pct = (uint32_t)((uint64_t)counter / total_div100);
		else
			cpu_pct = 0u;
		if (cpu_pct > 100u)
			cpu_pct = 100u;

		cpu_alert = os_stats_alert_level(cpu_pct, skip_color);

		stack_total = os_monitor_stack_total_bytes(&s_os_task_status[i]);
		if (stack_total > 0u) {
			size_t free_words;
			size_t total_words;
			size_t used_words;
			size_t used_bytes;
			uint32_t total_kb_x100;
			uint32_t used_kb_x100;
			uint32_t used_pct_x10;

			total_words = stack_total / sizeof(StackType_t);
			free_words = (size_t)s_os_task_status[i].usStackHighWaterMark;
			if (free_words > total_words)
				free_words = total_words;
			used_words = total_words - free_words;
			used_bytes = used_words * sizeof(StackType_t);
			total_kb_x100 = os_stats_bytes_to_kb_x100(stack_total);
			used_kb_x100 = os_stats_bytes_to_kb_x100(used_bytes);
			used_pct_x10 = ((uint32_t)used_bytes * 1000u + (uint32_t)stack_total / 2u)
				/ (uint32_t)stack_total;

			if (!skip_color) {
				if (used_pct_x10 > (OS_STATS_ALERT_YELLOW_MAX * 10u))
					stk_alert = OS_STATS_ALERT_RED;
				else if (used_pct_x10 >= (OS_STATS_ALERT_YELLOW_MIN * 10u))
					stk_alert = OS_STATS_ALERT_YELLOW;
			}

			(void)snprintf(total_raw, sizeof(total_raw), "%u.%02uKB",
				(unsigned)(total_kb_x100 / 100u), (unsigned)(total_kb_x100 % 100u));
			(void)snprintf(used_raw, sizeof(used_raw), "%u.%02uKB(%u.%u%%)",
				(unsigned)(used_kb_x100 / 100u), (unsigned)(used_kb_x100 % 100u),
				(unsigned)(used_pct_x10 / 10u), (unsigned)(used_pct_x10 % 10u));
		} else {
			(void)snprintf(total_raw, sizeof(total_raw), "?");
			(void)snprintf(used_raw, sizeof(used_raw), "?");
		}

		if (cpu_pct > 0u)
			(void)snprintf(cpu_raw, sizeof(cpu_raw), "%u%%", (unsigned)cpu_pct);
		else
			(void)snprintf(cpu_raw, sizeof(cpu_raw), "<1%%");

		(void)snprintf(name_str, sizeof(name_str), "%-16s", name);
		(void)snprintf(num_str, sizeof(num_str), "%-4u",
			(unsigned)s_os_task_status[i].xTaskNumber);
		(void)snprintf(prio_str, sizeof(prio_str), "%-6u",
			(unsigned)s_os_task_status[i].uxCurrentPriority);
		(void)snprintf(state_str, sizeof(state_str), "%-4c",
			os_stats_state_char(s_os_task_status[i].eCurrentState));
		(void)snprintf(total_str, sizeof(total_str), "%-8s", total_raw);
		(void)snprintf(used_str, sizeof(used_str), "%-16s", used_raw);
		(void)snprintf(cpu_str, sizeof(cpu_str), "%-8s", cpu_raw);

		row_alert = (stk_alert != OS_STATS_ALERT_NONE || cpu_alert != OS_STATS_ALERT_NONE)
			? 1u : 0u;
		o_name = os_stats_field_open(OS_STATS_ALERT_NONE, row_alert);
		c_name = os_stats_field_close(OS_STATS_ALERT_NONE, row_alert);
		o_num = os_stats_field_open(OS_STATS_ALERT_NONE, row_alert);
		c_num = os_stats_field_close(OS_STATS_ALERT_NONE, row_alert);
		o_prio = os_stats_field_open(OS_STATS_ALERT_NONE, row_alert);
		c_prio = os_stats_field_close(OS_STATS_ALERT_NONE, row_alert);
		o_state = os_stats_field_open(OS_STATS_ALERT_NONE, row_alert);
		c_state = os_stats_field_close(OS_STATS_ALERT_NONE, row_alert);
		o_total = os_stats_field_open(OS_STATS_ALERT_NONE, row_alert);
		c_total = os_stats_field_close(OS_STATS_ALERT_NONE, row_alert);
		o_used = os_stats_field_open(stk_alert, row_alert);
		c_used = os_stats_field_close(stk_alert, row_alert);
		o_cpu = os_stats_field_open(cpu_alert, row_alert);
		c_cpu = os_stats_field_close(cpu_alert, row_alert);

		used = os_stats_buf_append(s_os_write_buf, WRITEBUFF_SIZE, used,
			"%s%s%s  %s%s%s  %s%s%s  %s%s%s  %s%s%s  %s%s%s  %s%s%s\r\n",
			o_name, name_str, c_name,
			o_num, num_str, c_num,
			o_prio, prio_str, c_prio,
			o_state, state_str, c_state,
			o_total, total_str, c_total,
			o_used, used_str, c_used,
			o_cpu, cpu_str, c_cpu);
		rows++;

		if (used >= WRITEBUFF_SIZE - 1u) {
			truncated = 1u;
			if (used == before)
				break;
			break;
		}
	}
	if (truncated)
		os_stats_mark_truncated(s_os_write_buf, WRITEBUFF_SIZE);

	if (inplace) {
		while (rows < OS_STATS_TASK_MAX && used < WRITEBUFF_SIZE - 80u) {
			used = os_stats_buf_append(s_os_write_buf, WRITEBUFF_SIZE, used,
				"%-16s  %-4s  %-6s  %-4s  %-8s  %-16s  %-8s\r\n",
				"", "", "", "", "", "", "");
			rows++;
		}
	}

	OS_MONITOR_PRINTF("%s\r\n%s\r\n", s_os_banner_detail, s_os_write_buf);
	os_stats_print_end_banner(inplace);

	if (inplace) {
		lines = OS_STATS_OVERVIEW_LINES
			+ 1u
			+ os_stats_count_lf(s_os_write_buf)
			+ 1u
			+ 1u;
		OS_MONITOR_PRINTF("\033[J");
		s_os_inplace_lines = (uint16_t)lines;
	} else {
		s_os_inplace_lines = 0u;
	}

	taskENTER_CRITICAL(&s_os_dump_lock);
	s_os_dump_busy = 0u;
	taskEXIT_CRITICAL(&s_os_dump_lock);
}

/**
 * 追加打印 OS 状态（Shell os_stats）
 */
void os_stats_dump(void)
{
	os_stats_dump_internal(0u);
}

/**
 * 设置是否周期性打印OS状态
 * @param en 0关闭 非0开启
 */
void os_stats_set_auto(uint8_t en)
{
	s_os_stats_auto = en ? 1u : 0u;
	s_os_inplace_lines = 0u;
}

/**
 * 可选周期打印（独立大栈）
 */
static void os_monitor_task(void *arg)
{
	(void)arg;

	if (s_os_stats_auto)
		os_stats_dump_internal(1u);

	for (;;) {
		vTaskDelay(pdMS_TO_TICKS(OS_STATS_PERIOD_MS));

		if (s_os_stats_auto)
			os_stats_dump_internal(1u);
	}
}

/**
 * 创建 OS 监视任务（独立大栈做周期 dump）
 * @return 与 xTaskCreate 相同：pdPASS(1) 成功，其它失败
 */
int os_monitor_start(void)
{
	BaseType_t res;

	os_monitor_cmd_register();
	res = xTaskCreate(os_monitor_task, "os_monitor", OS_MONITOR_STACK,
			  NULL, OS_MONITOR_TASK_PRIO, NULL);
	return (int)res;
}
