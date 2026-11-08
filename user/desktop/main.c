#include "desktop.h"
#include "../lib/api.h"

void _start(void) {
    user_api_init();
    if (desktop_run() < 0) syscall_invoke(SYS_EXIT, 1, 0, 0);
    syscall_invoke(SYS_EXIT, PIUX_EXIT_TO_RECOVERY_SHELL, 0, 0);
    for (;;) {
    }
}