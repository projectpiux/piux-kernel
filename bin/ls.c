#include <stdint.h>
#include "../kernel/fs/ext2.h"

typedef void (*vga_puts_t)(const char*);
typedef void (*vga_putc_t)(char);

extern void ramfs_list_files(void (*vga_puts)(const char*));
extern ext2_filesystem_t fs;

void cmd_ls(const char *param, vga_puts_t vga_puts, vga_putc_t vga_putc) {
    const char *path = param[0] == '\0' ? "." : param;
    (void)vga_putc;
    vga_puts("Files in system:\n");
    if (ext2_is_mounted()) {
        if (ext2_print_dir_by_path(path, vga_puts) < 0) vga_puts("Directory not found\n");
    } else if (param[0] == '\0') {
        ramfs_list_files(vga_puts);
    } else {
        vga_puts("Paths require an ext2 filesystem\n");
    }
}
