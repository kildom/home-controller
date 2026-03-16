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

typedef struct DeviceInfo {
	uint32_t loadAddress; // Loader can use hex file start address to verify address before loading
	uint16_t totalPages;
	uint8_t pageSizeLog2;
	uint8_t writeSizeLog2;
	uint8_t deviceModel;
} DeviceInfo;

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
} NetworkPort;

NetworkPort ports[2] = {
  {
    .uart = USART1,
    .txGpio = GPIOA,
    .txPin = LL_GPIO_PIN_0,
    .rxGpio = GPIOB,
    .rxPin = LL_GPIO_PIN_7,
  },
  {
    .uart = USART2,
    .txGpio = GPIOA,
    .txPin = LL_GPIO_PIN_2,
    .rxGpio = GPIOA,
    .rxPin = LL_GPIO_PIN_3,
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

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_USART1_UART_Init();
  MX_USART2_UART_Init();
  MX_CRC_Init();
  /* USER CODE BEGIN 2 */

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  uint8_t byte = 'a';
  bool oldState = false;
  while (1)
  {
      txSend(&ports[1], byte);
      while (!txIsSend(&ports[1])) {
        bool newState = rxPinState(&ports[1]);
        if (newState && !oldState) {
          byte++;
          if (byte > 'z') {
            byte = 'a';
          }          
        }
        oldState = newState;
      }
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

  /* HSI configuration and activation */
  LL_RCC_HSI_Enable();
  while(LL_RCC_HSI_IsReady() != 1)
  {
  }

  LL_RCC_HSI_SetCalibTrimming(64);
  LL_RCC_SetHSIDiv(LL_RCC_HSI_DIV_2);
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
  LL_SetSystemCoreClock(24000000);

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
