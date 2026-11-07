#include "../kernel/core/io.h"

typedef void (*vga_puts_t)(const char*);
typedef void (*vga_putc_t)(char);

void cmd_reboot(const char *param, vga_puts_t vga_puts, vga_putc_t vga_putc) {
    (void)param;
    (void)vga_putc;
    vga_puts("Rebooting...\n");

    while (inb(0x64) & 0x02) {
    }
    outb(0x64, 0xFE);

    for (;;) {
        asm volatile ("cli; hlt");
    }
}
