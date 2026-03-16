#include "main.h"
#include "uart.h"
#include "prog.h"

#define PORT0_BAUDRATE 115200
#define PORT1_BAUDRATE 115200

#define BUFFER_SIZE 256 // TODO: May be smaller

static const int ERROR_DATA = -2;
static const int NO_DATA = -1;
static const uint8_t NETWORK_ESC = 0xAA;
static const uint8_t NETWORK_END = 0xFF;
static const uint8_t BOOT_ESC = 0xA5;
static const int BOOT_END = 0x100;

typedef struct PortState
{
    USART_TypeDef *uart;
    uint32_t rxIndex;
    bool headerReceived;
    int rxValueOffset;
    uint8_t rxBuffer[BUFFER_SIZE];
} PortState;

static PortState portStates[2];
static uint8_t header[6 + 1 + 1 + 2 * 12];
static int headerLength;
static uint8_t txBuffer[BUFFER_SIZE];
static int txIndex = 0;

void copyBytes(void *dest, const void *src, size_t n)
{
    uint8_t *d = (uint8_t *)dest;
    const uint8_t *s = (const uint8_t *)src;
    const uint8_t *end = (const uint8_t *)src + n;
    while (s < end)
    {
        *d++ = *s++;
    }
}

uint32_t encodeBytes(void *dest, const void *src, size_t n)
{
    uint8_t *d = (uint8_t *)dest;
    const uint8_t *s = (const uint8_t *)src;
    const uint8_t *end = (const uint8_t *)src + n;
    while (s < end)
    {
        uint8_t byte = *s++;
        if (byte == BOOT_ESC || byte == NETWORK_ESC)
        {
            *d++ = BOOT_ESC;
            byte -= BOOT_ESC;
        }
        *d++ = byte;
    }
    return d - (uint8_t *)dest;
}

/*
 * Programmer -> Proxy and Proxy -> destination port (since they are almost the same):
 *      ESC
 *      mask
 *      flags = 0x21 (bootloader protocol, one destination node)
 *      src   = programmer address
 *      dst   = proxy address
 *      boot  = type: pass, port: N
 *      UID length
 *      UID
 *      cmd
 *      cmd counter
 *      cmd arguments...
 *      CRC32
 *      ESC
 *      END (not when Proxy -> destination port)
 * 
 * Proxy -> destination port
 *      ESC
 *      mask
 *      flags = 0x21 (bootloader protocol, one destination node)
 *      src   = programmer address
 *      dst   = 0 (unknown device)
 *      boot  = 0
 *      data...
 *      CRC32
 *      ESC
 *      (no END)
 * 
 */

void uartInit()
{
    static const uint8_t constHeaderBytes[6] = {
        0xAA, // 0 ESC      - start of packet
        0x00, // 1 mask     - boot packet will never contain 0xAA
        0x21, // 2 flags    - bootloader protocol, one destination node
        0x00, // 3 src      - address of proxy node, it will be ignored
        0x00, // 4 dst      - address of programmer node, it will be ignored
        0xB1, // 5 type     - bootloader packet type
              // 6 UID length
              // 7 UID model
              // 8... UID content
    };

    uint32_t wUID[3] = {
        HAL_GetUIDw0(),
        HAL_GetUIDw1(),
        HAL_GetUIDw2(),
    };

    portStates[0].uart = USART1;
    portStates[1].uart = USART2;
    copyBytes(header, constHeaderBytes, 6);
    header[6] = encodeBytes(&header[8], wUID, sizeof(wUID)) + 1;
    header[7] = ModelSTM32C011xx;
    headerLength = 6 + header[6];
}

void txSend(USART_TypeDef *uart, uint8_t byte)
{
    // Wait until Tx FIFO is not full using TXE flag in USART_ISR register
    while (!LL_USART_IsActiveFlag_TXE(uart))
    {
    }
    LL_USART_TransmitData8(uart, byte);
}

int rxReceive(USART_TypeDef *uart)
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

static void switchHandler(PortState *port, bool headerReceived)
{
    port->headerReceived = headerReceived;
    port->rxIndex = 0;
    port->rxValueOffset = 0;
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
        switchHandler(port, false);
    }
    else if (byte == header[port->rxIndex] || port->rxIndex == 3) // Allow any byte at dst position
    {
        port->rxIndex++;
        if (port->rxIndex == headerLength)
        {
            switchHandler(port, true);
        }
    }
    else
    {
        switchHandler(port, false);
    }
}

static bool validatePacket(const uint8_t *data, size_t length)
{
    if (length < 4)
    {
        return false;
    }

    const uint8_t *ptr = data;
    const uint8_t *end = ptr + length - 4;

    LL_CRC_ResetCRCCalculationUnit(CRC);

    while (ptr < end)
    {
        LL_CRC_FeedData8(CRC, *ptr++);
    }

    uint32_t calculatedCrc = LL_CRC_ReadData32(CRC);
    uint32_t receivedCrc;
    copyBytes(&receivedCrc, end, sizeof(receivedCrc));

    return calculatedCrc == receivedCrc;
}

static uint32_t sendBytes(USART_TypeDef *uart, const uint8_t *data, size_t length)
{
    LL_CRC_ResetCRCCalculationUnit(CRC);
    for (size_t i = 0; i < length; i++)
    {
        uint8_t byte = data[i];
        LL_CRC_FeedData8(CRC, byte);
        if (byte == BOOT_ESC || byte == NETWORK_ESC)
        {
            txSend(uart, BOOT_ESC);
            byte -= BOOT_ESC;
        }
        txSend(uart, byte);
    }
    return LL_CRC_ReadData32(CRC);
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
        switchHandler(port, false);
    }
    else if (byte == BOOT_ESC)
    {
        port->rxValueOffset = byte;
    }
    else
    {
        byte += port->rxValueOffset;
        port->rxValueOffset = 0;

        if (byte == BOOT_END)
        {
            uint32_t crc = sendBytes(port->uart, txBuffer, txIndex);
            sendBytes(port->uart, (uint8_t *)&crc, sizeof(crc));
            txSend(port->uart, NETWORK_ESC);
            txSend(port->uart, NETWORK_END);
            txIndex = 0;
            if (port->rxIndex > 0 && validatePacket(port->rxBuffer, port->rxIndex))
            {
                packetReceived(port->rxBuffer, port->rxIndex);
            }
            switchHandler(port, false);
        }
        else if (port->rxIndex < sizeof(port->rxBuffer))
        {
            port->rxBuffer[port->rxIndex++] = byte;
        }
        else
        {
            switchHandler(port, false);
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

void uartAppend(const void *data, size_t length)
{
    copyBytes(&txBuffer[txIndex], data, length);
    txIndex += length;
}
