#ifndef CRC_HH
#define CRC_HH

#include <stddef.h>
#include <stdint.h>

class CRC32 {
public:
    static uint32_t calculate(const void* data, size_t size);
};

#endif // CRC_HH