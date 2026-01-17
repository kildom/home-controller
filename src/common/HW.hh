#ifndef HW_HH
#define HW_HH

#ifdef STM32C011xx
#include "stm32c0xx.h"
#include "stm32c0xx_hal.h"
#else
#error "Unsupported MCU"
#endif

#include "main.h"
#include "stm32c0xx_hal_tim.h"

void myprintf(const char* fmt, ...);

#endif // HW_HH
