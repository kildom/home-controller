

# If new chip is required

* Create new project in STM32CubeIDE
* Configure USARTs:
  * 8N1, no flow control
  * Overrun: DISABLE
* SYS:
  * Timebase source: None
* Clocks:
  * Internal should be good enough
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
  * Set flash size to 8K
  * Set RAM size to actual RAM size minus 8 bytes (for the bootloader magic bytes)
* `SystemInit()`:
  * Add `void bootSelect(); bootSelect();` at the beginning of the function
* If new microcontroller, edit `bootloader.c`:
  * Add new model
  * Check if the memory sizes are correct
  * Check if flash writing is correct
* Edit `patch.py`:


# If the same chip but different port configuration

* Create new build configurations in STM32CubeIDE
* Edit PORTx_BAUDRATE, PORTx_OC defines in new build configurations
