#include <stdint.h>
#include "../kernel/fs/ext2.h"

typedef void (*vga_puts_t)(const char*);
typedef void (*vga_putc_t)(char);

extern int ramfs_create_file(const char *filename);

void cmd_touch(const char *filename, vga_puts_t vga_puts, vga_putc_t vga_putc) {
    if (*filename == '\0') {
        vga_puts("Usage: touch <filename>\n");
        return;
    }

    int result = ext2_is_mounted() ? ext2_create_file_by_path(filename) : ramfs_create_file(filename);
    if (result >= 0) {
        vga_puts("File created: ");
        vga_puts(filename);
        vga_puts("\n");
    } else {
        vga_puts("Error creating file\n");
    }
}
