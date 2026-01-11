
#include "gtest/gtest.h"
#include "gmock/gmock.h"

#include "test_common.hh"
#include "stub_CMSIS.hh"
#include "stub_CRC.hh"

BEGIN_ISOLATED_NAMESPACE

#define private public
#define protected public

#include "src/PacketInQueue.hh"
#include "src/PacketInQueue.cc"

const uint8_t data[] = { 0x01, 0x02, 0x03, 0x04, 0x05 };

TEST(PacketInQueue, simpleWrite) {
    PacketInQueue queue;
    EXPECT_FALSE(queue.write(data, sizeof(data)));
    EXPECT_EQ(queue.writePos, sizeof(data));
    EXPECT_EQ(queue.readPos, 0);
    EXPECT_EQ(queue.overrunBytes, 0);
    EXPECT_EQ(queue.invalidPackets, 0);
    EXPECT_EQ(memcmp(queue.buffer, data, sizeof(data)), 0);
}

TEST(PacketInQueue, wrappedWrite) {
    PacketInQueue queue;
    queue.writePos = PacketInQueue::SIZE - 2;
    queue.readPos = PacketInQueue::SIZE / 2;
    EXPECT_FALSE(queue.write(data, sizeof(data)));
    EXPECT_EQ(queue.writePos, sizeof(data) - 2);
    EXPECT_EQ(queue.readPos, PacketInQueue::SIZE / 2);
    EXPECT_EQ(memcmp(&queue.buffer[PacketInQueue::SIZE - 2], data, 2), 0);
    EXPECT_EQ(memcmp(queue.buffer, &data[2], sizeof(data) - 2), 0);
}

TEST(PacketInQueue, overrunWrite) {
    PacketInQueue queue;
    queue.writePos = 1;
    queue.readPos = 5;
    EXPECT_TRUE(queue.write(data, sizeof(data)));
    EXPECT_EQ(queue.writePos, 4);
    EXPECT_EQ(queue.readPos, 5);
    EXPECT_EQ(queue.overrunBytes, (sizeof(data) - 3) | 0x80000000);
    EXPECT_EQ(queue.invalidPackets, 0);
    EXPECT_EQ(queue.buffer[2], ESC);
    EXPECT_EQ(queue.buffer[3], END);
}

TEST(PacketInQueue, packetEndWrite) {
    const uint8_t dataWithEsc[] = { ESC, 0x01, 0x02, 0x03, 0x04, 0x05, ESC };
    const uint8_t dataJustWithEnd[] = { END };
    EXPECT_FALSE(PacketInQueue().write(dataWithEsc, sizeof(dataWithEsc) - 1));
    EXPECT_TRUE(PacketInQueue().write(&dataWithEsc[1], sizeof(dataWithEsc) - 1));
    PacketInQueue queue;
    EXPECT_TRUE(queue.write(dataWithEsc, sizeof(dataWithEsc)));
    EXPECT_TRUE(queue.write(dataJustWithEnd, sizeof(dataJustWithEnd)));
    EXPECT_FALSE(queue.write(data, sizeof(data)));
    EXPECT_FALSE(queue.write(dataJustWithEnd, sizeof(dataJustWithEnd)));
}

TEST(PacketInQueue, emptyRead) {
    PacketInQueue queue;
    uint8_t* dataPtr;
    EXPECT_EQ(queue.peek(dataPtr), PacketInQueue::NO_PACKET);
    queue.writePos = 5;
    queue.readPos = 5;
    EXPECT_EQ(queue.peek(dataPtr), PacketInQueue::NO_PACKET);
}

const uint8_t samplePacket[] = { 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x0F, 0x00, 0x00, 0x00 };
const uint8_t escByte[] = { ESC };
const uint8_t endByte[] = { END };
uint8_t* dataPtr;

TEST(PacketInQueue, simpleRead) {
    PacketInQueue queue;
    queue.write(escByte, sizeof(escByte));
    queue.write(samplePacket, sizeof(samplePacket));
    queue.write(escByte, sizeof(escByte));
    int size = queue.peek(dataPtr);
    EXPECT_EQ(size, sizeof(samplePacket) - 1 - 4);
}

END_ISOLATED_NAMESPACE
