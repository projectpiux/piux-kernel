#include "commands.h"
#include "../kernel/piux_module.h"

#include "../kernel/ext2.h"
#include "../kernel/system.h"
#include "../kernel/process.h"
#include "../kernel/vfs.h"
#include "../kernel/keyboard.h"

#ifndef PIUX_MODULE_COMMANDS
#define PIUX_MODULE_COMMANDS
#endif

static const command_t commands[] = {
    { "help", cmd_help },
    { "about", cmd_about },
    { "ls", cmd_ls },
    { "cd", cmd_cd },
    { "cat", cmd_cat },
    { "clear", cmd_clean },
    { "fs", cmd_fs },
    { "touch", cmd_touch },
    { "mkdir", cmd_mkdir },
    { "nano", cmd_nano },
    { "echo", cmd_echo },
    { "shutdown", cmd_shutdown },
    { "reboot", cmd_reboot },
    { "top", cmd_top },
    { "run", cmd_run },
    { "initctl", cmd_initctl },
    { "pde", cmd_desktop },
    { "desktop", cmd_desktop },
    { "net", cmd_net },
    { "pix", cmd_pix },
    { "numix", cmd_numix },
    { 0, 0 }
};

static int names_equal(const char *left, const char *right) {
    while (*left && *left == *right) { left++; right++; }
    return *left == *right;
}

command_t *commands_lookup(const char *name) {
    if (!name) return 0;
    for (int index = 0; commands[index].name; index++) {
        if (names_equal(commands[index].name, name)) return (command_t *)&commands[index];
    }
    return 0;
}

void piux_module_exports(const char *const **names, const void *const **addresses,
                         uint32_t *count) {
    static const char *const export_names[] = { "commands_lookup" };
    static const void *const export_addresses[] = { (const void *)(uintptr_t)&commands_lookup };
    *names = export_names;
    *addresses = export_addresses;
    *count = 1;
}

static int commands_module_init(void) {
    return commands_lookup("help") ? 0 : -1;
}

PIUX_MODULE(
    .name = "commands",
    .version = PIUX_MODULE_VERSION,
    .deps = (const char *const[]){ "vfs" },
    .dep_count = 1,
    .init = commands_module_init,
    .description = "Native shell applications (ls, cat, nano, net, pix, ...)"
);
