#ifndef BSP_LED_H
#define BSP_LED_H

#include "main.h"

/* 状态灯：野火 F103-MINI 板 LED1=PC3、LED2=PC4，共阳接法（低电平点亮）
 * 若 CubeMX 里没把 PC3 的用户标签设为 LED，这里给默认值兜底；
 * 工程里生成过 LED_Pin 的话自动用工程里的定义。
 */
#ifndef LED_Pin
#define LED_Pin         GPIO_PIN_3
#define LED_GPIO_Port   GPIOC
#endif

#define LED_ON()        HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_RESET)
#define LED_OFF()       HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_SET)
#define LED_TOGGLE()    HAL_GPIO_TogglePin(LED_GPIO_Port, LED_Pin)

/* 串口句柄：部分 CubeMX 版本不在 main.h 里声明 extern，这里统一兜底
 * （与 main.h 里的重复 extern 声明不冲突） */
extern UART_HandleTypeDef huart1;

#endif /* BSP_LED_H */
