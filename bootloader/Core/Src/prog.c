#include <stdint.h>
#include <stdbool.h>

#include "stm32c0xx.h"
#include "stm32c0xx_hal_def.h"
#include "stm32c0xx_hal_flash.h"

#include "uart.h"
#include "prog.h"


#ifdef STM32C011xx
#define FLASH_PAGE_SIZE_LOG2 11 // 2K
#define FLASH_WRITE_SIZE_LOG2 3 // 8 bytes
#define DEVICE_MODEL ModelSTM32C011xx
#else
#error Unknown Device
#endif


typedef struct DeviceInfo {
	uint32_t loadAddress;
	uint16_t totalPages;
	uint8_t pageSizeLog2;
	uint8_t writeSizeLog2;
	uint8_t deviceModel;
} DeviceInfo;

static const DeviceInfo deviceInfo = {
		.loadAddress = FLASH_BASE + 4 * 1024,
		.totalPages = (FLASH_SIZE_DEFAULT - 4 * 1024) / (1 << FLASH_PAGE_SIZE_LOG2),
		.pageSizeLog2 = FLASH_PAGE_SIZE_LOG2,
		.writeSizeLog2 = FLASH_WRITE_SIZE_LOG2,
		.deviceModel = DEVICE_MODEL,
};


enum {
    CMD_INIT = 0,
    CMD_ERASE = 1,
    CMD_WRITE = 2,
    CMD_READ = 3,
    CMD_RESET = 4,
    CMD_PING = 5,
    CMD_LAST_ID = CMD_PING,
};

static void waitFlashReady()
{
    // 1. Check that no flash memory operation is ongoing by checking the BSY1 bit of the FLASH status register (FLASH_SR).
    while (READ_BIT(FLASH->SR, FLASH_SR_BSY1));
    // 2. Check and clear all error programming flags due to a previous programming. If not, PGSERR is set.
    FLASH->SR = FLASH_FLAG_SR_ERROR;
    // 3. Check that the CFGBSY bit of the FLASH status register (FLASH_SR) is cleared.
    while (READ_BIT(FLASH->SR, FLASH_SR_CFGBSY));
}

static void pageErase(uint32_t pageNumber)
{
    waitFlashReady();
    // 4. Set the PER bit and select the page to erase (PNB) in the FLASH control register (FLASH_CR).
    FLASH->CR = (FLASH->CR & ~FLASH_CR_PNB) | (pageNumber <<  FLASH_CR_PNB_Pos) | FLASH_CR_PER;
    // 5. Set the STRT bit of the FLASH control register (FLASH_CR).
    SET_BIT(FLASH->CR, FLASH_CR_STRT);
    // 6. Wait until the CFGBSY bit of the FLASH status register (FLASH_SR) is cleared.
    waitFlashReady();
    CLEAR_BIT(FLASH->CR, FLASH_CR_PER);    
}

static void writeData(uint32_t address, const uint8_t *data, size_t length)
{
    waitFlashReady();
    // 4. Set the PG bit of the FLASH control register (FLASH_CR).
    SET_BIT(FLASH->CR, FLASH_CR_PG);
    for (int i = 0; i < length; i += (1 << FLASH_WRITE_SIZE_LOG2))
    {
        // 5. Perform the data write operation at the desired memory address, inside main flash memory block or OTP area. Only double word (64 bits) can be programmed.
        //    a) Write a first word in an address aligned with double word
        //    b) Write the second word.
        *(volatile uint32_t *)(address + i) = getUint32(data + i);
        __ISB();
        __DMB();
        *(volatile uint32_t *)(address + i + 4) = getUint32(data + i + 4);
        __ISB();
        __DMB();
        // 6. Wait until the CFGBSY bit of the FLASH status register (FLASH_SR) is cleared.
        waitFlashReady();
        // 7. Check that the EOP flag in the FLASH status register (FLASH_SR) is set (programming operation succeeded), and clear it by software.
        FLASH->SR = FLASH_SR_EOP;
    }
    // 8. Clear the PG bit of the FLASH control register (FLASH_CR) if there no more programming request anymore.
    CLEAR_BIT(FLASH->CR, FLASH_CR_PG);
}

static void readData(struct PortState *port, uint32_t address, uint8_t length)
{
    uartTxAppend(port, (const void *)address, length);
}

static void progInit(struct PortState *port)
{
    uartTxAppend(port, &deviceInfo, sizeof(deviceInfo));
    HAL_FLASH_Unlock();
}

static void executeCommand(struct PortState *port, uint8_t cmd, const uint8_t *data, size_t length)
{
    switch (cmd) {
        case CMD_INIT:
            progInit(port);
            return;
        case CMD_ERASE:
            pageErase(getUint32(data));
            return;
        case CMD_WRITE:
            writeData(getUint32(data), data + 4, length - 4);
            return;
        case CMD_READ:
            readData(port, getUint32(data), data[4]);
            return;
        case CMD_RESET:
            NVIC_SystemReset();
            return;
        case CMD_PING:
        default:
            return;
    }
}

static uint32_t lastValidPacketCounter = 0xFFFFFFFF;


void packetReceived(struct PortState *port, uint8_t *data, size_t length)
{
    uint8_t cmd = data[0];
    uint32_t packetCounter = getUint32(&data[1]);

    if ((packetCounter == 0xFFFFFFFF || packetCounter == lastValidPacketCounter + 1) && cmd <= CMD_LAST_ID) {
        lastValidPacketCounter++;
        uartTxPrepare(port);
        uartTxAppend(port, &lastValidPacketCounter, sizeof(lastValidPacketCounter));
        executeCommand(port, cmd, &data[1 + sizeof(packetCounter)], length - 1 - sizeof(packetCounter));
        uartTxFinalize(port);
    }
}
