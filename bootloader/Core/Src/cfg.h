#ifndef CFG_H
#define CFG_H

#include <stdint.h>

#ifndef PORT0_USART
#error Define USART peripheral instances for all used USART ports
#endif

#ifdef STM32C011xx

#define FLASH_PAGE_SIZE_LOG2 11 // 2K
#define FLASH_WRITE_SIZE_LOG2 3 // 8 bytes
#define DEVICE_MODEL ModelSTM32C011xx

#else

#error Unknown Device

#endif


enum Model {
	ModelSTM32C011xx = 1,
};

#endif // CFG_H
