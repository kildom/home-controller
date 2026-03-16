#include "main.h"
#include "uart.h"
#include "prog.h"
#include "cfg.h"


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


#pragma endregion

#define BUFFER_SIZE 256 // TODO: May be smaller

#define UUID_SIZE 13

static const int ERROR_DATA = -2;
static const int NO_DATA = -1;
static const uint8_t NETWORK_ESC = 0xAA;
static const uint8_t NETWORK_END = 0xFF;

typedef struct PortState
{
    USART_TypeDef *uart;
    uint32_t rxIndex;
    bool headerReceived;
    uint8_t rxBuffer[BUFFER_SIZE];
} PortState;

static const int NUM_PORTS = 0
#if defined(PORT0_USART)
    + 1
#endif
#if defined(PORT1_USART)
    + 1
#endif
#if defined(PORT2_USART)
    + 1
#endif
#if defined(PORT3_USART)
    + 1
#endif
;

static PortState portStates[NUM_PORTS];
__attribute__((aligned(4)))
static uint8_t header[7 + UUID_SIZE];
static uint8_t txBuffer[BUFFER_SIZE];
static int txSize = 0;

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

enum {
    BOOT_TYPE_PASS = 0,
    BOOT_TYPE_PASSED = 1,
    BOOT_TYPE_RESPONSE = 2,
};



void uartInit()
{
#if defined(PORT0_USART)
    portStates[0].uart = PORT0_USART;
#endif
#if defined(PORT1_USART)
    portStates[1].uart = PORT1_USART;
#endif
#if defined(PORT2_USART)
    portStates[2].uart = PORT2_USART;
#endif
#if defined(PORT3_USART)
    portStates[3].uart = PORT3_USART;
#endif

    header[0] = NETWORK_ESC;
    // header[1] = mask
    header[2] = 0x21; // flags: bootloader protocol, one destination node
    // header[3] = src
    header[4] = 0x00; // dst is always 0
    header[5] = BOOT_TYPE_PASSED;
    header[6] = UUID_SIZE;
    header[7] = ModelSTM32C011xx;
    *(uint32_t*)&header[8] = HAL_GetUIDw0();
    *(uint32_t*)&header[12] = HAL_GetUIDw1();
    *(uint32_t*)&header[16] = HAL_GetUIDw2();
}

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

static bool validatePacket(const uint8_t *data, size_t length)
{
    if (length < 4) {
        return false;
    }

    uint32_t calculatedCrc = calcCrc(&data[2], length - 2 - 4);
    uint32_t receivedCrc = getUint32(data + length - 4);

    return calculatedCrc == receivedCrc;
}

static void sendBytes(USART_TypeDef *uart, const uint8_t *data, size_t length)
{
    for (size_t i = 0; i < length; i++)
    {
        txSend(uart, data[i]);
    }
}

void uartTxPrepare(PortState *port)
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
    if (map[NETWORK_ESC / 32] & (1 << (NETWORK_ESC % 32))) {
        // Avoid using ESC or END as mask
        map[(NETWORK_ESC ^ NETWORK_ESC) / 32] |= (1 << ((NETWORK_ESC ^ NETWORK_ESC) % 32));
        map[(NETWORK_END ^ NETWORK_ESC) / 32] |= (1 << ((NETWORK_END ^ NETWORK_ESC) % 32));
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

void uartTxFinalize(PortState *port)
{
    uint32_t crc = calcCrc(&txBuffer[2], txSize - 2);
    copyBytes(&txBuffer[txSize], &crc, sizeof(crc));
    txSize += sizeof(crc);
    txBuffer[1] = applyMask(&txBuffer[2], txSize - 2);
    txBuffer[txSize++] = NETWORK_ESC;
    txBuffer[txSize++] = NETWORK_END;
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
    else if (byte == NETWORK_ESC)
    {
        sendBytes(port->uart, txBuffer, txSize);
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

static void receiveHandler(PortState *port)
{
    if (port->headerReceived)
    {
        receiveContent(port);
    }
    else
    {
        receiveHeader(port);
    }
}

void uartPoll()
{
    for (int i = 0; i < sizeof(portStates) / sizeof(portStates[0]); i++)
    {
        receiveHandler(&portStates[i]);
    }
}

void uartTxAppend(struct PortState *port, const void *data, size_t length)
{
    copyBytes(&txBuffer[txSize], data, length);
    txSize += length;
}
