#ifndef UART_HH
#define UART_HH

#include "WorkQueue.hh"
#include "PacketInQueue.hh"
#include "HW.hh"

class UART
{
private:
    static constexpr size_t rxBufferSize = 64;
    static constexpr uint32_t rxBufferMask = rxBufferSize - 1;
    UART_HandleTypeDef* huart;
    uint32_t baudrate;
    uint8_t rxBuffer[rxBufferSize];
    uint32_t rxReadIndex = 0;
    DelayedWork transferTimeoutWork;
    uint32_t transferTimeoutMs;

    static void transferTimeoutCallback(DelayedWork* work);
    void transferTimeoutCallback();
    void consumeBytes();

public:
    PacketInQueue rxQueue;

    UART(UART_HandleTypeDef* huart);

    void init();
    void receivedBuffer();
    static UART* instanceForHandle(UART_HandleTypeDef* huart);

};

#endif // UART_HH
