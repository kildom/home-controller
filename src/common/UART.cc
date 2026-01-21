#include "Utils.hh"
#include "IRQ.hh"
#include "UART.hh"

// #include <stdio.h>
#include <string.h>

static UART* instances[2] = { nullptr, nullptr };

extern "C"
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    UART::instanceForHandle(huart)->receivedBuffer();
}

extern "C"
void HAL_UART_RxHalfCpltCallback(UART_HandleTypeDef *huart)
{
    UART::instanceForHandle(huart)->receivedBuffer();
}

extern "C"
void MyRXCallback(UART_HandleTypeDef *huart)
{
    UART::instanceForHandle(huart)->receivedBuffer();
}

UART* UART::instanceForHandle(UART_HandleTypeDef* huart) {
    if (instances[0] && instances[0]->huart == huart) {
        return instances[0];
    } else {
        return instances[1];
    }
}

void UART::transferTimeoutCallback(DelayedWork *work)
{
    CONTAINER_OF(work, UART, transferTimeoutWork)->transferTimeoutCallback();
}

void UART::transferTimeoutCallback()
{
    //myprintf("UART timeout\n");
    consumeBytes();
    __HAL_UART_ENABLE_IT(huart, UART_IT_RXNE);
    //myprintf("Enable IRQ\n");
}

extern "C" int uartIRQCalled;

int totalBytesReceived = 0;
int totalBytesReceivedReported = 0;

void UART::consumeBytes()
{
    bool notify = false;
    bool received = false;
    char tmp[65];

    REENTRY_GUARD_BEGIN;

    do {
        uint32_t bytesRemaining = huart->hdmarx->Instance->CNDTR & 0xFFFF;
        uint32_t rxWriteIndex = (rxBufferSize - bytesRemaining) & rxBufferMask;
        if (rxWriteIndex == rxReadIndex) {
            break;
        }
        received = true;
        if (rxReadIndex > rxWriteIndex) {
            memcpy(tmp, &rxBuffer[rxReadIndex], rxBufferSize - rxReadIndex);
            tmp[rxBufferSize - rxReadIndex] = '\0';
            //myprintf("tail %d -> %d = %d '%s' %d\n", rxReadIndex, rxWriteIndex, rxBufferSize - rxReadIndex, tmp, uartIRQCalled);
            notify = rxQueue.write(&rxBuffer[rxReadIndex], rxBufferSize - rxReadIndex) || notify;
            totalBytesReceived += rxBufferSize - rxReadIndex;
            rxReadIndex = 0;
        }
        if (rxReadIndex < rxWriteIndex) {
            memcpy(tmp, &rxBuffer[rxReadIndex], rxWriteIndex - rxReadIndex);
            tmp[rxWriteIndex - rxReadIndex] = '\0';
            //myprintf("buff %d -> %d = %d '%s' %d\n", rxReadIndex, rxWriteIndex, rxWriteIndex - rxReadIndex, tmp, uartIRQCalled);
            notify = rxQueue.write(&rxBuffer[rxReadIndex], rxWriteIndex - rxReadIndex) || notify;
            totalBytesReceived += rxWriteIndex - rxReadIndex;
            rxReadIndex = rxWriteIndex;
        }
    } while (true);//▲▶▼◀▲▶▼◀▲▶▼◀▲▶▼◀▲▶▼◀▲▶▼◀▲▶▼◀▲▶▼◀▲▶▼◀▲▶▼◀▲▶▼◀▲▶▼◀▲▶▼◀▲▶▼◀▲▶▼◀▲▶▼◀▲▶▼◀▲▶▼◀▲▶▼◀▲▶▼◀▲▶▼◀▲▶▼◀

    REENTRY_GUARD_END;

    if (totalBytesReceived - totalBytesReceivedReported >= 1024) {
        myprintf("UART received total %d.%03d KB\n", totalBytesReceived / 1024, (totalBytesReceived % 1024) * 1000 / 1024);
        totalBytesReceivedReported = totalBytesReceived;
    }

    if (notify) {
        //myprintf("Received %d -> %d\n", rxQueue.readPos, rxQueue.writePos);
        //receiveWork.run();
    }

    if (received) {
        // auto urgency = getUrgencyBasedOnTxQueueContent(); // 0 = no urgency, 100 = max urgency
        // lineFreeWork.run(LINE_FREE_MARGIN_MS + (LINE_FREE_DELAY_MS + Rand::get(DELAY_JITTER_MS)) * (128 - urgency) / 128);
    }
}

UART::UART(UART_HandleTypeDef* huart)
    : huart(huart), transferTimeoutWork(transferTimeoutCallback)
{
}

void UART::init()
{
    if (instances[0] == nullptr) {
        instances[0] = this;
    } else {
        instances[1] = this;
    }
    auto bytesPerSec = huart->Init.BaudRate / 10;
    transferTimeoutMs = ((rxBufferSize * 1000 + bytesPerSec - 1) * 4) / (5 * bytesPerSec) + 1;
    if (transferTimeoutMs < 2) {
        transferTimeoutMs = 2;
    }
    auto res = HAL_UART_Receive_DMA(huart, rxBuffer, sizeof(rxBuffer));
    myprintf("UART at %lu bps, timeout %lu ms, res %d\n", huart->Init.BaudRate, transferTimeoutMs, res);
    ASSERT(res == HAL_OK);
    __HAL_UART_ENABLE_IT(huart, UART_IT_RXNE);
}

void UART::receivedBuffer()
{
    consumeBytes();
    transferTimeoutWork.run(transferTimeoutMs);
    __HAL_UART_DISABLE_IT(huart, UART_IT_RXNE);
    //myprintf("Disable IRQ\n");
}
