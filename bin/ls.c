#include <stdint.h>
#include "../kernel/fs/vfs.h"
#include "../kernel/fs/ext2.h"
#include "../kernel/fs/ramfs.h"
#include "../kernel/fs/procfs.h"

typedef void (*vga_puts_t)(const char*);
typedef void (*vga_putc_t)(char);

extern ext2_filesystem_t fs;
extern int ramfs_find_file(const char *filename);

/*
 * ls goes through the VFS, so a path is resolved exactly like cat/read does:
 * device nodes under /dev, generated files under /proc and regular entries on
 * ext2 (or ramfs without a disk).
 */
void cmd_ls(const char *param, vga_puts_t vga_puts, vga_putc_t vga_putc) {
    const char *path = param[0] == '\0' ? "." : param;
    (void)vga_putc;
    vga_puts("Files in system:\n");

    if (path[0] == '/' && path[1] == 'd' && path[2] == 'e' && path[3] == 'v' && path[4] == '\0') {
        vga_puts("[CHR]  console\n");
        vga_puts("[CHR]  tty\n");
        vga_puts("[CHR]  stdin\n");
        vga_puts("[CHR]  stdout\n");
        vga_puts("[CHR]  stderr\n");
        vga_puts("[CHR]  keyboard\n");
        vga_puts("[CHR]  null\n");
        vga_puts("[CHR]  zero\n");
        vga_puts("[BLK]  hda\n");
        return;
    }

    if (procfs_is_proc_path(path)) {
        if (procfs_node_type(path) == VFS_NODE_DIRECTORY) {
            procfs_list(vga_puts);
            return;
        }
    }

    if (ext2_is_mounted()) {
        if (ext2_print_dir_by_path(path, vga_puts) < 0) vga_puts("Directory not found\n");
    } else if (param[0] == '\0') {
        ramfs_list_files(vga_puts);
    } else {
        vga_puts("Paths require an ext2 filesystem\n");
    }
}
