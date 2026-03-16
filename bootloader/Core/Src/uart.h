#ifndef UART_H
#define UART_H


#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include "stm32c0xx_ll_crc.h"

struct PortState;

void uartInit();
void uartPoll();
void uartTxPrepare(struct PortState *port);
void uartTxAppend(struct PortState *port, const void* data, size_t length);
void uartTxFinalize(struct PortState *port);

#endif // UART_H