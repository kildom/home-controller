#ifndef CRC_HH
#define CRC_HH

#include <stddef.h>
#include <stdint.h>

class CRC {
public:
    static uint32_t calculate(const void* data, size_t size)
    {
        uint32_t crc = 0;
        uint8_t* ptr = (uint8_t*)data;
        for (size_t i = 0; i < size; i++) {
            crc += (uint32_t)ptr[i];
        }
        return crc;
    }
};

#endif // CRC_HH