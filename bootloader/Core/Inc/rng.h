#ifndef RNG_H
#define RNG_H

#include <stdint.h>

uint16_t rngGet(void);
void rngAdd(uint32_t value);

#endif /* RNG_H */
