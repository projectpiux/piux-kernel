#include <stdint.h>
#include "../kernel/fs/vfs.h"

typedef void (*vga_puts_t)(const char *);
typedef void (*vga_putc_t)(char);

#define CAT_BUFFER_SIZE 512

static char cat_buffer[CAT_BUFFER_SIZE];

/*
 * cat is a pure VFS client: open/read/close. That makes it work on regular
 * files, device nodes (/dev/zero, /dev/hda) and generated files (/proc/uptime),
 * exactly the "everything is a file" contract.
 */
void cmd_cat(const char *filename, vga_puts_t vga_puts, vga_putc_t vga_putc) {
    int fd;
    int bytes = 0;
    int total = 0;
    int regular = 0;
    char last = 0;

    if (filename == 0 || filename[0] == '\0') {
        vga_puts("usage: cat <filename>\n");
        return;
    }

    fd = vfs_open(filename, VFS_O_READ);
    if (fd < 0) {
        vga_puts("File not found\n");
        return;
    }
    regular = vfs_is_regular(fd);

    for (;;) {
        bytes = vfs_read(fd, cat_buffer, CAT_BUFFER_SIZE);
        if (bytes < 0) {
            vga_puts("read error\n");
            vfs_close(fd);
            return;
        }
        if (bytes == 0) break;
        for (int i = 0; i < bytes; i++) vga_putc(cat_buffer[i]);
        last = cat_buffer[bytes - 1];
        total += bytes;
        /* Character devices may never report EOF: stop on a short read. */
        if (bytes < CAT_BUFFER_SIZE && !regular) break;
    }
    vfs_close(fd);

    if (total == 0) vga_puts("(empty file)\n");
    else if (last != '\n') vga_putc('\n');
}
