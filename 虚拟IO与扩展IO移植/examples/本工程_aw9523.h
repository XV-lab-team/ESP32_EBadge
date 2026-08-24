#ifndef __AW9523_H_
#define __AW9523_H_

#include "stdint.h"
#include "i2c.h"


#define AW9523_CHIP_NUM_MAX				4		//AW9523芯片数量最大值
//引脚模式枚举
typedef enum{
  AW9523_PIN_MODE_NULL  = 0,
	AW9523_PIN_MODE_IN,     //输入模式
	AW9523_PIN_MODE_OUT,		//输出模式	推挽输出
	AW9523_PIN_MODE_LED,		//LED模式
}	AW9523_PIN_MODE;
	
//aw9523引脚枚举
typedef enum{
  AW9523_PIN_NULL = (uint32_t)0,
  AW9523_PIN_0_0  = (uint32_t)1<<0,
  AW9523_PIN_0_1  = (uint32_t)1<<1,
  AW9523_PIN_0_2  = (uint32_t)1<<2,
  AW9523_PIN_0_3  = (uint32_t)1<<3,
  AW9523_PIN_0_4  = (uint32_t)1<<4,
  AW9523_PIN_0_5  = (uint32_t)1<<5,
  AW9523_PIN_0_6  = (uint32_t)1<<6,
  AW9523_PIN_0_7  = (uint32_t)1<<7,
  AW9523_PIN_1_0  = (uint32_t)1<<8,
  AW9523_PIN_1_1  = (uint32_t)1<<9,
  AW9523_PIN_1_2  = (uint32_t)1<<10,
  AW9523_PIN_1_3  = (uint32_t)1<<11,
  AW9523_PIN_1_4  = (uint32_t)1<<12,
  AW9523_PIN_1_5  = (uint32_t)1<<13,
  AW9523_PIN_1_6  = (uint32_t)1<<14,
  AW9523_PIN_1_7  = (uint32_t)1<<15,
  AW9523_PIN_ALL  = (uint32_t)0xFFFF,
}AW9523_PIN;

//一个AW9523芯片结构体
typedef struct {
	uint8_t a1;   //芯片A1值
	uint8_t a0;   //芯片A0值
  
  uint8_t gpio_p0_mod_pp;   //P0推挽模式
  uint8_t ledmode_isel; //LED模式下的Imax   0:Imax为4/4  1:Imax为3/4    2:Imax为2/4    3:Imax为1/4

  
	AW9523_PIN_MODE p0_mode[8];	//P0引脚的模式
	AW9523_PIN_MODE p1_mode[8];	//P0引脚的模式

	uint8_t p0_val[8];	//P0引脚的值
	uint8_t p1_val[8];	//P0引脚的值	

  uint8_t p0_led_dim[8];	//P0引脚LED模式调光等级
	uint8_t p1_led_dim[8];	//P1引脚LED模式调光等级	
  
}aw9523_t;


//AW9523芯片设备结构体
typedef struct {
	I2C_HandleTypeDef*	hi2c;			//i2c句柄
	GPIO_TypeDef* rst_gpio_port;	//RST引脚的port
	uint16_t 			rst_gpio_pin;		//RST引脚的pin
	
	uint32_t			chip_num;				//芯片数量
	aw9523_t			chip[AW9523_CHIP_NUM_MAX];	//单个芯片信息
}aw9523_dev;


HAL_StatusTypeDef aw9523_init(aw9523_dev* dev); //初始化

HAL_StatusTypeDef aw9523_set_pin_mode(aw9523_dev* dev, uint32_t chip, AW9523_PIN pin, AW9523_PIN_MODE mode); //设置引脚模式    （只写缓存）
HAL_StatusTypeDef aw9523_set_pin_gpioval(aw9523_dev* dev, uint32_t chip, AW9523_PIN pin, uint8_t val); //设置引脚高低电平    （只写缓存）
HAL_StatusTypeDef aw9523_set_pin_ledval(aw9523_dev* dev, uint32_t chip, AW9523_PIN pin, uint8_t val); //设置引脚LED模式下的LED调光等级  （只写缓存）

HAL_StatusTypeDef aw9523_apply_config(aw9523_dev* dev); //将缓存中的芯片配置写入芯片
HAL_StatusTypeDef aw9523_apply_output(aw9523_dev* dev); //将缓存中的芯片引脚状态写入芯片


#endif







