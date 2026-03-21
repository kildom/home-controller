export const patch = [
    {
        file: 'router/Core/Src/main.c',
        search: /(USART1 Initialization Function[\s\S]*?USART_InitStruct\.BaudRate = )115200/,
        replace: '$1PORT0_BAUDRATE',
    },
    {
        file: 'router/Core/Src/main.c',
        search: /(USART2 Initialization Function[\s\S]*?USART_InitStruct\.BaudRate = )115200/,
        replace: '$1PORT1_BAUDRATE',
    },
    {
        file: 'router/Core/Src/main.c',
        search: /(PA0[\s\S]*?GPIO_InitStruct\.OutputType = )LL_GPIO_OUTPUT_PUSHPULL/,
        replace: '$1(PORT0_OC ? LL_GPIO_OUTPUT_OPENDRAIN : LL_GPIO_OUTPUT_PUSHPULL)',
    },
    {
        file: 'router/Core/Src/main.c',
        search: /(PA2[\s\S]*?GPIO_InitStruct\.OutputType = )LL_GPIO_OUTPUT_PUSHPULL/,
        replace: '$1(PORT1_OC ? LL_GPIO_OUTPUT_OPENDRAIN : LL_GPIO_OUTPUT_PUSHPULL)',
    },
];
