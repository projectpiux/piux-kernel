#include <stdint.h>
#include "../kernel/fs/vfs.h"

typedef void (*vga_puts_t)(const char*);
typedef void (*vga_putc_t)(char);

/*
 * touch creates the file through the VFS; if the VFS refuses the create (for
 * instance on a read-only namespace such as /proc) it falls back to opening the
 * path read-only to report whether it exists already.
 */
void cmd_touch(const char *filename, vga_puts_t vga_puts, vga_putc_t vga_putc) {
    int fd;
    (void)vga_putc;
    if (*filename == '\0') {
        vga_puts("Usage: touch <filename>\n");
        return;
    }

    fd = vfs_open(filename, VFS_O_WRITE | VFS_O_CREATE);
    if (fd < 0) {
        int existing = vfs_open(filename, VFS_O_READ);
        if (existing >= 0) {
            vfs_close(existing);
            vga_puts("File already exists: ");
            vga_puts(filename);
            vga_puts("\n");
        } else {
            vga_puts("Error creating file\n");
        }
        return;
    }
    vfs_close(fd);
    vga_puts("File created: ");
    vga_puts(filename);
    vga_puts("\n");
}
