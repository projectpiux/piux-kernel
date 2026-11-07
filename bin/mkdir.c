#include <stdint.h>
#include "../kernel/fs/ext2.h"

typedef void (*vga_puts_t)(const char*);
typedef void (*vga_putc_t)(char);

void cmd_mkdir(const char *dirname, vga_puts_t vga_puts, vga_putc_t vga_putc) {
    (void)vga_putc;
    if (*dirname == '\0') {
        vga_puts("Usage: mkdir <dirname>\n");
        return;
    }
    if (!ext2_is_mounted()) {
        vga_puts("mkdir requires an ext2 filesystem\n");
        return;
    }
    if (ext2_create_directory_by_path(dirname) < 0) {
        vga_puts("Error creating directory\n");
        return;
    }
    vga_puts("Directory created: ");
    vga_puts(dirname);
    vga_puts("\n");
}