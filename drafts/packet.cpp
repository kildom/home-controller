
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "crc.hh"


constexpr uint8_t ESC = 0xAA; // Escape character
constexpr uint8_t END = 0xFF; // End character
constexpr size_t CRC_SIZE = 4; // 4 bytes for CRC32
constexpr size_t MAX_DATA_SIZE = 256 - 1 - 1 - 1 - CRC_SIZE; // 256 (total symbols) - 1 (reserved for <ESC>) - 1 (reserved for <END>) - 1 (needed for scrambling) - 4 (needed for CRC)

struct Packet {
    uint8_t esc;
    uint8_t mask;
    uint8_t data[MAX_DATA_SIZE + CRC_SIZE + 2];
    size_t dataSize;
    void postprocess();
    bool descrambleAndVerify();
};

bool Packet::descrambleAndVerify()
{
    // Check data size
    if (dataSize > MAX_DATA_SIZE) {
        return false;
    }
    // Descramble if needed
    if (mask != 0) {
        uint8_t *ptr = data;
        uint8_t *end = ptr + dataSize + CRC_SIZE;
        while (ptr < end) {
            *ptr ^= mask;
            ptr++;
        }
    }
    // Calculate CRC
    uint8_t *ptr = data;
    uint8_t *end = ptr + dataSize;
    uint32_t crc = ~(uint32_t)0;
    while (ptr < end) {
        uint8_t byte = *ptr;
        crc = crc32_tab[(crc ^ byte) & 0xff] ^ (crc >> 8);
        ptr++;
    }
    crc = ~crc;
    // Compare with received CRC
    return memcmp(&crc, end, CRC_SIZE) == 0;
}


void Packet::postprocess()
{
    esc = ESC;
    mask = 0;
    uint32_t map[256 / 32] = {0};

    // TODO: assert(dataSize <= MAX_DATA_SIZE);

    // Calculate CRC and build byte usage map
    uint8_t *ptr = data;
    uint8_t *end = ptr + dataSize;
    uint32_t crc = ~(uint32_t)0;
    while (ptr < end) {
        uint8_t byte = *ptr;
        crc = crc32_tab[(crc ^ byte) & 0xff] ^ (crc >> 8);
        map[byte / 32] |= (1 << (byte % 32));
        ptr++;
    }
    crc = ~crc;

    // Append CRC to data and update byte usage map
    const uint8_t *crcPtr = (const uint8_t *)&crc;
    end = ptr + CRC_SIZE;
    while (ptr < end) {
        uint8_t byte = *crcPtr++;
        map[byte / 32] |= (1 << (byte % 32));
        *ptr++ = byte;
    }

    // Append ending marker
    *ptr++ = ESC;
    *ptr++ = END;

    // Do content scrambling if needed
    if (map[ESC / 32] & (1 << (ESC % 32))) {
        // Avoid using ESC or END as mask
        map[(ESC ^ ESC) / 32] |= (1 << ((ESC ^ ESC) % 32));
        map[(END ^ ESC) / 32] |= (1 << ((END ^ ESC) % 32));
        // Find first not fully used word in map
        uint32_t* ptrMap = map;
        size_t wordIndex = 0;
        while (*ptrMap == 0xFFFFFFFF) {
            ptrMap++;
            wordIndex += 32;
        }
        // Find first free bit in the word
        size_t bitIndex = 0;
        while ((*ptrMap & (1 << bitIndex)) != 0) {
            bitIndex++;
        }
        mask = (uint8_t)(wordIndex + bitIndex);
        // Apply mask
        ptr = data;
        end = ptr + dataSize + CRC_SIZE;
        while (ptr < end) {
            *ptr ^= mask;
            ptr++;
        }
    }
}
