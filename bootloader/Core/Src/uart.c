#include "main.h"
#include <stdint.h>
#include <stdbool.h>

#include "stm32c0xx.h"
#include "stm32c0xx_hal_def.h"
#include "stm32c0xx_hal_flash.h"
#include "stm32c0xx_ll_crc.h"


#pragma region Device configuration


#ifdef STM32C011xx

#define DEVICE_MODEL ModelSTM32C011xx

#else

#error Unknown Device

#endif


#define WRITE_SIZE 8


enum Model {
	ModelSTM32C011xx = 1,
};


#pragma endregion


#pragma region Declarations and initialization


#if !defined(NUM_PORTS) || NUM_PORTS < 1 || NUM_PORTS > 4
#error NUM_PORTS must be defined as 1, 2, 3 or 4
#endif

enum {
    BOOT_TYPE_PASS = 0,
    BOOT_TYPE_PASSED = 1,
    BOOT_TYPE_RESPONSE = 2,
};

#define BUFFER_SIZE (256 + 32) // TODO: May be smaller
#define UUID_SIZE 13 // Includes model byte
#define NETWORK_HEADER_SIZE 7
#define ERROR_DATA -2
#define NO_DATA -1
#define ESC 0xAA
#define END 0xFF

typedef struct PortState
{
    USART_TypeDef *uart;
    uint32_t rxIndex;
    uint8_t rxBuffer[BUFFER_SIZE];
    bool headerReceived;
} PortState;

typedef struct DeviceInfo {
	uint32_t loadAddress;
	uint16_t totalPages;
	uint8_t pageSizeLog2;
	uint8_t writeSizeLog2;
	uint8_t deviceModel;
} DeviceInfo;

__attribute__((aligned(4)))
static uint8_t header[NETWORK_HEADER_SIZE + UUID_SIZE];
static PortState portStates[NUM_PORTS];
static uint8_t txBuffer[BUFFER_SIZE];
static int txSize = 0;
static DeviceInfo deviceInfo;

static void packetReceived(struct PortState *port, uint8_t *data, size_t length);

#pragma endregion


#pragma region Utility functions


static void copyBytes(void *dest, const void *src, size_t n)
{
    uint8_t *d = (uint8_t *)dest;
    const uint8_t *s = (const uint8_t *)src;
    const uint8_t *end = (const uint8_t *)src + n;
    while (s < end)
    {
        *d++ = *s++;
    }
}


static uint32_t getUint32(const void *data)
{
    uint32_t value;
    copyBytes(&value, data, sizeof(value));
    return value;
}


static inline void setUint32(void *data, uint32_t value)
{
    copyBytes(data, &value, sizeof(value));
}


static uint32_t calcCrc(const void *data, size_t length)
{
    const uint8_t *ptr = data;
    const uint8_t *end = data + length;

    LL_CRC_ResetCRCCalculationUnit(CRC);

    while (ptr < end)
    {
        LL_CRC_FeedData8(CRC, *ptr++);
    }

    return LL_CRC_ReadData32(CRC);
}


#pragma endregion


#pragma region USART input output


static void txSend(USART_TypeDef *uart, uint8_t byte)
{
    // Wait until Tx FIFO is not full using TXE flag in USART_ISR register
    while (!LL_USART_IsActiveFlag_TXE(uart));
    LL_USART_TransmitData8(uart, byte);
}

static int rxReceive(USART_TypeDef *uart)
{
    // Use RXNE flag in USART_ISR register to check if data is received and can be read from RDR register
    if (LL_USART_IsActiveFlag_RXNE(uart))
    {
        // First check framing error using FE flag in USART_ISR register and clear it by setting FECF flag.
        if (LL_USART_IsActiveFlag_FE(uart))
        {
            LL_USART_ClearFlag_FE(uart);
            LL_USART_ReceiveData8(uart);
            return ERROR_DATA;
        }
        return LL_USART_ReceiveData8(uart);
    }
    else
    {
        // No data received
        return NO_DATA;
    }
}

static void txSendBuffer(USART_TypeDef *uart, const void *data, size_t length)
{
    const uint8_t *ptr = data;
    const uint8_t *end = ptr + length;
    while (ptr < end)
    {
        txSend(uart, *ptr++);
    }
}


static int inline log2Aligned(uint32_t value)
{
    return 31 - __builtin_clz(value);
}


#pragma endregion


#pragma region Initialization


static void init()
{
    header[0] = ESC;
    header[1] = 0x00; // Mask
    header[2] = 0x21; // flags: bootloader protocol, one destination node
    header[3] = 0x00; // src is always 0 if sending, ignored if receiving
    header[4] = 0x00; // dst is always 0
    header[5] = BOOT_TYPE_PASSED;
    header[6] = UUID_SIZE;
    header[7] = DEVICE_MODEL;
    *(uint32_t*)&header[8] = HAL_GetUIDw0();
    *(uint32_t*)&header[12] = HAL_GetUIDw1();
    *(uint32_t*)&header[16] = HAL_GetUIDw2();

    extern uint8_t _sidata;
    extern uint8_t _sdata;
    extern uint8_t _edata;
    uint32_t pageSizeLog2 = log2Aligned(FLASH_PAGE_SIZE);
    uint32_t programCodeEnd = (uint32_t)&_sidata + ((uint32_t)&_edata - (uint32_t)&_sdata);
    uint32_t bootloaderEndPage = (programCodeEnd + FLASH_PAGE_SIZE - 1) >> pageSizeLog2;
    uint32_t flashEndPage = (FLASH_BASE + FLASH_SIZE) >> pageSizeLog2;

    deviceInfo.deviceModel = DEVICE_MODEL;
    deviceInfo.pageSizeLog2 = pageSizeLog2;
    deviceInfo.loadAddress = bootloaderEndPage << pageSizeLog2;
    deviceInfo.totalPages = flashEndPage - bootloaderEndPage;
    deviceInfo.writeSizeLog2 = log2Aligned(WRITE_SIZE);
}


#pragma endregion


#pragma region RX Receiving


static void switchToHeader(PortState *port)
{
    port->headerReceived = false;
    port->rxIndex = 0;
    port->rxBuffer[1] = 0; // Reset mask
}


static void switchToContent(PortState *port)
{
    port->headerReceived = true;
}


static void receiveHeader(PortState *port)
{
    int byte;

    byte = rxReceive(port->uart);

    if (byte == NO_DATA)
    {
        // No data - nothing to do
    }
    else if (byte == ERROR_DATA)
    {
        switchToHeader(port);
    }
    else if ((byte ^ port->rxBuffer[1]) == header[port->rxIndex] || port->rxIndex == 1 || port->rxIndex == 3)
    {
        port->rxBuffer[port->rxIndex] = byte;
        port->rxIndex++;
        if (port->rxIndex == sizeof(header))
        {
            switchToContent(port);
        }
    }
    else
    {
        switchToHeader(port);
    }
}


static bool validatePacket(const uint8_t *data, size_t length)
{
    if (length < 4) {
        return false;
    }

    uint32_t calculatedCrc = calcCrc(&data[2], length - 2 - 4);
    uint32_t receivedCrc = getUint32(data + length - 4);

    return calculatedCrc == receivedCrc;
}


static void receiveContent(PortState *port)
{
    int byte;

    byte = rxReceive(port->uart);

    if (byte == NO_DATA)
    {
        // No data - nothing to do
    }
    else if (byte == ERROR_DATA)
    {
        switchToHeader(port);
    }
    else if (byte == ESC)
    {
        txSendBuffer(port->uart, txBuffer, txSize);
        if (validatePacket(port->rxBuffer, port->rxIndex))
        {
            packetReceived(port, port->rxBuffer, port->rxIndex);
        }
        switchToHeader(port);
    }
    else
    {
        byte ^= port->rxBuffer[1]; // Unmask byte
        if (port->rxIndex < sizeof(port->rxBuffer))
        {
            port->rxBuffer[port->rxIndex++] = byte;
        }
        else
        {
            switchToHeader(port);
        }
    }
}


void uartPoll(USART_TypeDef *uart0, USART_TypeDef *uart1, USART_TypeDef *uart2, USART_TypeDef *uart3)
{
    static bool initialized = false;
    if (!initialized) {
        portStates[0].uart = uart0;
        #if NUM_PORTS > 1
        portStates[1].uart = uart1;
        #endif
        #if NUM_PORTS > 2
        portStates[2].uart = uart2;
        #endif
        #if NUM_PORTS > 3
        portStates[3].uart = uart3;
        #endif
        init();
        initialized = true;
    }
    for (int i = 0; i < sizeof(portStates) / sizeof(portStates[0]); i++)
    {
        if (portStates[i].headerReceived)
        {
            receiveContent(&portStates[i]);
        }
        else
        {
            receiveHeader(&portStates[i]);
        }
    }
}


#pragma endregion


#pragma region TX Transmitting


static void txPrepare(PortState *port)
{
    copyBytes(txBuffer, header, sizeof(header));
    // txBuffer[0] = ESC unchanged
    // txBuffer[1] = mask tbd
    // txBuffer[2] = flags unchanged
    // txBuffer[3] = src 0 (proxy address)
    txBuffer[4] = port->rxBuffer[3]; // dst = programmer address
    txBuffer[5] = BOOT_TYPE_RESPONSE; // boot  = response
    // txBuffer[6] = UID length unchanged
    // txBuffer[7...] = UID unchanged
    txSize = sizeof(header);
}


static void txAppend(struct PortState *port, const void *data, size_t length)
{
    copyBytes(&txBuffer[txSize], data, length);
    txSize += length;
}


static uint8_t applyMask(uint8_t *data, size_t length)
{
    uint8_t mask = 0;
    uint32_t map[256 / 32] = {0};

    uint8_t *ptr = data;
    uint8_t *end = ptr + length;
    while (ptr < end) {
        uint8_t byte = *ptr;
        map[byte / 32] |= (1 << (byte % 32));
        ptr++;
    }

    // Do content scrambling is needed
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
        end = ptr + length;
        while (ptr < end) {
            *ptr ^= mask;
            ptr++;
        }
    }

    return mask;
}


static void txFinalize()
{
    uint32_t crc = calcCrc(&txBuffer[2], txSize - 2);
    copyBytes(&txBuffer[txSize], &crc, sizeof(crc));
    txSize += sizeof(crc);
    txBuffer[1] = applyMask(&txBuffer[2], txSize - 2);
    txBuffer[txSize++] = ESC;
    txBuffer[txSize++] = END;
}


#pragma endregion


#pragma region FLASH programming


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
    for (int i = 0; i < length; i += WRITE_SIZE)
    {
        // 5. Perform the data write operation at the desired memory address, inside main flash memory block or OTP area. Only double word (64 bits) can be programmed.
        //    a) Write a first word in an address aligned with double word
        //    b) Write the second word.
#if WRITE_SIZE != 4 && WRITE_SIZE != 8
#error Unsupported WRITE_SIZE
#endif
        *(volatile uint32_t *)(address + i) = getUint32(data + i);
        __ISB();
        __DMB();
#if WRITE_SIZE == 8
        *(volatile uint32_t *)(address + i + 4) = getUint32(data + i + 4);
        __ISB();
        __DMB();
#endif
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
    txAppend(port, (const void *)address, length);
}

static void progInit(struct PortState *port)
{
    txAppend(port, &deviceInfo, sizeof(deviceInfo));
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

static uint32_t lastValidCommandCounter = 0xFFFFFFFF;


static void packetReceived(struct PortState *port, uint8_t *data, size_t length)
{
    uint8_t cmd = data[0];
    uint32_t commandCounter = getUint32(&data[1]);

    if ((commandCounter == 0xFFFFFFFF || commandCounter == lastValidCommandCounter + 1) && cmd <= CMD_LAST_ID) {
        lastValidCommandCounter++;
        txPrepare(port);
        txAppend(port, &lastValidCommandCounter, sizeof(lastValidCommandCounter));
        executeCommand(port, cmd, &data[1 + sizeof(commandCounter)], length - 1 - sizeof(commandCounter));
        txFinalize(port);
    }
}


#pragma endregion


#pragma region Boot selection


__attribute__((naked))
void startApplication(uint32_t stackPointer, uint32_t resetHandler)
{
    __ASM volatile (
        "MSR msp, r0\n" // Set master stack pointer
        "MOV sp, r0\n"  // Set stack pointer
        "BX r1\n"       // Jump to reset handler
    );
}


void bootSelect()
{
    extern uint8_t _sidata;
    extern uint8_t _sdata;
    extern uint8_t _edata;
    extern uint64_t _estack;

    if (_estack == 0x6015A81F165BBB3EuLL) {
        return;
    }

    uint32_t programCodeEnd = (uint32_t)&_sidata + ((uint32_t)&_edata - (uint32_t)&_sdata);
    uint32_t bootloaderEnd = (programCodeEnd + FLASH_PAGE_SIZE - 1) & ~(FLASH_PAGE_SIZE - 1);
    uint32_t* vectorTable = (uint32_t *)bootloaderEnd;

    if (vectorTable[0] <= SRAM_BASE || vectorTable[0] > (uint32_t)&_estack + sizeof(_estack)) {
        return;
    }

    if (vectorTable[1] <= bootloaderEnd || vectorTable[1] > FLASH_BASE + FLASH_SIZE) {
        return;
    }

    SCB->VTOR = bootloaderEnd;
    startApplication(vectorTable[0], vectorTable[1]);
}


#pragma endregion


/*
 * Programmer -> Proxy
 *      ESC
 *      mask
 *      flags = 0x21 (bootloader protocol, one destination node)
 *      src   = programmer address
 *      dst   = proxy address
 *      boot  = type: pass, port: N
 *      data...
 *      CRC32
 *      ESC
 *      END
 * 
 * Proxy -> destination port
 *      ESC
 *      mask
 *      flags = 0x21 (bootloader protocol, one destination node)
 *      src   = programmer address
 *      dst   = 0 (unknown device)
 *      boot  = type: passed
 *      UID length
 *      UID
 *      cmd
 *      cmd counter
 *      cmd arguments...
 *      CRC32
 *      ESC
 *
 *      (missing END)
 *
 *      ESC               # Start of bootload transmission
 *      mask
 *      flags = 0x21 (bootloader protocol, one destination node)
 *      src   = 0 (proxy address)
 *      dst   = programmer address
 *      boot  = response
 *      UID length
 *      UID
 *      last valid cmd counter
 *      last valid cmd result ...
 *      CRC32
 *      ESC
 *      END
 * 
 */
