#ifndef PROG_H
#define PROG_H

enum Model {
	ModelSTM32C011xx = 1,
};

void packetReceived(uint8_t *data, size_t length);

#endif // PROG_H
