#ifndef __OS_MONITOR_CMD_H_
#define __OS_MONITOR_CMD_H_

/*
 * letter-shell commands: os_stats / os_stats_auto.
 * os_monitor_cmd.c lives in a static .a; nothing else referenced it, so ld
 * dropped the object and KEEP(shellCommand) never saw the symbols.
 * Call os_monitor_cmd_register() from os_monitor_start() to pull it in.
 */
void os_monitor_cmd_register(void);

#endif
