#include <stdint.h>
#include "../kernel/fs/ext2.h"

typedef void (*vga_puts_t)(const char*);

extern ext2_filesystem_t fs;

void cmd_cd(const char *dirname, vga_puts_t vga_puts, void (*vga_putc)(char)) {
    int inode;
    int path_length;
    if (*dirname == '\0') {
        vga_puts("Usage: cd <dirname>\n");
        return;
    }

    inode = ext2_find_inode_by_path(dirname);

    if (inode < 0) {
        vga_puts("Directory not found\n");
        return;
    }

    ext2_inode_t inode_data;
    if (ext2_read_inode(inode, &inode_data) < 0) {
        vga_puts("Cannot read directory\n");
        return;
    }

    if ((inode_data.mode & EXT2_S_IFMT) != EXT2_S_IFDIR) {
        vga_puts("Not a directory\n");
        return;
    }

    fs.current_dir_inode = (uint32_t)inode;
    if (dirname[0] == '~') {
        for (path_length = 0; dirname[path_length] && path_length < 255; path_length++) {
            fs.current_path[path_length] = dirname[path_length];
        }
        fs.current_path[path_length] = '\0';
    } else if (dirname[0] == '/') {
        fs.current_path[0] = '~';
        for (path_length = 0; dirname[path_length] && path_length < 254; path_length++) {
            fs.current_path[path_length + 1] = dirname[path_length];
        }
        fs.current_path[path_length + 1] = '\0';
    } else if (dirname[0] == '.' && dirname[1] == '.' && dirname[2] == '\0') {
        path_length = 0;
        while (fs.current_path[path_length]) path_length++;
        while (path_length > 1 && fs.current_path[path_length - 1] != '/') path_length--;
        if (path_length > 1) fs.current_path[path_length - 1] = '\0';
    } else {
        path_length = 0;
        while (fs.current_path[path_length]) path_length++;
        if (path_length < 255) fs.current_path[path_length++] = '/';
        for (int i = 0; dirname[i] && path_length < 255; i++) {
            fs.current_path[path_length++] = dirname[i];
        }
        fs.current_path[path_length] = '\0';
    }
    vga_puts("Changed directory\n");
}
