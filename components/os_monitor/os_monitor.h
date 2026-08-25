#ifndef __OS_MONITOR_H_
#define __OS_MONITOR_H_

#include <stdint.h>

/**
 * 追加打印：概览（CPU/内存进度条与汇总）+ 详细（任务栈/CPU，含阈值着色）
 * 周期自动刷新走原地模式，不经过本接口
 */
void os_stats_dump(void);

/**
 * 设置是否周期性打印 OS 状态（开启后约 0.5s 原地刷新）
 * @param en 0关闭 非0开启
 */
void os_stats_set_auto(uint8_t en);

/**
 * 创建 OS 监视任务（独立大栈做周期 dump）
 * @return 与 xTaskCreate 相同：pdPASS(1) 成功，其它失败
 */
int os_monitor_start(void);

#endif
