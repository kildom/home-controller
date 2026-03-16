/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include "stm32c0xx_ll_crc.h"
#include "rng.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

CRC_HandleTypeDef hcrc;

/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_USART1_UART_Init(void);
static void MX_USART2_UART_Init(void);
static void MX_CRC_Init(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

#define FREQUENCY 48000000
#define US_TO_TICKS(us) ((int32_t)((int64_t)(us) * (int64_t)FREQUENCY / (int64_t)1000000))
#define MS_TO_TICKS(ms) ((int32_t)((int64_t)(ms) * (int64_t)FREQUENCY / (int64_t)1000))
#define SEC_TO_TICKS(s) ((int32_t)((int64_t)(s) * (int64_t)FREQUENCY))
#define UART_BITS_TO_TICKS(bits, baud) ((int32_t)((int64_t)(bits) * (int64_t)FREQUENCY / (int64_t)baud))
#define UART_BYTES_TO_TICKS(bytes, baud) ((int32_t)((int64_t)(bytes) * (int64_t)FREQUENCY * (int64_t)11 / (int64_t)baud))


typedef struct DeviceInfo {
	uint32_t loadAddress; // Loader can use hex file start address to verify address before loading
	uint16_t totalPages;
	uint8_t pageSizeLog2;
	uint8_t writeSizeLog2;
	uint8_t deviceModel;
} DeviceInfo;

DeviceInfo deviceInfo = {
		.loadAddress = 0x08000000 + 6 * 1024,
		.totalPages = 32 * 1024 - 6 * 1024,
		.pageSizeLog2 = 11, // 2K
		.writeSizeLog2 = 3, // 8 bytes
#ifdef STM32C011xx
		.deviceModel = ModelSTM32C011xx,
#else
#error Unknown Device
#endif
};

typedef struct DeviceSignature {
	uint32_t magicWord0;
	uint32_t magicWord1;
	DeviceInfo info;
} DeviceSignature;

enum Model {
	ModelSTM32C011xx = 1,
};

__attribute__((used))
const DeviceSignature deviceSignature = { // TODO: This goes to application only
	.magicWord0 = 0x96d287ac,
	.magicWord1 = 0xd35b7c9a,
	.info = { // TODO: This goes to both: application and bootloader
		.loadAddress = 0x08001800,
		.totalPages = 13,
		.pageSizeLog2 = 11, // 2K
		.writeSizeLog2 = 3, // 8 bytes
#ifdef STM32C011xx
		.deviceModel = ModelSTM32C011xx,
#else
#error Unknown Device
#endif
	},
};



#if 0

uint8_t randByte() {
	return rand() & 0xFF; // TODO: More entropy
}

static const uint8_t ESC = 0xAA;
static const uint8_t STOP = 0xFF;

static uint8_t packet[256 + 6];
static uint8_t* const packetData = &packet[2];
static int packetDataLength;

uint8_t initSeq[1 + 4];
int initSeqIndex;

bool busAcquisition(int port, uint32_t globalTimeout)
{
	// The main loop that repeats if we need to repeat negotiation from the beginning
repeatMainLoop:
	initSeq[0] = ESC;
	initSeq[1] = randByte() << 2;
	initSeq[2] = randByte() << 2;
	initSeq[3] = randByte() << 2;
	initSeq[4] = randByte() << 2;
	initSeqIndex = 0;

	uint32_t timeout = getTime() + portConfig[port].idleTimeout;
	bool lastEsc = false;

	// Wait for free line
	while ((int32_t)(timeout - getTime()) > 0) {
		// Make sure we did not hit global timeout
		if ((int32_t)(globalTimeout - getTime()) < 0) {
			return false;
		}
		// Check UART Rx data
		int byte = readPortData(port);
		if (byte != NODATA) {
			// Extend waiting if we have a new byte, but reduce if this is a STOP sequence.
			timeout = getTime() + portConfig[port].idleTimeout;
			if (byte == ESC) {
				lastEsc = true;
			} else {
				if (lastEsc && byte == STOP) {
					timeout = getTime() + portConfig[port].frameDelay;
				}
				lastEsc = false;
			}
		}
		// Extend waiting if we detected UART start bit
		if (!readPortState(port)) {
			timeout = getTime() + portConfig[port].idleTimeout;
		}
	}

	int successBytes = 0;

	// Loop over each byte in init sequence until everything from it is send
	while (successBytes < sizeof(initSeq)) {

		// Make sure we did not hit global timeout
		if ((int32_t)(globalTimeout - getTime()) < 0) {
			return false;
		}

		int recvByte;

		// Prepare byte to send
		uint8_t sendByte = initSeq[initSeqIndex];
		initSeqIndex++;
		if (initSeqIndex == sizeof(initSeq)) {
			initSeqIndex = 0;
		}

		// Calculate propagation timeout
		timeout = getTime() + portConfig[port].propagationTimeout;

		// Do final check on port state
		do {
			if (!readPortState(port)) {
				goto repeatMainLoop;
			}
		} while (!txReady(port));

		// Send byte
		writePortData(port, sendByte);

		// Wait for the same byte received
		while (true) {
			recvByte = readPortData(port);
			if (byte == ERRORDATA) {
				// If error on UART Rx line detected, assume 0xFF which is for sure different than expected
				recvByte = 0xFF;
				break;
			} else if (recvByte >= 0) {
				// If byte received, exit this loop
				break;
			} else if ((int32_t)(timeout - getTime()) < 0) {
				// If timeout occured, then something is very bad, e.g. line short circuit, repeat everything again
				goto repeatMainLoop;
			}
		}

		if (recvByte == sendByte) {
			// No collisions, count successes and continue sending next byte
			successBytes++;
			continue;
		}

		// Collision detected, wait calculated time and retry with the next byte in the sequence
		successBytes = 0;
		uint32_t waitTime = portConfig[port].negotiationContantWait;
		uint8_t collisions = recvByte ^ sendByte;
		waitTime += (__builtin_clz(collisions) - 24) * portConfig[port].negotiationWaitPerBit;

		// Wait and monitor Rx line
		timeout = getTime() + waitTime;
		while ((int32_t)(timeout - getTime()) > 0) {
			if (!readPortState(port)) {
				goto repeatMainLoop;
			}
		}
	}

	// Enough non-colliding bytes send - we did acquisition
	return true;
}

void sendPacket(int port)
{
	uint32_t globalTimeout = getTime() + portConfig[port].packetSendTimeout;

	// Loop over send retries if some collision during sending was detected
	while ((int32_t)(globalTimeout - getTime()) > 0) {

		if (!busAcquisition(port, globalTimeout)) {
			return;
		}

		int sendIndex = 0;
		int recvIndex = 0;
		int total = 2 + packetDataLength + 4 + 2;

		while (true) {
			if (txReady(port)) {
				writePortData(port, packet[sendIndex]);
				sendIndex++;
				if (sendIndex == total) {
					return;
				}
			}
			int recv = readPortData(port);
			if (recv == ERRORDATA) {
				// Error detected on Rx - something interrupted the packet, try send packet again if not fully send
				break;
			} if (recv >= 0) {
				// Something received, check if expected
				if (recv != packet[recvIndex]) {
					break;
				}
				recvIndex++;
				if (recvIndex > sendIndex) {
					// Too many received bytes - retry everything
					break;
				}
			}
		}
	}
}
#endif


typedef struct NetworkPort {
  USART_TypeDef *uart;
  GPIO_TypeDef *txGpio;
  uint32_t txPin;
  GPIO_TypeDef *rxGpio;
  uint32_t rxPin;
  uint32_t idleTimeout;
  uint32_t frameDelay;
  uint32_t propagationTimeout;
  uint32_t negotiationConstantWait;
  uint32_t negotiationWaitPerBit;
  uint32_t packetSendTimeout;
} NetworkPort;

#define PORT0_BAUDRATE 115200
#define PORT1_BAUDRATE 115200

NetworkPort ports[2] = {
  {
    .uart = USART1,
    .txGpio = GPIOA,
    .txPin = LL_GPIO_PIN_0,
    .rxGpio = GPIOB,
    .rxPin = LL_GPIO_PIN_7,
    .idleTimeout = UART_BYTES_TO_TICKS(6, PORT0_BAUDRATE) + MS_TO_TICKS(10),
    .frameDelay = UART_BYTES_TO_TICKS(6, PORT0_BAUDRATE),
    .propagationTimeout = UART_BYTES_TO_TICKS(3, PORT0_BAUDRATE),
    .negotiationConstantWait = UART_BITS_TO_TICKS(5, PORT0_BAUDRATE),
    .negotiationWaitPerBit = UART_BITS_TO_TICKS(2, PORT0_BAUDRATE),
    .packetSendTimeout = MS_TO_TICKS(2000),
  },
  {
    .uart = USART2,
    .txGpio = GPIOA,
    .txPin = LL_GPIO_PIN_2,
    .rxGpio = GPIOA,
    .rxPin = LL_GPIO_PIN_3,
    .idleTimeout = UART_BYTES_TO_TICKS(6, PORT1_BAUDRATE) + MS_TO_TICKS(10),
    .frameDelay = UART_BYTES_TO_TICKS(6, PORT1_BAUDRATE),
    .propagationTimeout = UART_BYTES_TO_TICKS(3, PORT1_BAUDRATE),
    .negotiationConstantWait = UART_BITS_TO_TICKS(5, PORT1_BAUDRATE),
    .negotiationWaitPerBit = UART_BITS_TO_TICKS(2, PORT1_BAUDRATE),
    .packetSendTimeout = MS_TO_TICKS(2000),
  },
};

static const int ERROR_DATA = -2;
static const int NO_DATA = -1;

void txSend(NetworkPort *port, uint8_t byte) {
  // Wait until Tx FIFO is not full using TXE flag in USART_ISR register
  while (!LL_USART_IsActiveFlag_TXE(port->uart)) {}
  LL_USART_TransmitData8(port->uart, byte);
}

void txWait(NetworkPort *port) {
  // Use TXFE flag in USART_ISR to wait until Tx FIFO is empty
  while (!LL_USART_IsActiveFlag_TXE(port->uart)) {}
  while (!LL_USART_IsActiveFlag_TC(port->uart)) {}
}

bool txIsReady(NetworkPort *port) {
  // Use TXE flag in USART_ISR register to check if we can send data
  return !!LL_USART_IsActiveFlag_TXE(port->uart);
}

bool txIsSend(NetworkPort *port) {
  return LL_USART_IsActiveFlag_TXE(port->uart) && LL_USART_IsActiveFlag_TC(port->uart);
}

int rxReceive(NetworkPort *port) {
  // Use RXNE flag in USART_ISR register to check if data is received and can be read from RDR register
  if (LL_USART_IsActiveFlag_RXNE(port->uart)) {
    // First check framing error using FE flag in USART_ISR register and clear it by setting FECF flag.
    if (LL_USART_IsActiveFlag_FE(port->uart)) {
      LL_USART_ClearFlag_FE(port->uart);
      LL_USART_ReceiveData8(port->uart);
      return ERROR_DATA;
    }
    return LL_USART_ReceiveData8(port->uart);
  } else {
    // No data received
    return NO_DATA;
  }
}

bool rxPinState(NetworkPort *port) {
  // Read Rx pin state using IDR register of Rx GPIO
  return !!(LL_GPIO_ReadInputPort(port->rxGpio) & port->rxPin);
}


HAL_StatusTypeDef HAL_InitTick(uint32_t TickPriority)
{
  SysTick->LOAD  = SysTick_LOAD_RELOAD_Msk;
  SysTick->CTRL  = SysTick_CTRL_CLKSOURCE_Msk | SysTick_CTRL_ENABLE_Msk;
  return HAL_OK;
}


uint32_t lastTime = 0;

uint32_t getTime() {
  uint32_t now = SysTick_LOAD_RELOAD_Msk - SysTick->VAL;
  if (now < (lastTime & SysTick_LOAD_RELOAD_Msk)) {
    lastTime += SysTick_LOAD_RELOAD_Msk + 1;
  }
  lastTime = (lastTime & ~SysTick_LOAD_RELOAD_Msk) | now;
  return lastTime;
}

static inline bool expired(uint32_t timeout) {
  return (int32_t)(timeout - getTime()) < 0;
}

static const uint8_t ESC = 0xAA;
static const uint8_t END = 0xFF;


bool busAcquisition(NetworkPort *port, uint32_t globalTimeout)
{
  static uint8_t initSeq[1 + 2 + 1 + 2];
  static int initSeqIndex;

	// The main loop that repeats if we need to repeat negotiation from the beginning
repeatMainLoop:
  rngAdd(getTime());
	initSeq[0] = ESC;
	initSeq[1] = (rngGet() << 2) | 0x80;
	initSeq[2] = (rngGet() << 2) | 0x80;
	initSeq[3] = ESC;
	initSeq[4] = (rngGet() << 2) | 0x80;
	initSeq[5] = (rngGet() << 2) | 0x80;
	initSeqIndex = 0;

	uint32_t timeout = getTime() + port->idleTimeout;
	bool lastEsc = false;

	// Wait for free line
	while (!expired(timeout)) {
		// Make sure we did not hit global timeout
		if (expired(globalTimeout)) {
			return false;
		}
		// Check UART Rx data
		int byte = rxReceive(port);
		if (byte != NO_DATA) {
			// Extend waiting if we have a new byte, but reduce if this is a STOP sequence.
			timeout = getTime() + port->idleTimeout;
			if (byte == ESC) {
				lastEsc = true;
			} else {
				if (lastEsc && byte == END) {
					timeout = getTime() + port->frameDelay;
				}
				lastEsc = false;
			}
		}
		// Extend waiting if we detected UART start bit
		if (!rxPinState(port)) {
      rngAdd(getTime());
			timeout = getTime() + port->idleTimeout;
		}
	}

	int successBytes = 0;

	// Loop over each byte in init sequence until everything from it is send
	while (successBytes < sizeof(initSeq)) {

		// Make sure we did not hit global timeout
		if (expired(globalTimeout)) {
			return false;
		}

		int recvByte;

		// Prepare byte to send
		uint8_t sendByte = initSeq[initSeqIndex];
		initSeqIndex++;
		if (initSeqIndex == sizeof(initSeq)) {
			initSeqIndex = 0;
		}

		// Do final check on port state
		do {
			if (!rxPinState(port)) {
				goto repeatMainLoop;
			}
		} while (!txIsReady(port));

		// Send byte
		txSend(port, sendByte);
    while (!txIsSend(port)) {}

		// Calculate propagation timeout
		timeout = getTime() + port->propagationTimeout;

		// Wait for the same byte received
		while (true) {
			recvByte = rxReceive(port);
			if (recvByte == ERROR_DATA) {
				// If error on UART Rx line detected, assume 0xFF which is for sure different than expected
				recvByte = 0xFF;
				break;
			} else if (recvByte >= 0) {
				// If byte received, exit this loop
				break;
			} else if (expired(timeout)) {
				// If timeout occured, then something is very bad, e.g. line short circuit, repeat everything again
				goto repeatMainLoop;
			}
		}

    rngAdd(getTime());
    rngAdd(recvByte);

		if (recvByte == sendByte) {
			// No collisions, count successes and continue sending next byte
			successBytes++;
			continue;
		}

		// Collision detected, wait calculated time and retry with the next byte in the sequence
		successBytes = 0;
		uint32_t waitTime = port->negotiationConstantWait;
		uint8_t collisions = recvByte ^ sendByte;
		waitTime += (__builtin_clz(collisions) - 24) * port->negotiationWaitPerBit;

		// Wait and monitor Rx line
		timeout = getTime() + waitTime;
		while (!expired(timeout)) {
			if (!rxPinState(port)) {
				goto repeatMainLoop;
			}
		}
	}

	// Enough non-colliding bytes send - we did acquisition
	return true;
}

static uint8_t packet[256 + 6];
static uint8_t* const packetData = &packet[2];
static int packetDataLength;

void preparePacket()
{
  uint8_t mask = 0;
  uint32_t map[256 / 32] = {0};
  LL_CRC_ResetCRCCalculationUnit(CRC);

  // Calculate CRC and build byte usage map
  uint8_t *ptr = packetData;
  uint8_t *end = ptr + packetDataLength;
  while (ptr < end) {
      uint8_t byte = *ptr;
      LL_CRC_FeedData8(CRC, byte);
      map[byte / 32] |= (1 << (byte % 32));
      ptr++;
  }

  // Append CRC to data and update byte usage map
  uint32_t crc = LL_CRC_ReadData32(CRC);
  const uint8_t *crcPtr = (const uint8_t *)&crc;
  end = ptr + 4;
  while (ptr < end) {
      uint8_t byte = *crcPtr++;
      map[byte / 32] |= (1 << (byte % 32));
      *ptr++ = byte;
  }

  // Append STOP marker
  *ptr++ = ESC;
  *ptr++ = END;

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
    ptr = packetData;
    end = ptr + packetDataLength + 4;
    while (ptr < end) {
        *ptr ^= mask;
        ptr++;
    }
  }

  packet[0] = ESC;
  packet[1] = mask;
}

void sendPacket(NetworkPort *port)
{
	uint32_t globalTimeout = getTime() + port->packetSendTimeout;

  preparePacket();

	// Loop over send retries if some collision during sending was detected
	while ((int32_t)(globalTimeout - getTime()) > 0) {

		if (!busAcquisition(port, globalTimeout)) {
			return;
		}

		int sendIndex = 0;
		int recvIndex = 0;
		int total = 2 + packetDataLength + 4 + 2;

		while (true) {
			if (txIsReady(port)) {
				txSend(port, packet[sendIndex]);
				sendIndex++;
				if (sendIndex == total) {
					return;
				}
			}
			int recv = rxReceive(port);
			if (recv == ERROR_DATA) {
				// Error detected on Rx - something interrupted the packet, try send packet again if not fully send
				break;
			} if (recv >= 0) {
				// Something received, check if expected
				if (recv != packet[recvIndex]) {
					break;
				}
				recvIndex++;
				if (recvIndex > sendIndex) {
					// Too many received bytes - retry everything
					break;
				}
			}
		}
	}
}

/*
QUERY (broadcast):
  |    1     |
  | Type = 0 |

RESPONSE (unicast):
  |    1     |   1    | ID len | ...
  | Type = 1 | ID len |   ID   | device info: page size, flash size, HW model

RESET (broadcast):
  |    1     |   1    | ID len |
  | Type = 2 | ID len |   ID   |

ERASE (broadcast):
  |    1     |   1    | ID len |    4    |
  | Type = 3 | ID len |   ID   | address |

ERASE_DONE (unicast):
  |    1     |   1    | ID len |    4    |
  | Type = 4 | ID len |   ID   | address |

WRITE (broadcast):
  |    1     |   1    | ID len |    4    | ...  |
  | Type = 5 | ID len |   ID   | address | data |

WRITE_DONE (unicast):
  |    1     |   1    | ID len |    4    |  1  |
  | Type = 6 | ID len |   ID   | address | len |

READ (broadcast):
  |    1     |   1    | ID len |    4    |  1  |
  | Type = 7 | ID len |   ID   | address | len |

READ_DONE (unicast):
  |    1     |   1    | ID len |    4    | ...  |
  | Type = 8 | ID len |   ID   | address | data |

*/

enum {
  BOOT_QUERY = 0,
  BOOT_RESPONSE,
  BOOT_RESET,
  BOOT_ERASE,
  BOOT_ERASE_DONE,
  BOOT_WRITE,
  BOOT_WRITE_DONE,
  BOOT_READ,
  BOOT_READ_DONE,
};

typedef struct RecvState {
  NetworkPort *port;
  void* jumpLabel;
  uint8_t buffer[256 + 6];
  int bufferIndex;
  uint32_t idleTimeout;
  uint32_t rxPacketEndTime;
} RecvState;

#define RESUME_POINT(name) name: state->jumpLabel = &&name
#define SUSPEND() return
#define SUSPEND_AT(name) do { goto name; } while (0)

uint8_t deviceUidWithLen[14];

void bootPacketReceived(uint8_t *buffer, int length, uint8_t src)
{
  uint8_t packetType = buffer[0];

  // QUERY request has no other parameters
  if (packetType == BOOT_QUERY) {
    startResponse(BOOT_RESPONSE);
    appendResponse(&deviceInfo, sizeof(deviceInfo));
    sendResponse();
    return;
  }

  uint8_t* endPtr = buffer + length;
  uint8_t* receivedDeviceUidWithLen = &buffer[1];
  uint8_t* addressPtr = &buffer[1 + sizeof(deviceUidWithLen)];
  uint8_t* dataPtr = &buffer[1 + sizeof(deviceUidWithLen) + 4];
  uint32_t dataLength = *dataPtr;

  // Verify device UID
  if (addressPtr > endPtr
      || simpleCompare(receivedDeviceUidWithLen, deviceUidWithLen, sizeof(deviceUidWithLen)) != 0) {
    return;
  }

  // RESET request has no more parameters
  if (packetType == BOOT_RESET) {
    resetDevice();
    return;
  }

  uint32_t address = addressPtr[0] | (addressPtr[1] << 8) | (addressPtr[2] << 16) | (addressPtr[3] << 24);
  const uint32_t flashEndAddress = deviceInfo.loadAddress + (deviceInfo.totalPages << deviceInfo.pageSizeLog2);

  if (dataPtr > endPtr
      || address < deviceInfo.loadAddress
      || address >= flashEndAddress) {
    return;
  }

  if (packetType == BOOT_ERASE) {
    if (address & ((1 << deviceInfo.pageSizeLog2) - 1)) {
      return;
    }
    erasePage(address);
    startResponse(BOOT_ERASE_DONE);
    appendResponse(&address, sizeof(address));
    sendResponse();
    return;
  }

  if (dataPtr + 1 > endPtr) {
    return;
  }

  if (packetType == BOOT_READ) {
    if (address + dataLength > flashEndAddress || dataLength > 128) {
      return;
    }
    startResponse(BOOT_READ_DONE);
    appendResponse(&address, sizeof(address));
    appendResponse((void*)address, dataLength);
    sendResponse();
    return;
  }

  if (packetType == BOOT_WRITE) {
    const uint32_t writeSize = 1 << deviceInfo.writeSizeLog2;
    dataLength = endPtr - dataPtr;
    if ((address & (writeSize - 1)) || (dataLength & (writeSize - 1))) {
      return;
    }
    for (uint32_t offset = 0; offset < dataLength; offset += writeSize) {
      flashBegin();
      flashData(address + offset, &dataPtr[offset], writeSize);
      flashEnd();
    }
    startResponse(BOOT_WRITE_DONE);
    appendResponse(&address, sizeof(address));
    uint8_t lenByte = dataLength;
    appendResponse(&lenByte, sizeof(lenByte));
    sendResponse();
    return;
  }

  // Unknown packet type - ignore
}

void packetReceived(uint8_t *buffer, int length)
{
  // Data Link Layer processing

  // Verify packet length: minimum: 1 mask + 1 flags + 1 src + 1 request type + 4 CRC
  if (length < 8 || length > 1 + 249 + 4) {
    return;
  }

  // Do content descrambling if needed
  uint8_t mask = buffer[0];
  if (mask) {
    uint8_t *ptr = buffer;
    uint8_t *end = buffer + length;
    while (ptr < end) {
      *ptr ^= mask;
      ptr++;
    }
  }

  // Calculate CRC and verify it
  LL_CRC_ResetCRCCalculationUnit(CRC);
  uint8_t *ptr = buffer + 1;
  uint8_t *end = buffer + length - 4;
  while (ptr < end) {
      LL_CRC_FeedData8(CRC, *ptr);
      ptr++;
  }
  uint32_t crcExpected = LL_CRC_ReadData32(CRC);
  uint32_t crcReceived = (uint32_t)buffer[length - 4] |
                         ((uint32_t)buffer[length - 3] << 8) |
                         ((uint32_t)buffer[length - 2] << 16) |
                         ((uint32_t)buffer[length - 1] << 24);  
  if (crcExpected != crcReceived) {
    return;
  }

  // Network Layer processing

  uint8_t flags = buffer[1];
  // Only broadcast packets with BOOT protocol allowed
  if ((flags & 0x7F) != 0x20) {
    return;
  }
  uint8_t src = buffer[2];

  // Pass to BOOT protocol
  bootPacketReceived(buffer + 3, length - 3 - 4, src);
}  

void recvPacket(RecvState *state)
{
  int byte;

  if (state->jumpLabel) {
    goto *state->jumpLabel;
  }

  RESUME_POINT(esc);

  // Wait for ESC byte
  state->bufferIndex = 0;
  byte = rxReceive(state->port);
  if (byte != ESC) {
    SUSPEND();
  }
  state->idleTimeout = getTime() + state->port->idleTimeout;

  RESUME_POINT(mask);

  byte = rxReceive(state->port);
  if (byte == END || byte == ERROR_DATA || expired(state->idleTimeout)) {
    SUSPEND_AT(esc);
  } else if (byte == NO_DATA || byte == ESC) {
    SUSPEND();
  }

  state->buffer[state->bufferIndex++] = byte;
  state->idleTimeout = getTime() + state->port->idleTimeout;

  RESUME_POINT(content);

  byte = rxReceive(state->port);
  if (byte == ERROR_DATA || expired(state->idleTimeout)) {
    // Error or timeout - restart packet reception
    SUSPEND_AT(esc);
  } else if (byte == NO_DATA) {
    // No data - just wait
    SUSPEND();
  } else if (byte == ESC) {
    // ESC byte - could be end of packet, process it and start waiting for mask byte
    packetReceived(state->buffer, state->bufferIndex);
    SUSPEND_AT(mask);
  } else if (state->bufferIndex < sizeof(state->buffer)) {
    // Normal byte received and we have space in buffer - store it and keep waiting for next byte
    state->buffer[state->bufferIndex++] = byte;
    state->idleTimeout = getTime() + state->port->idleTimeout;
    SUSPEND();
  } else {
    // Buffer overflow - restart packet reception
    SUSPEND_AT(esc);
  }
}

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  __disable_irq();

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  if (SystemCoreClock != FREQUENCY) {
    Error_Handler();
  }
  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_USART1_UART_Init();
  MX_USART2_UART_Init();
  MX_CRC_Init();
  /* USER CODE BEGIN 2 */

  __disable_irq();

  rngAdd(getTime());
  rngAdd(HAL_GetUIDw0());
  rngAdd(HAL_GetUIDw1());
  rngAdd(HAL_GetUIDw2());

  union {
    uint32_t w[3];
    uint8_t b[12];
  } a;

  a.w[0] = HAL_GetUIDw0();
  a.w[1] = HAL_GetUIDw1();
  a.w[2] = HAL_GetUIDw2();

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  uint8_t byte = 'a';
  bool oldState = false;
  uint32_t nextTime = 0;
  uint32_t now;
  int c = 0;
  int inc = 1;
  while (1)
  {
    do {
      now = getTime();
    } while ((int32_t)(now - nextTime) < 0);
    nextTime = now + MS_TO_TICKS(1);
    //busAcquisition(&ports[1], getTime() + MS_TO_TICKS(1000));
    // 
    packetData[0] = a.b[0];
    packetData[1] = a.b[1];
    packetData[2] = a.b[2];
    packetData[3] = a.b[3];
    packetData[4] = a.b[4];
    packetData[5] = a.b[5];
    packetData[6] = a.b[6];
    packetData[7] = a.b[7];
    packetData[8] = a.b[8];
    packetData[9] = a.b[9];
    packetData[10] = a.b[10];
    packetData[11] = a.b[11];
    packetDataLength = 12;
    sendPacket(&ports[1]);
    //txSend(&ports[1], LL_RCC_HSI_GetCalibTrimming());
    // while (!txIsSend(&ports[1])) {
    //   bool newState = rxPinState(&ports[1]);
    //   if (newState && !oldState) {
    //     byte++;
    //     if (byte > 'z') {
    //       byte = 'a';
    //     }          
    //   }
    //   oldState = newState;
    // }
    // c++;
    // if (c == 1000) {
    //   c = 0;
    //   uint32_t cal = LL_RCC_HSI_GetCalibTrimming();
    //   LL_RCC_HSI_SetCalibTrimming(cal + inc);
    //   if (cal + inc >= 0x40 + 8 || cal + inc <= 0x40 - 8) {
    //     inc = -inc;
    //   }
    // }
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{

  LL_FLASH_SetLatency(LL_FLASH_LATENCY_1);

  /* HSI configuration and activation */
  LL_RCC_HSI_Enable();
  while(LL_RCC_HSI_IsReady() != 1)
  {
  }

  LL_RCC_HSI_SetCalibTrimming(64);
  LL_RCC_SetHSIDiv(LL_RCC_HSI_DIV_1);
  /* Set AHB prescaler*/
  LL_RCC_SetAHBPrescaler(LL_RCC_HCLK_DIV_1);

  /* Sysclk activation on the HSI */
  LL_RCC_SetSysClkSource(LL_RCC_SYS_CLKSOURCE_HSI);
  while(LL_RCC_GetSysClkSource() != LL_RCC_SYS_CLKSOURCE_STATUS_HSI)
  {
  }

  /* Set APB1 prescaler*/
  LL_RCC_SetAPB1Prescaler(LL_RCC_APB1_DIV_1);
  /* Update CMSIS variable (which can be updated also through SystemCoreClockUpdate function) */
  LL_SetSystemCoreClock(48000000);

   /* Update the time base */
  if (HAL_InitTick (TICK_INT_PRIORITY) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief CRC Initialization Function
  * @param None
  * @retval None
  */
static void MX_CRC_Init(void)
{
  /* USER CODE BEGIN CRC_Init 0 */
#if 0
  /* USER CODE END CRC_Init 0 */

  /* USER CODE BEGIN CRC_Init 1 */

  /* USER CODE END CRC_Init 1 */
  hcrc.Instance = CRC;
  hcrc.Init.DefaultPolynomialUse = DEFAULT_POLYNOMIAL_ENABLE;
  hcrc.Init.DefaultInitValueUse = DEFAULT_INIT_VALUE_ENABLE;
  hcrc.Init.InputDataInversionMode = CRC_INPUTDATA_INVERSION_NONE;
  hcrc.Init.OutputDataInversionMode = CRC_OUTPUTDATA_INVERSION_DISABLE;
  hcrc.InputDataFormat = CRC_INPUTDATA_FORMAT_BYTES;
  if (HAL_CRC_Init(&hcrc) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN CRC_Init 2 */
#else
  __HAL_RCC_CRC_CLK_ENABLE();
#endif
  /* USER CODE END CRC_Init 2 */

}

/**
  * @brief USART1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART1_UART_Init(void)
{

  /* USER CODE BEGIN USART1_Init 0 */

  /* USER CODE END USART1_Init 0 */

  LL_USART_InitTypeDef USART_InitStruct = {0};

  LL_GPIO_InitTypeDef GPIO_InitStruct = {0};

  LL_RCC_SetUSARTClockSource(LL_RCC_USART1_CLKSOURCE_PCLK1);

  /* Peripheral clock enable */
  LL_APB2_GRP1_EnableClock(LL_APB2_GRP1_PERIPH_USART1);

  LL_IOP_GRP1_EnableClock(LL_IOP_GRP1_PERIPH_GPIOB);
  LL_IOP_GRP1_EnableClock(LL_IOP_GRP1_PERIPH_GPIOA);
  /**USART1 GPIO Configuration
  PB7   ------> USART1_RX
  PA0   ------> USART1_TX
  */
  GPIO_InitStruct.Pin = LL_GPIO_PIN_7;
  GPIO_InitStruct.Mode = LL_GPIO_MODE_ALTERNATE;
  GPIO_InitStruct.Speed = LL_GPIO_SPEED_FREQ_LOW;
  GPIO_InitStruct.OutputType = LL_GPIO_OUTPUT_PUSHPULL;
  GPIO_InitStruct.Pull = LL_GPIO_PULL_NO;
  GPIO_InitStruct.Alternate = LL_GPIO_AF_0;
  LL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  GPIO_InitStruct.Pin = LL_GPIO_PIN_0;
  GPIO_InitStruct.Mode = LL_GPIO_MODE_ALTERNATE;
  GPIO_InitStruct.Speed = LL_GPIO_SPEED_FREQ_LOW;
  GPIO_InitStruct.OutputType = LL_GPIO_OUTPUT_PUSHPULL;
  GPIO_InitStruct.Pull = LL_GPIO_PULL_NO;
  GPIO_InitStruct.Alternate = LL_GPIO_AF_4;
  LL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /* USER CODE BEGIN USART1_Init 1 */

  /* USER CODE END USART1_Init 1 */
  USART_InitStruct.PrescalerValue = LL_USART_PRESCALER_DIV1;
  USART_InitStruct.BaudRate = 115200;
  USART_InitStruct.DataWidth = LL_USART_DATAWIDTH_8B;
  USART_InitStruct.StopBits = LL_USART_STOPBITS_2;
  USART_InitStruct.Parity = LL_USART_PARITY_NONE;
  USART_InitStruct.TransferDirection = LL_USART_DIRECTION_TX_RX;
  USART_InitStruct.HardwareFlowControl = LL_USART_HWCONTROL_NONE;
  USART_InitStruct.OverSampling = LL_USART_OVERSAMPLING_16;
  LL_USART_Init(USART1, &USART_InitStruct);
  LL_USART_SetTXFIFOThreshold(USART1, LL_USART_FIFOTHRESHOLD_1_8);
  LL_USART_SetRXFIFOThreshold(USART1, LL_USART_FIFOTHRESHOLD_1_8);
  LL_USART_DisableFIFO(USART1);
  LL_USART_DisableOverrunDetect(USART1);
  LL_USART_ConfigAsyncMode(USART1);

  /* USER CODE BEGIN WKUPType USART1 */

  /* USER CODE END WKUPType USART1 */

  LL_USART_Enable(USART1);

  /* Polling USART1 initialisation */
  while((!(LL_USART_IsActiveFlag_TEACK(USART1))) || (!(LL_USART_IsActiveFlag_REACK(USART1))))
  {
  }
  /* USER CODE BEGIN USART1_Init 2 */

  /* USER CODE END USART1_Init 2 */

}

/**
  * @brief USART2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART2_UART_Init(void)
{

  /* USER CODE BEGIN USART2_Init 0 */

  /* USER CODE END USART2_Init 0 */

  LL_USART_InitTypeDef USART_InitStruct = {0};

  LL_GPIO_InitTypeDef GPIO_InitStruct = {0};

  /* Peripheral clock enable */
  LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_USART2);

  LL_IOP_GRP1_EnableClock(LL_IOP_GRP1_PERIPH_GPIOA);
  /**USART2 GPIO Configuration
  PA2   ------> USART2_TX
  PA3   ------> USART2_RX
  */
  GPIO_InitStruct.Pin = LL_GPIO_PIN_2;
  GPIO_InitStruct.Mode = LL_GPIO_MODE_ALTERNATE;
  GPIO_InitStruct.Speed = LL_GPIO_SPEED_FREQ_LOW;
  GPIO_InitStruct.OutputType = LL_GPIO_OUTPUT_PUSHPULL;
  GPIO_InitStruct.Pull = LL_GPIO_PULL_NO;
  GPIO_InitStruct.Alternate = LL_GPIO_AF_1;
  LL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  GPIO_InitStruct.Pin = LL_GPIO_PIN_3;
  GPIO_InitStruct.Mode = LL_GPIO_MODE_ALTERNATE;
  GPIO_InitStruct.Speed = LL_GPIO_SPEED_FREQ_LOW;
  GPIO_InitStruct.OutputType = LL_GPIO_OUTPUT_PUSHPULL;
  GPIO_InitStruct.Pull = LL_GPIO_PULL_NO;
  GPIO_InitStruct.Alternate = LL_GPIO_AF_1;
  LL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /* USER CODE BEGIN USART2_Init 1 */

  /* USER CODE END USART2_Init 1 */
  USART_InitStruct.PrescalerValue = LL_USART_PRESCALER_DIV1;
  USART_InitStruct.BaudRate = 115200;
  USART_InitStruct.DataWidth = LL_USART_DATAWIDTH_8B;
  USART_InitStruct.StopBits = LL_USART_STOPBITS_2;
  USART_InitStruct.Parity = LL_USART_PARITY_NONE;
  USART_InitStruct.TransferDirection = LL_USART_DIRECTION_TX_RX;
  USART_InitStruct.HardwareFlowControl = LL_USART_HWCONTROL_NONE;
  USART_InitStruct.OverSampling = LL_USART_OVERSAMPLING_16;
  LL_USART_Init(USART2, &USART_InitStruct);
  LL_USART_DisableOverrunDetect(USART2);
  LL_USART_ConfigAsyncMode(USART2);

  /* USER CODE BEGIN WKUPType USART2 */

  /* USER CODE END WKUPType USART2 */

  LL_USART_Enable(USART2);

  /* Polling USART2 initialisation */
  while((!(LL_USART_IsActiveFlag_TEACK(USART2))) || (!(LL_USART_IsActiveFlag_REACK(USART2))))
  {
  }
  /* USER CODE BEGIN USART2_Init 2 */

  /* USER CODE END USART2_Init 2 */

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  LL_IOP_GRP1_EnableClock(LL_IOP_GRP1_PERIPH_GPIOB);
  LL_IOP_GRP1_EnableClock(LL_IOP_GRP1_PERIPH_GPIOA);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
