#include <stdint.h>
#include "../tui/wm/wm.h"
#include "ext2.h"

typedef void (*vga_puts_t)(const char *);
typedef void (*vga_putc_t)(char);

#define CAT_BUFFER_SIZE 4096

extern int ext2_is_mounted(void);
extern int ext2_find_inode_by_path(const char *path);
extern int ext2_read_file_by_path(const char *path, char *buffer, uint32_t size);
extern int ramfs_find_file(const char *filename);
extern int ramfs_read_file(const char *filename, char *buffer, uint32_t max_size);

static char cat_buffer[CAT_BUFFER_SIZE];

void cmd_cat(const char *filename, vga_puts_t vga_puts, vga_putc_t vga_putc) {
    if (filename == 0 || filename[0] == '\0') {
        vga_puts("usage: cat <filename>\n");
        return;
    }

    int use_ext2 = ext2_is_mounted();
    int exists = use_ext2 ? (ext2_find_inode_by_path(filename) >= 0)
                          : (ramfs_find_file(filename) >= 0);

    if (!exists) {
        vga_puts("File not found\n");
        return;
    }

    int bytes = use_ext2 ? ext2_read_file_by_path(filename, cat_buffer, CAT_BUFFER_SIZE)
                         : ramfs_read_file(filename, cat_buffer, CAT_BUFFER_SIZE);

    if (bytes < 0) {
        vga_puts("read error\n");
        return;
    }
    if (bytes > CAT_BUFFER_SIZE) bytes = CAT_BUFFER_SIZE;

    for (int i = 0; i < bytes; i++) vga_putc(cat_buffer[i]);

    if (bytes == 0) vga_puts("(empty file)\n");
    else if (cat_buffer[bytes - 1] != '\n') vga_putc('\n');

    if (pwm_command_is_active()) pwm_command_refresh();
}