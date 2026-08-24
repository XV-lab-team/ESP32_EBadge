#include "shell_app.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "shell.h"
#include "bsp.h"
#include "ring.h"


#include "FreeRTOS.h"
#include "task.h"
#include "cmsis_os.h"
#include "log.h"

#include "uav.h"
#include "logic.h"


ring uart_rx_ring;
char ring_buf[512];
SHELL_TypeDef shell;

signed char myShellRead(char *c){
	if (ring_poll(&uart_rx_ring, c) == 0)		
    return 0;
	else
		return -1;
}

void myShellWrite(const char c){
	while(((huart_Debug.Instance->ISR)&0X40)==0) __NOP();
  huart_Debug.Instance->TDR = (uint8_t) c;     
}

















// shell 任务
void shell_app_task(void* arg){
	vTaskSuspendAll();
	USART_bsp.rx_start(UART_NUM_DEBUG);	//开启 DEBUG 口 USART 接收
	
	ring_init(&uart_rx_ring,ring_buf,sizeof(ring_buf));
  
	shell.read = myShellRead;
	shell.write = myShellWrite;
	shellInit(&shell);
	xTaskResumeAll();
	for(;;){
		
		if(USART_bsp.mes[UART_NUM_DEBUG]->rxstate == RX_OVER){
			USART_bsp.mes[UART_NUM_DEBUG]->rxstate = RX_ING;
			for(uint32_t i=0;i<USART_bsp.mes[UART_NUM_DEBUG]->rxsize;i++)
				ring_push(&uart_rx_ring, USART_bsp.mes[UART_NUM_DEBUG]->rxdata[i]);  
		}
		shellTask(&shell);
	
		osDelay(10);
		
	}

}

// shell 命令

// 设置电机 PWM 刻度
static int shell_cmd_motor_scale(int argc, char *argv[])
{
    if (argc < 3)
        return -1;

    uint32_t motor = (uint32_t)strtoul(argv[1], NULL, 10);
    uint32_t scale = (uint32_t)strtoul(argv[2], NULL, 10);

    uav_motor_set_scale(motor, scale);
    return 0;
}


// 设置舵机 PWM 刻度
static int shell_cmd_servo_scale(int argc, char *argv[])
{
    if (argc < 3)
        return -1;

    uint32_t servo = (uint32_t)strtoul(argv[1], NULL, 10);
    uint32_t scale = (uint32_t)strtoul(argv[2], NULL, 10);

    uav_servo_set_scale(servo, scale);
    return 0;
}

// 设置控制使能
static int shell_cmd_ctr_en(int argc, char *argv[])
{
    if (argc < 2)
        return -1;

    uint8_t en = (uint8_t)strtoul(argv[1], NULL, 10);
    uav_set_ctr_en(en);
    return 0;
}

// 设置控制模式
static int shell_cmd_ctr_mode(int argc, char *argv[])
{
    if (argc < 2)
        return -1;

    uint8_t mode = (uint8_t)strtoul(argv[1], NULL, 10);
    uav_set_ctr_mode(mode);
    return 0;
}


// 设备信息
static int shell_cmd_info(int argc, char *argv[])
{
	(void)argc;
	(void)argv;
	SHELL_TypeDef *sh = shellGetCurrent();
	if (sh == NULL)
		return -1;

	shellPrint(sh, "---- device info ----\r\n");

           
	return 0;
}

static int shell_cmd_rst(int argc, char *argv[])
{
	(void)argc;
	(void)argv;
	SHELL_TypeDef *sh = shellGetCurrent();
	if (sh != NULL) {
		shellPrint(sh, "MCU reset...\r\n");
	}
	/* 可选：留一点时间让打印输出完成 */
	osDelay(10);
	

  HAL_NVIC_SystemReset();
	
	
	NVIC_SystemReset();
	int a=1;
	while(a);
	/* 不会执行到 */
	return 0;
}

// 立刻打印OS状态
static int shell_cmd_os_stats(int argc, char *argv[])
{
	(void)argc;
	(void)argv;
	os_stats_dump();
	return 0;
}

// 周期打印OS状态开关 os_stats_auto <0|1>
static int shell_cmd_os_stats_auto(int argc, char *argv[])
{
	if (argc < 2)
		return -1;

	uint8_t en = (uint8_t)strtoul(argv[1], NULL, 10);
	os_stats_set_auto(en);
	return 0;
}


SHELL_EXPORT_CMD_EX(motor_scale, 	shell_cmd_motor_scale,			"设置电机PWM刻度",			motor_scale <motor> <scale>);
SHELL_EXPORT_CMD_EX(info, 			 	shell_cmd_info,           	"设备信息",	 					  info);
SHELL_EXPORT_CMD_EX(rst, 					shell_cmd_rst,							"复位", 							  mcu_reset);
SHELL_EXPORT_CMD_EX(servo_scale,	shell_cmd_servo_scale, 			"设置舵机PWM刻度",      servo_scale <servo> <scale>);
SHELL_EXPORT_CMD_EX(ctr_en,     	shell_cmd_ctr_en,      			"设置控制使能",         ctr_en <0|1>);
SHELL_EXPORT_CMD_EX(ctr_mode,   	shell_cmd_ctr_mode,    			"设置控制模式",         ctr_mode <mode>);


SHELL_EXPORT_CMD_EX(os_stats,		  shell_cmd_os_stats,			  	"打印OS状态",		        os_stats);
SHELL_EXPORT_CMD_EX(os_stats_auto,shell_cmd_os_stats_auto,		"周期打印OS状态开关",	os_stats_auto <0|1>);

