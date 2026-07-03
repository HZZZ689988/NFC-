#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32f4xx_hal.h"

void Error_Handler(void);

#define SPI1_CS_Pin        GPIO_PIN_4
#define SPI1_CS_GPIO_Port  GPIOC
#define BL_LED_Pin         GPIO_PIN_8
#define BL_LED_GPIO_Port   GPIOE

#ifdef __cplusplus
}
#endif

#endif
