
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>


int main() {
    return 0;
}

extern "C" {
    void Reset_Handler() {
        main();
        while (1);
    }
    void NMI_Handler() {
        while (1);
    }
    void HardFault_Handler() {
        while (1);
    }
    void * const isr_vector[] __attribute__((section(".isr_vector"), used)) = {
        (void *) 0x20001000,   // Initial stack pointer (example)
        (void *) Reset_Handler,
        (void *) NMI_Handler,
        (void *) HardFault_Handler,
    };
}
