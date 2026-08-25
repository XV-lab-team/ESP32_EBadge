#include "os_monitor_cmd.h"
#include "os_monitor.h"
#include "shell.h"

#include <stdlib.h>
#include <stdint.h>

void os_monitor_cmd_register(void)
{
}

static int shell_cmd_os_stats(int argc, char *argv[])
{
	(void)argc;
	(void)argv;
	os_stats_dump();
	return 0;
}

static int shell_cmd_os_stats_auto(int argc, char *argv[])
{
	uint8_t en;

	if (argc < 2)
		return -1;

	en = (uint8_t)strtoul(argv[1], NULL, 10);
	os_stats_set_auto(en);
	return 0;
}

SHELL_EXPORT_CMD_EX(os_stats, shell_cmd_os_stats, "print OS stats", os_stats);
SHELL_EXPORT_CMD_EX(os_stats_auto, shell_cmd_os_stats_auto,
		    "period dump on/off", os_stats_auto <0|1>);
