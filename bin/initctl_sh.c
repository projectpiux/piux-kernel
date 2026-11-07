#include <stdint.h>
#include "../kernel/process.h"

typedef void (*vga_puts_t)(const char *);
typedef void (*vga_putc_t)(char);

void cmd_initctl(const char *param, vga_puts_t vga_puts, vga_putc_t vga_putc) {
    (void)vga_putc;
    char command[PROCESS_ARGS_SIZE];
    int length = 0;
    if (param) {
        while (param[length] && length < (int)sizeof(command) - 1) {
            command[length] = param[length];
            length++;
        }
    }
    command[length] = '\0';
    if (!length) {
        vga_puts("Usage: initctl <start|stop|restart|reload|status> [service]\n");
        return;
    }
    int status = process_spawn_and_wait("/usr/bin/initctl", command);
    if (status < 0) vga_puts("initctl: cannot start the initctl client\n");
}
