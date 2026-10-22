#ifndef COMMANDS_H
#define COMMANDS_H

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
extern void cmd_pwm(const char *param, void (*vga_puts)(const char*), void (*vga_putc)(char));
extern void cmd_top(const char *param, void (*vga_puts)(const char*), void (*vga_putc)(char));
extern void cmd_run(const char *param, void (*vga_puts)(const char*), void (*vga_putc)(char));
extern void cmd_net(const char *param, void (*vga_puts)(const char*), void (*vga_putc)(char));
extern void cmd_pix(const char *param, void (*vga_puts)(const char*), void (*vga_putc)(char));
extern void cmd_numix(const char *, void (*)(const char *), void (*)(char));
extern void cmd_desktop(const char *param, void (*vga_puts)(const char*), void (*vga_putc)(char));

static command_t commands[] = {
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
    { "pwm", cmd_pwm },
    { "pmw", cmd_pwm },
    { "top", cmd_top },
    { "run", cmd_run },
    { "pde", cmd_desktop },
    { "desktop", cmd_desktop },
    { "net", cmd_net },
    { "pix", cmd_pix },
    { "numix", cmd_numix },
    { 0, 0 }
};

#endif
