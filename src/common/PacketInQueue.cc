
#include "PacketInQueue.hh"
#include "CRC32.hh"
#include "IRQ.hh"
#include <cstring>


static constexpr uint8_t ESC = 0xAA; // Escape character
static constexpr uint8_t END = 0xFF; // End character

PacketInQueue::PacketInQueue() :
    writePos(0),
    readPos(0),
    overrunBytes(0),
    invalidPackets(0)
{
}

bool PacketInQueue::write(const uint8_t *data, size_t size)
{
    auto writePos = this->writePos;
    auto readPos = this->readPos;
    auto needNotify = false;
    auto dataEnd = data + size;
    while (data < dataEnd) {
        int next = (writePos + 1) & MASK;
        if (next == readPos) {
            overrunBytes = (overrunBytes + (dataEnd - data)) | 0x80000000;
            buffer[(writePos - 2) & MASK] = ESC;
            buffer[(writePos - 1) & MASK] = END;
            this->writePos = writePos;
            return true;
        }
        if ((*data == ESC || *data == END) && readPos != writePos) {
            needNotify = needNotify || *data == ESC || buffer[(writePos - 1) & MASK] == ESC;
        }
        buffer[writePos] = *data;
        writePos = next;
        data++;
    }
    this->writePos = writePos;
    return needNotify;
}

int PacketInQueue::peek(uint8_t* &data)
{
    int res;
    do {
        res = peekInner(data);
    } while (res == INVALID);
    return res;
}

int PacketInQueue::peekInner(uint8_t* &data)
{
    auto writePos = this->writePos;
    auto readPos = this->readPos;
    // Return if no data
    if (readPos == writePos) {
        return NO_PACKET;
    }
    // Trim leading garbage, wait for { ESC, mask } or { ESC, END } sequence
    auto maskPos = (readPos + 1) & MASK;
    while (maskPos != writePos && (buffer[readPos] != ESC || buffer[maskPos] == ESC)) {
        readPos = maskPos;
        maskPos = (maskPos + 1) & MASK;
    }
    // Return if no start of packet found
    if (maskPos == writePos) {
        this->readPos = readPos;
        return NO_PACKET;
    }
    // Return special END_MARKER packet if { ESC, END } sequence found
    uint32_t mask = buffer[maskPos];
    if (mask == END) {
        this->readPos = (maskPos + 1) & MASK;
        return END_MARKER;
    }
    // Find next ESC without moving the readPos
    auto dataBegin = (maskPos + 1) & MASK;
    auto dataEnd = dataBegin;
    while (dataEnd != writePos && buffer[dataEnd] != ESC) {
        dataEnd = (dataEnd + 1) & MASK;
    }
    // Return if current packet is not complete yet
    auto size = (dataEnd - dataBegin) & MASK;
    if (dataEnd == writePos) {
        if (size > 249 + 4) {
            // Packet too large, skip it
            readPos = dataEnd;
            invalidPackets = (invalidPackets + 1) | 0x80000000;
        }
        this->readPos = readPos;
        return NO_PACKET;
    }
    // Skip packets smaller than CRC-32 and bigger than maximum allowed size
    if (size < 4 || size > 249 + 4) {
        this->readPos = dataEnd;
        invalidPackets = (invalidPackets + 1) | 0x80000000;
        return INVALID;
    }
    // Extract CRC-32 from the end of the packet
    auto crcPos = (dataEnd - 4) & MASK;
    auto crc = ((uint32_t)buffer[crcPos] << 0) |
               ((uint32_t)buffer[(crcPos + 1) & MASK] << 8) |
               ((uint32_t)buffer[(crcPos + 2) & MASK] << 16) |
               ((uint32_t)buffer[(crcPos + 3) & MASK] << 24);
    // If packet is wrapped in the buffer, move it backward in the buffer to make it contiguous
    if (dataBegin > crcPos && crcPos != 0) {
        {
            IRQ::Guard guard;
            auto freeSpace = dataBegin - writePos - 1;
            if (freeSpace <= crcPos) {
                this->readPos = dataEnd;
                overrunBytes = (overrunBytes + size + 2) | 0x80000000;
                return INVALID;
            }
            readPos = dataBegin - crcPos;
            this->readPos = readPos;
        }
        std::memmove(&buffer[readPos], &buffer[dataBegin], SIZE - dataBegin);
        std::memmove(&buffer[readPos + SIZE - dataBegin], &buffer[0], crcPos);
        std::memset(&buffer[0], 0, dataEnd);
        dataBegin = readPos;
        crcPos = 0;
    }
    // Unmask packet content and CRC-32
    data = &buffer[dataBegin];
    auto contentSize = size - 4;
    if (mask != 0) {
        auto ptr = data;
        auto end = ptr + contentSize;
        while (ptr < end) {
            *ptr ^= mask;
            ptr++;
        }
        mask |= (mask << 8);
        mask |= (mask << 16);
        crc ^= mask;
    }
    // Calculate and verify CRC-32
    auto computedCrc = CRC32::calculate(data, contentSize);
    if (computedCrc != crc) {
        this->readPos = dataEnd;
        invalidPackets = (invalidPackets + 1) | 0x80000000;
        return INVALID;
    }

    return contentSize;
}

void PacketInQueue::drop(uint8_t* data, int size)
{
    if (size < 0) {
        return;
    }
    uint32_t dataBegin = data - buffer;
    auto dataEnd = (dataBegin + size + 4) & MASK;
    this->readPos = dataEnd;
}
