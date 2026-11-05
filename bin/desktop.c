#include "process.h"

typedef void (*desktop_puts_t)(const char *);
typedef void (*desktop_putc_t)(char);

static int matches(const char *value, const char *expected) {
    int index = 0;
    while (value[index] && expected[index] && value[index] == expected[index]) index++;
    return value[index] == expected[index];
}

static void print_help(desktop_puts_t puts) {
    puts("Usage: pde [--help|--info|--reload|--start|--stop]\n");
    puts("  --help     Show this help message\n");
    puts("  --info     Show pDE status and controls\n");
    puts("  --reload   Close the Start menu and redraw pDE\n");
    puts("  --start    Start pDE from the minimal shell (default)\n");
    puts("  --stop     Stop pDE and return to the minimal shell\n");
}

static void print_info(desktop_puts_t puts) {
    puts("pDE graphical desktop\n\n");
    puts("Controls:\n");
    puts("  Mouse        Move windows by title, resize from corners\n");
    puts("  Start        Open the Start menu (or Ctrl+Q)\n");
    puts("  _ / X        Minimize / close a window\n");
    puts("  Ctrl+Alt+Q   Exit pDE\n\n");
    puts("Commands:\n");
    puts("  pde --help   Show command usage\n");
    puts("  pde --info   Show this information\n");
    puts("  pde --reload Redraw the desktop\n");
    puts("  pde --start  Start pDE from the minimal shell\n");
    puts("  pde --stop   Stop pDE and return to the minimal shell\n\n");
    puts("v1.0.0 BETA!\n");
}

void cmd_desktop(const char *param, desktop_puts_t puts, desktop_putc_t putc) {
    (void)putc;
    if (matches(param, "--help")) {
        print_help(puts);
        return;
    }
    if (matches(param, "--info")) {
        print_info(puts);
        puts("Runs as /desktop.elf in user mode.\n");
        return;
    }
    if (matches(param, "--reload")) {
        puts("pDE redraws automatically; use Ctrl+Alt+Q to exit.\n");
        return;
    }
    if (matches(param, "--stop")) {
        puts("Use Ctrl+Alt+Q from the desktop to return to the shell.\n");
        return;
    }
    if (param[0] == '\0' || matches(param, "--start")) {
        if (process_exec("/desktop.elf") < 0) puts("Unable to start /desktop.elf\n");
        return;
    }
    puts("Unknown pde option: ");
    puts(param);
    puts("\n\n");
    print_help(puts);
}
