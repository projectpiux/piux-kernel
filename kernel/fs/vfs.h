#ifndef VFS_H
#define VFS_H

#include <stdint.h>

#define VFS_O_READ  0x01
#define VFS_O_WRITE 0x02
#define VFS_O_CREATE 0x04

#define VFS_FD_STDIN  0
#define VFS_FD_STDOUT 1
#define VFS_FD_STDERR 2
#define VFS_MAX_FDS 32

typedef void (*vfs_console_putc_t)(char character);

typedef enum {
    VFS_NODE_FILE,
    VFS_NODE_DIRECTORY,
    VFS_NODE_CHAR,
    VFS_NODE_BLOCK
} vfs_node_type_t;

int vfs_init(void);
int vfs_open(const char *path, int flags);
int vfs_read(int fd, void *buffer, uint32_t size);
int vfs_write(int fd, const void *buffer, uint32_t size);
int vfs_close(int fd);
int vfs_seek(int fd, uint32_t offset);
vfs_node_type_t vfs_node_type(const char *path);
vfs_console_putc_t vfs_set_console_output(vfs_console_putc_t putc);

#endif