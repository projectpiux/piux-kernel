#include <stdint.h>

static inline void exit_process(uint32_t status) {
    asm volatile ("int $0x80"
                  :
                  : "a"(8U), "b"(status), "c"(0U), "d"(0U)
                  : "memory");
}

void _start(void) {
    volatile uint32_t value = *(volatile uint32_t *)(uintptr_t)0x00100000U;
    exit_process(value ? 99U : 98U);
    for (;;) {
    }
}