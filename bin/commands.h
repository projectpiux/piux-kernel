#ifndef COMMANDS_H
#define COMMANDS_H

#include <stdint.h>

typedef void (*cmd_handler_t)(const char*, void (*)(const char*), void (*)(char));

typedef struct {
    const char *name;
    cmd_handler_t handler;
} command_t;

extern void cmd_help(const char *param, void (*vga_puts)(const char*), void (*vga_putc)(char));
extern void cmd_about(const char *param, void (*vga_puts)(const char*), void (*vga_putc)(char));
extern void cmd_ls(const char *param, void (*vga_puts)(const char*), void (*vga_putc)(char));
extern void cmd_cd(const char *param, void (*vga_puts)(const char*), void (*vga_putc)(char));
extern void cmd_cat(const char *param, void (*vga_puts)(const char*), void (*vga_putc)(char));
extern void cmd_clean(const char *param, void (*vga_puts)(const char*), void (*vga_putc)(char));
extern void cmd_fs(const char *param, void (*vga_puts)(const char*), void (*vga_putc)(char));
extern void cmd_touch(const char *param, void (*vga_puts)(const char*), void (*vga_putc)(char));
extern void cmd_mkdir(const char *param, void (*vga_puts)(const char*), void (*vga_putc)(char));
extern void cmd_nano(const char *param, void (*vga_puts)(const char*), void (*vga_putc)(char));
extern void cmd_echo(const char *param, void (*vga_puts)(const char*), void (*vga_putc)(char));
extern void cmd_shutdown(const char *param, void (*vga_puts)(const char*), void (*vga_putc)(char));
extern void cmd_reboot(const char *param, void (*vga_puts)(const char*), void (*vga_putc)(char));
extern void cmd_top(const char *param, void (*vga_puts)(const char*), void (*vga_putc)(char));
extern void cmd_run(const char *param, void (*vga_puts)(const char*), void (*vga_putc)(char));
extern void cmd_net(const char *param, void (*vga_puts)(const char*), void (*vga_putc)(char));
extern void cmd_pix(const char *param, void (*vga_puts)(const char*), void (*vga_putc)(char));
extern void cmd_numix(const char *, void (*)(const char *), void (*)(char));
extern void cmd_desktop(const char *param, void (*vga_puts)(const char*), void (*vga_putc)(char));

/*
 * Command table module ("commands").
 *
 * The table lives in the module image. The core does not link against the
 * table: it asks the module manager for the module's public entry point
 * (the first symbol exported through piux_module_exports).
 */
command_t *commands_lookup(const char *name);

/* Non-zero once the vfs module initialized its descriptor table. */
extern int vfs_module_ready;

#endif
