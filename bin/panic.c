#include "../kernel/core/panic.h"

typedef void (*vga_puts_t)(const char*);
typedef void (*vga_putc_t)(char);

/*
 * `panic [message]` - deliberately crash the kernel through the panic path.
 * Useful to exercise the full-screen panic report, e.g. after attaching new
 * hardware or a module that is known to be unstable.
 */
void cmd_panic(const char *param, vga_puts_t vga_puts, vga_putc_t vga_putc) {
    (void)vga_putc;
    while (param && (*param == ' ' || *param == '\t')) param++;
    vga_puts("Triggering a kernel panic on request...\n");
    kernel_panic_msg(param && *param ? param : "kernel panic requested from the shell");
}
