#include <stdint.h>
#include "../kernel/core/process.h"

typedef void (*vga_puts_t)(const char*);
typedef void (*vga_putc_t)(char);

static void print_int(int value, vga_putc_t putc) {
    char digits[12];
    int length = 0;
    unsigned int magnitude = value < 0 ? (unsigned int)(-value) : (unsigned int)value;
    if (value < 0) putc('-');
    if (magnitude == 0) {
        putc('0');
        return;
    }
    while (magnitude > 0) {
        digits[length++] = (char)('0' + magnitude % 10);
        magnitude /= 10;
    }
    while (length > 0) putc(digits[--length]);
}

void cmd_run(const char *filename, vga_puts_t vga_puts, vga_putc_t vga_putc) {
    int status;

    if (*filename == '\0') {
        vga_puts("Usage: run <path>\n");
        return;
    }

    status = process_exec(filename);
    if (status < 0) {
        vga_puts("run: invalid ELF32 executable or insufficient memory\n");
        return;
    }
    vga_puts("Program exited with status ");
    print_int(status, vga_putc);
    vga_putc('\n');
}
