#ifndef PROG_H
#define PROG_H

#include "uart.h"

void packetReceived(struct PortState *port, uint8_t *data, size_t length);

#endif // PROG_H
