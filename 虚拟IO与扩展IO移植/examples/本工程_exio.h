#ifndef __ESIO_H_
#define __ESIO_H_

#include "aw9523.h"

//外部扩展IO GPIO模式 单独一个引脚的结构体
typedef struct{
  AW9523_PIN      pin;        //引脚在aw9523中的编码
  AW9523_PIN_MODE mode;       //引脚模式
  uint8_t         val_default;//上电默认电平
  uint32_t        chip_num;   //归属芯片
  const char      *desc;      //引脚名称 / 描述
}exio_gpio_pin_t;


//外部扩展IO led模式 单独一个引脚的结构体
typedef struct{
  AW9523_PIN      pin;        //引脚在aw9523中的编码
  uint8_t         val_default;//上电默认电平
  uint32_t        chip_num;   //归属芯片
  const char      *desc;      //引脚名称 / 描述
}exio_led_pin_t;




typedef struct exio_t{
	aw9523_dev aw9523_dev;		//aw9523句柄
	exio_gpio_pin_t exio_gpio_pin[64];	//扩展的GPIO  理论只能挂4个aw9523芯片，一共可以扩展64个引脚	
	exio_led_pin_t exio_led_pin[64];		//led模式的引脚
	
}exio_t;



HAL_StatusTypeDef exio_init(exio_t* dev);  //初始化
HAL_StatusTypeDef exio_apply(exio_t* dev);  //刷新外部芯片





#endif





