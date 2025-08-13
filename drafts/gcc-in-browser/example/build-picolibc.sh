#!/bin/bash
set -e
cd "$(dirname "${BASH_SOURCE[0]}")"

mkdir -p build

arm-none-eabi-g++ \
    -c main.cpp -mcpu=cortex-m4 -std=gnu++14 -Os \
    -ffunction-sections -fdata-sections -fno-exceptions -fno-rtti \
    -fno-use-cxa-atexit -Wall -fstack-usage \
    --specs=picolibcpp.specs -mfpu=fpv4-sp-d16 -mfloat-abi=hard -mthumb \
    -o build/main.o

arm-none-eabi-g++ \
    -o build/test.elf build/main.o -mcpu=cortex-m4 -Tlinker.ld \
    -Wl,-Map=build/test.map -Wl,--gc-sections \
    -static --specs=picolibcpp.specs -mfpu=fpv4-sp-d16 -mfloat-abi=hard -mthumb \
    -Wl,--start-group -lc -lm -lstdc++ -lsupc++ -Wl,--end-group

arm-none-eabi-size   build/test.elf
arm-none-eabi-objdump -h -S  build/test.elf  > "build/test.list"
