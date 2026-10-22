#include <stdint.h>

typedef void (*vga_puts_t)(const char*);

void cmd_help(const char *param, vga_puts_t vga_puts, void (*vga_putc)(char)) {
    vga_puts("Available commands:\n");
    vga_puts("  help   - Show this help message\n");
    vga_puts("  about  - About Piux\n");
    vga_puts("  ls     - List directory\n");
    vga_puts("  cd     - Change directory\n");
    vga_puts("  cat    - Read file\n");
    vga_puts("  clear  - Clear screen\n");
    vga_puts("  top    - Show RAM and disk resources\n");
    vga_puts("  fs     - Show filesystem info\n");
    vga_puts("  touch  - Create new file\n");
    vga_puts("  mkdir  - Create directory\n");
    vga_puts("  nano   - Edit file\n");
    vga_puts("  numix  - Calculator app\n");
    vga_puts("  pwm    - Control the pWM window manager (also: pmw)\n");
    vga_puts("  pde    - Control the pDE desktop (also: desktop)\n");
    vga_puts("  echo   - Print arguments\n");
    vga_puts("  shutdown - Shutdown in 60 seconds, or use 'shutdown now'\n");
    vga_puts("  reboot - Reboot immediately\n");
}
