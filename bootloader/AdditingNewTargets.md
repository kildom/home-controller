

* Create new project in STM32CubeIDE
* Configure USARTs:
  * 115200 baud, 8N1, no flow control
  * Overrun: DISABLE
  * GPIO open-drain on TX where needed
* SYS:
  * Timebase source: None
* Clocks:
  * Internal shold be good enough
* CRC:
  * Activate
* Set heap size to 0x000
* Override weak: `HAL_StatusTypeDef HAL_InitTick(uint32_t TickPriority) { return HAL_OK; }`.
* Advanced Settings:
  * All LL drivers (if possible)
* `main.c`:
  * Disable interrupts `__disable_irq();`
  * Deactivate CRC initialization `#if 0` ... `#endif` and add just `__HAL_RCC_CRC_CLK_ENABLE();`
  * In main loop:
    ```
    void uartPoll(USART_TypeDef *uart0, USART_TypeDef *uart1, USART_TypeDef *uart2, USART_TypeDef *uart3);
    uartPoll(USART1, USART2, NULL, NULL);
    ```
* Linker script:
  * Set flash size to 4K
  * Set RAM size to actual RAM size minus 8 bytes (for the bootloader magic bytes)
* `SystemInit()`:
  * Add `void bootSelect(); bootSelect();` at the beginning of the function
* If new microcontroller, edit `bootloader.c`:
  * Add new model
  * Check if the memory sizes are correct
  * Check if flash writing is correct

