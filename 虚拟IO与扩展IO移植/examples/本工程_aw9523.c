#include "aw9523.h"
#include "bsp.h"

#include "log.h"
#define TAG "AW9523"

#define I2C_TIMOUT_MS                     (10)		//i2C通信超时时间 


#define AW9523_MCU_GPIO_OUT_RESET(port,pin)		GPIO_OUT_RESET(port,pin)	//MCU gpio输出低电平
#define AW9523_MCU_GPIO_OUT_SET(port,pin)	  	GPIO_OUT_SET(port,pin)		//MCU gpio输出高电平




 
#define AW9523_ADDRESS                     (0xB0)		//器件I2C地址 通信时要加上A1A0
 

//寄存器映射
#define INPUT_PORT0                     (0x00)//R   	P0输入状态
#define INPUT_PORT1                     (0x01)//R   	P1输入状态
#define OUTPUT_PORT0                    (0x02)//W/R 	P0输出状态
#define OUTPUT_PORT1                    (0x03)//W/R 	P1输出状态
#define CONFIG_PORT0                    (0x04)//W/R 	P0端口方向配置
#define CONFIG_PORT1                    (0x05)//W/R 	P1端口方向配置
#define INT_PORT0                       (0x06)//W/R 	P0中断使能
#define INT_PORT1                       (0x07)//W/R 	P1中断使能
#define ID                              (0x10)//R 		ID寄存器
#define GCR                             (0x11)//W/R 	全局控制寄存器
#define LED_MODE_SWITCH_P0              (0x12)//W/R 	P0工作模式切换
#define LED_MODE_SWITCH_P1              (0x13)//W/R 	P1工作模式切换
#define DIM0                            (0x20)// W 		P1_0 LED电流控制
#define DIM1                            (0x21)// W 		P1_1 LED电流控制
#define DIM2                            (0x22)// W 		P1_2 LED电流控制
#define DIM3                            (0x23)// W 		P1_3 LED电流控制
#define DIM4                            (0x24)// W 		P0_0 LED电流控制
#define DIM5                            (0x25)// W 		P0_1 LED电流控制
#define DIM6                            (0x26)// W 		P0_2 LED电流控制
#define DIM7                            (0x27)// W 		P0_3 LED电流控制
#define DIM8                            (0x28)// W 		P0_4 LED电流控制
#define DIM9                            (0x29)// W 		P0_5 LED电流控制
#define DIM10                           (0x2A)// W 		P0_6 LED电流控制
#define DIM11                           (0x2B)// W		P0_7 LED电流控制
#define DIM12                           (0x2C)// W 		P1_4 LED电流控制
#define DIM13                           (0x2D)// W 		P1_5 LED电流控制
#define DIM14                           (0x2E)// W 		P1_6 LED电流控制
#define DIM15                           (0x2F)// W 		P1_7 LED电流控制
#define SOFT_RESET                      (0x7F)// W 		软件复位




//i2c发送接口
static inline HAL_StatusTypeDef aw9523_i2c_transmit(I2C_HandleTypeDef *hi2c, uint16_t DevAddress, uint8_t *pData, uint16_t Size);
static HAL_StatusTypeDef aw9523_SetAllIo_Hiz(aw9523_dev* dev, uint32_t chip);					//将所有IO设置为高阻模式
static HAL_StatusTypeDef aw9523_set_reg(I2C_HandleTypeDef *hi2c, uint16_t DevAddress, uint8_t RegAddress, uint8_t *pData, uint16_t Size);	//设置寄存器





//aw9523初始化  初始化时会将所有gpio设为高阻模式
HAL_StatusTypeDef aw9523_init(aw9523_dev* dev){
	//参数合法性检查
	if(dev == NULL)					return HAL_ERROR;
	if(dev->hi2c == NULL)		return HAL_ERROR;
	if(dev->chip_num > AW9523_CHIP_NUM_MAX)	return HAL_ERROR;
	if(dev->rst_gpio_port == NULL)					return HAL_ERROR;
	
	HAL_StatusTypeDef res = HAL_OK;
	
	AW9523_MCU_GPIO_OUT_RESET(dev->rst_gpio_port, dev->rst_gpio_pin);
	AW9523_MCU_GPIO_OUT_SET(dev->rst_gpio_port, dev->rst_gpio_pin);
	
  //设置为高阻输出
	for(uint32_t num=0; num<dev->chip_num; num++)
		res |= aw9523_SetAllIo_Hiz(dev, num);
	

  //配置全局控制寄存器
	for(uint32_t num=0; num<dev->chip_num; num++){
    uint8_t reg_ctl = 0;
    if(dev->chip[num].gpio_p0_mod_pp)
      reg_ctl |= (0x01 << 4); 
    
    if(dev->chip[num].ledmode_isel !=0){
      if(dev->chip[num].ledmode_isel > 3)
        dev->chip[num].ledmode_isel = 3;
      reg_ctl |= (dev->chip[num].ledmode_isel & 0x03);

      uint8_t chip_addr = AW9523_ADDRESS;	
      if(dev->chip[num].a0)		chip_addr |= 0x02;
      if(dev->chip[num].a1)		chip_addr |= 0x04;
      
      res |= aw9523_set_reg(dev->hi2c, chip_addr, GCR, &reg_ctl, 1); 
    }
  }
		
	return res;
}

//设置引脚模式  （只写缓存）
HAL_StatusTypeDef aw9523_set_pin_mode(aw9523_dev* dev, uint32_t chip, AW9523_PIN pin, AW9523_PIN_MODE mode){
  //参数验证
  if(dev == NULL)
    return HAL_ERROR;
  if(mode != AW9523_PIN_MODE_IN && mode != AW9523_PIN_MODE_OUT && mode != AW9523_PIN_MODE_LED)
    return HAL_ERROR;
  if(chip >= dev->chip_num || chip >= AW9523_CHIP_NUM_MAX)
    return HAL_ERROR;
  
  if(mode == AW9523_PIN_MODE_IN){
    XK_LOGE(TAG, "AW9523_PIN_MODE_IN模式未编程！！！！");
    return HAL_ERROR;
  }
  
	
  if(pin == AW9523_PIN_NULL)
    return HAL_OK;
  
  for(uint32_t i=0; i<16; i++){
    if( (pin & (0x01<<i)) != 0){
      switch(0x01<<i)  //找到有效Pin
      {
        case AW9523_PIN_0_0 : dev->chip[chip].p0_mode[0] = mode;    break;
        case AW9523_PIN_0_1 : dev->chip[chip].p0_mode[1] = mode;    break; 
        case AW9523_PIN_0_2 : dev->chip[chip].p0_mode[2] = mode;    break; 
        case AW9523_PIN_0_3 : dev->chip[chip].p0_mode[3] = mode;    break; 
        case AW9523_PIN_0_4 : dev->chip[chip].p0_mode[4] = mode;    break; 
        case AW9523_PIN_0_5 : dev->chip[chip].p0_mode[5] = mode;    break; 
        case AW9523_PIN_0_6 : dev->chip[chip].p0_mode[6] = mode;    break; 
        case AW9523_PIN_0_7 : dev->chip[chip].p0_mode[7] = mode;    break; 
        case AW9523_PIN_1_0 : dev->chip[chip].p1_mode[0] = mode;    break; 
        case AW9523_PIN_1_1 : dev->chip[chip].p1_mode[1] = mode;    break; 
        case AW9523_PIN_1_2 : dev->chip[chip].p1_mode[2] = mode;    break; 
        case AW9523_PIN_1_3 : dev->chip[chip].p1_mode[3] = mode;    break; 
        case AW9523_PIN_1_4 : dev->chip[chip].p1_mode[4] = mode;    break; 
        case AW9523_PIN_1_5 : dev->chip[chip].p1_mode[5] = mode;    break; 
        case AW9523_PIN_1_6 : dev->chip[chip].p1_mode[6] = mode;    break; 
        case AW9523_PIN_1_7 : dev->chip[chip].p1_mode[7] = mode;    break; 
        default:  break;
      }
    }
  }
  
  return HAL_OK;
}


//将缓存中的芯片配置写入芯片
HAL_StatusTypeDef aw9523_apply_config(aw9523_dev* dev){
  //参数合法性检查
	if(dev == NULL)					return HAL_ERROR;
	if(dev->hi2c == NULL)		return HAL_ERROR;
	if(dev->chip_num > AW9523_CHIP_NUM_MAX)	return HAL_ERROR;
	if(dev->rst_gpio_port == NULL)					return HAL_ERROR;
	
	HAL_StatusTypeDef res = HAL_OK;
	
  for(uint32_t chip=0; chip<dev->chip_num; chip++){

    uint8_t chip_addr = AW9523_ADDRESS;	
    if(dev->chip[chip].a0)		chip_addr |= 0x02;
    if(dev->chip[chip].a1)		chip_addr |= 0x04;
    uint8_t reg_cfg[2]={0};          //输入输出配置寄存器
    uint8_t reg_led_mod_sw_p[2]={0}; //P0LED模式切换寄存器
    
    //拼接芯片寄存器值
    for(uint8_t i=0; i<8; i++){
      if(dev->chip[chip].p0_mode[i] == AW9523_PIN_MODE_IN){
        reg_cfg[0] |= (0x01 << i);
        XK_LOGE(TAG, "aw9523_apply_config 输入模式未编程！！！" );
        res |= HAL_ERROR;
      }
      if(dev->chip[chip].p1_mode[i] == AW9523_PIN_MODE_IN){
        reg_cfg[1] |= (0x01 << i);
        XK_LOGE(TAG, "aw9523_apply_config 输入模式未编程！！！" );
        res |= HAL_ERROR;
      }  
    }

    for(uint8_t i=0; i<8; i++){
      if(dev->chip[chip].p0_mode[i] == AW9523_PIN_MODE_OUT)
        reg_led_mod_sw_p[0] |= (0x01 << i);
      if(dev->chip[chip].p1_mode[i] == AW9523_PIN_MODE_OUT)
        reg_led_mod_sw_p[1] |= (0x01 << i); 
    }

    //将寄存器值写入芯片
    res |= aw9523_set_reg(dev->hi2c, chip_addr ,CONFIG_PORT0,       reg_cfg,2);
    res |= aw9523_set_reg(dev->hi2c, chip_addr ,LED_MODE_SWITCH_P0, reg_led_mod_sw_p,2);
  }
  
  return res;
}

//将缓存中的芯片引脚状态写入芯片
HAL_StatusTypeDef aw9523_apply_output(aw9523_dev* dev){
  //参数合法性检查
	if(dev == NULL)					return HAL_ERROR;
	if(dev->hi2c == NULL)		return HAL_ERROR;
	if(dev->chip_num > AW9523_CHIP_NUM_MAX)	return HAL_ERROR;
	if(dev->rst_gpio_port == NULL)					return HAL_ERROR;
	
	HAL_StatusTypeDef res = HAL_OK;
	
  for(uint32_t chip=0; chip<dev->chip_num; chip++){

    uint8_t chip_addr = AW9523_ADDRESS;	
    if(dev->chip[chip].a0)		chip_addr |= 0x02;
    if(dev->chip[chip].a1)		chip_addr |= 0x04;
    uint8_t reg_out[2]={0};   //输出状态寄存器
    uint8_t reg_dim[16]={0};  //LED调光等级寄存器 DIM 注意不是顺序的，是乱序的

    //拼接芯片寄存器值
    for(uint8_t i=0; i<8; i++){
      if(dev->chip[chip].p0_val[i])
        reg_out[0] |= (0x01 << i);
      if(dev->chip[chip].p1_val[i])
        reg_out[1] |= (0x01 << i);
    }
    reg_dim[0] =dev->chip[chip].p1_led_dim[0];    reg_dim[1] =dev->chip[chip].p1_led_dim[1];    reg_dim[2] =dev->chip[chip].p1_led_dim[2];    reg_dim[3] =dev->chip[chip].p1_led_dim[3];
    reg_dim[4] =dev->chip[chip].p0_led_dim[0];    reg_dim[5] =dev->chip[chip].p0_led_dim[1];    reg_dim[6] =dev->chip[chip].p0_led_dim[2];    reg_dim[7] =dev->chip[chip].p0_led_dim[3];
    reg_dim[8] =dev->chip[chip].p0_led_dim[4];    reg_dim[9] =dev->chip[chip].p0_led_dim[5];    reg_dim[10]=dev->chip[chip].p0_led_dim[6];    reg_dim[11]=dev->chip[chip].p0_led_dim[7];
    reg_dim[12]=dev->chip[chip].p1_led_dim[4];    reg_dim[13]=dev->chip[chip].p1_led_dim[5];    reg_dim[14]=dev->chip[chip].p1_led_dim[6];    reg_dim[15]=dev->chip[chip].p1_led_dim[7];
    //将寄存器值写入芯片
    res |= aw9523_set_reg(dev->hi2c, chip_addr ,OUTPUT_PORT0, reg_out,2);
    res |= aw9523_set_reg(dev->hi2c, chip_addr ,DIM0,         reg_dim,16);
  }
  
  return res;



}

//设置引脚高低电平  （只写缓存）
HAL_StatusTypeDef aw9523_set_pin_gpioval(aw9523_dev* dev, uint32_t chip, AW9523_PIN pin, uint8_t val){
  //参数验证
  if(dev == NULL)
    return HAL_ERROR;
  if(chip >= dev->chip_num || chip >= AW9523_CHIP_NUM_MAX)
    return HAL_ERROR;
  
  if(pin == AW9523_PIN_NULL)
    return HAL_OK;
  
  for(uint32_t i=0; i<16; i++){
    if( (pin & (0x01<<i)) != 0){
      switch(0x01<<i)  //找到有效Pin
      {
        case AW9523_PIN_0_0 : dev->chip[chip].p0_val[0] = val;    break;
        case AW9523_PIN_0_1 : dev->chip[chip].p0_val[1] = val;    break; 
        case AW9523_PIN_0_2 : dev->chip[chip].p0_val[2] = val;    break; 
        case AW9523_PIN_0_3 : dev->chip[chip].p0_val[3] = val;    break; 
        case AW9523_PIN_0_4 : dev->chip[chip].p0_val[4] = val;    break; 
        case AW9523_PIN_0_5 : dev->chip[chip].p0_val[5] = val;    break; 
        case AW9523_PIN_0_6 : dev->chip[chip].p0_val[6] = val;    break; 
        case AW9523_PIN_0_7 : dev->chip[chip].p0_val[7] = val;    break; 
        case AW9523_PIN_1_0 : dev->chip[chip].p1_val[0] = val;    break; 
        case AW9523_PIN_1_1 : dev->chip[chip].p1_val[1] = val;    break; 
        case AW9523_PIN_1_2 : dev->chip[chip].p1_val[2] = val;    break; 
        case AW9523_PIN_1_3 : dev->chip[chip].p1_val[3] = val;    break; 
        case AW9523_PIN_1_4 : dev->chip[chip].p1_val[4] = val;    break; 
        case AW9523_PIN_1_5 : dev->chip[chip].p1_val[5] = val;    break; 
        case AW9523_PIN_1_6 : dev->chip[chip].p1_val[6] = val;    break; 
        case AW9523_PIN_1_7 : dev->chip[chip].p1_val[7] = val;    break; 
        default:  break;
      }
    }
  }
  
  return HAL_OK;
}

//设置引脚LED模式下的LED调光等级  （只写缓存）
HAL_StatusTypeDef aw9523_set_pin_ledval(aw9523_dev* dev, uint32_t chip, AW9523_PIN pin, uint8_t led_val){
  //参数验证
  if(dev == NULL)
    return HAL_ERROR;
  if(chip >= dev->chip_num || chip >= AW9523_CHIP_NUM_MAX)
    return HAL_ERROR;
  
  if(pin == AW9523_PIN_NULL)
    return HAL_OK;
  
  for(uint32_t i=0; i<16; i++){
    if( (pin & (0x01<<i)) != 0){
      switch(0x01<<i)  //找到有效Pin
      {
        case AW9523_PIN_0_0 : dev->chip[chip].p0_led_dim[0] = led_val;    break;
        case AW9523_PIN_0_1 : dev->chip[chip].p0_led_dim[1] = led_val;    break; 
        case AW9523_PIN_0_2 : dev->chip[chip].p0_led_dim[2] = led_val;    break; 
        case AW9523_PIN_0_3 : dev->chip[chip].p0_led_dim[3] = led_val;    break; 
        case AW9523_PIN_0_4 : dev->chip[chip].p0_led_dim[4] = led_val;    break; 
        case AW9523_PIN_0_5 : dev->chip[chip].p0_led_dim[5] = led_val;    break; 
        case AW9523_PIN_0_6 : dev->chip[chip].p0_led_dim[6] = led_val;    break; 
        case AW9523_PIN_0_7 : dev->chip[chip].p0_led_dim[7] = led_val;    break; 
        case AW9523_PIN_1_0 : dev->chip[chip].p1_led_dim[0] = led_val;    break; 
        case AW9523_PIN_1_1 : dev->chip[chip].p1_led_dim[1] = led_val;    break; 
        case AW9523_PIN_1_2 : dev->chip[chip].p1_led_dim[2] = led_val;    break; 
        case AW9523_PIN_1_3 : dev->chip[chip].p1_led_dim[3] = led_val;    break; 
        case AW9523_PIN_1_4 : dev->chip[chip].p1_led_dim[4] = led_val;    break; 
        case AW9523_PIN_1_5 : dev->chip[chip].p1_led_dim[5] = led_val;    break; 
        case AW9523_PIN_1_6 : dev->chip[chip].p1_led_dim[6] = led_val;    break; 
        case AW9523_PIN_1_7 : dev->chip[chip].p1_led_dim[7] = led_val;    break; 
        default:  break;
      }
    }
  }
  
  return HAL_OK;
}


//将所有IO设置为高阻模式
static HAL_StatusTypeDef aw9523_SetAllIo_Hiz(aw9523_dev* dev, uint32_t chip){
	HAL_StatusTypeDef res = HAL_OK;
	uint8_t data[16] = {0};	//全0的数据
	
	uint8_t chip_addr = AW9523_ADDRESS;	
	if(dev->chip[chip].a0)		chip_addr |= 0x02;
	if(dev->chip[chip].a1)		chip_addr |= 0x04;
	
	//配置所有IO为LED模式
	res |= aw9523_set_reg(dev->hi2c, chip_addr, LED_MODE_SWITCH_P0, data, 2 );
	//配置所有IO的LED输出为OFF
	res |= aw9523_set_reg(dev->hi2c, chip_addr, DIM0, data, 16 );

	return res;
	
}




//连续设置寄存器
#define AW9523_I2C_BUFF_SIZE 32
static HAL_StatusTypeDef aw9523_set_reg(I2C_HandleTypeDef *hi2c, uint16_t DevAddress, uint8_t RegAddress, uint8_t *pData, uint16_t Size){
	uint8_t buf[AW9523_I2C_BUFF_SIZE] = {RegAddress};
	if(Size > AW9523_I2C_BUFF_SIZE-1)
		return HAL_ERROR;
	
	for(uint32_t i=0; i<Size; i++)
		buf[i+1] = pData[i];
	return aw9523_i2c_transmit(hi2c, DevAddress, buf, Size+1);
}


//i2c发送接口
static inline HAL_StatusTypeDef aw9523_i2c_transmit(I2C_HandleTypeDef *hi2c, uint16_t DevAddress, uint8_t *pData, uint16_t Size){
	return HAL_I2C_Master_Transmit(hi2c, DevAddress, pData, Size, I2C_TIMOUT_MS);
}









