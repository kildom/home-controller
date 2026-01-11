#ifndef PACKETQUEUE_HH
#define PACKETQUEUE_HH

#include <stdint.h>
#include <stdlib.h>
#include <stddef.h>


class PacketInQueue
{
public:
    static constexpr int NO_PACKET = -1;
    static constexpr int END_MARKER = -2;

private:
    static constexpr int INVALID = -3;

    static constexpr size_t SIZE = 1024;
    static constexpr size_t MASK = (SIZE - 1);
    uint8_t buffer[SIZE];
    volatile size_t writePos;
    volatile size_t readPos;

public:
    size_t overrunBytes; // This is only for statistics, so write races are acceptable (no need for volatile or atomic).
    size_t invalidPackets;

    PacketInQueue();

    /** Write raw data to the packet queue. Called from IRQ. Returns true if consumer need to be notified. */
    bool write(const uint8_t *data, size_t size);

    /** Peek single packet. Returns packet size or negative status code. The packet remains in the queue. */
    int peek(uint8_t* &data);

    /** Remove packet recently peeked from the queue. */
    void drop(uint8_t* data, int size);

private:
    int peekInner(uint8_t* &data);
};


#endif // PACKETQUEUE_HH
