#ifndef UART_H
#define UART_H


#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include "stm32c0xx_ll_crc.h"


void uartInit();
void uartPoll();
void uartAppend(const void* data, size_t length);

#endif // UART_H