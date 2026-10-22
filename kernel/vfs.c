#include <stdint.h>
#include "vfs.h"
#include "ext2.h"
#include "ata.h"
#include "keyboard.h"
#include "auth.h"

typedef int (*vfs_read_fn)(void *, uint32_t, uint32_t, uint32_t *);
typedef int (*vfs_write_fn)(const void *, uint32_t, uint32_t, uint32_t *);

typedef struct {
    const char *path;
    vfs_node_type_t type;
    vfs_read_fn read;
    vfs_write_fn write;
} vfs_device_t;

typedef struct {
    int used;
    int flags;
    uint32_t offset;
    char path[256];
} vfs_fd_t;

extern void vga_putc(char c);
extern int ramfs_create_file(const char *filename);
extern int ramfs_read_file(const char *filename, char *buffer, uint32_t size);
extern int ramfs_write_file(const char *filename, const char *data, uint32_t size);
extern int ramfs_find_file(const char *filename);

static vfs_fd_t descriptors[VFS_MAX_FDS];
static vfs_console_putc_t console_output;

static int has_permission(const char *path, int flags) {
    uint16_t mode = 0644;
    uint32_t owner = auth_current_uid();
    ext2_inode_t inode;
    int inode_number;
    if (owner == 0) return 1;
    if (ext2_is_mounted()) {
        inode_number = ext2_find_inode_by_path(path);
        if (inode_number >= 0 && ext2_read_inode((uint32_t)inode_number, &inode) == 0) {
            mode = inode.mode & 0777;
            owner = inode.uid == auth_current_uid() ? 1 : 0;
        } else if (inode_number < 0) {
            return 0;
        }
    }
    if (owner) {
        if ((flags & VFS_O_READ) && !(mode & 0400)) return 0;
        if ((flags & VFS_O_WRITE) && !(mode & 0200)) return 0;
    } else {
        if ((flags & VFS_O_READ) && !(mode & 0004)) return 0;
        if ((flags & VFS_O_WRITE) && !(mode & 0002)) return 0;
    }
    return 1;
}

static int path_equals(const char *left, const char *right) {
    int index = 0;
    while (left[index] && right[index] && left[index] == right[index]) index++;
    return left[index] == right[index];
}

static void copy_path(char *destination, const char *source) {
    int index = 0;
    while (source[index] && index < 255) {
        destination[index] = source[index];
        index++;
    }
    destination[index] = '\0';
}

static int console_read(void *buffer, uint32_t size, uint32_t offset, uint32_t *done) {
    char *bytes = buffer;
    (void)offset;
    *done = 0;
    while (*done < size) {
        int character = keyboard_try_read_char();
        if (character < 0) break;
        bytes[(*done)++] = (char)character;
    }
    return 0;
}

static int console_write(const void *buffer, uint32_t size, uint32_t offset, uint32_t *done) {
    const char *bytes = buffer;
    (void)offset;
    for (*done = 0; *done < size; (*done)++) {
        if (console_output) console_output(bytes[*done]);
        else vga_putc(bytes[*done]);
    }
    return 0;
}

vfs_console_putc_t vfs_set_console_output(vfs_console_putc_t putc) {
    vfs_console_putc_t previous = console_output;
    console_output = putc;
    return previous;
}

static int null_read(void *buffer, uint32_t size, uint32_t offset, uint32_t *done) {
    (void)buffer;
    (void)size;
    (void)offset;
    *done = 0;
    return 0;
}

static int null_write(const void *buffer, uint32_t size, uint32_t offset, uint32_t *done) {
    (void)buffer;
    (void)offset;
    *done = size;
    return 0;
}

static int disk_read(void *buffer, uint32_t size, uint32_t offset, uint32_t *done) {
    if (offset % 512) return -1;
    uint32_t sectors = (size + 511) / 512;
    if (sectors == 0 || ata_read_sectors(offset / 512, sectors, buffer) < 0) return -1;
    *done = sectors * 512;
    return 0;
}

static int disk_write(const void *buffer, uint32_t size, uint32_t offset, uint32_t *done) {
    if (offset % 512) return -1;
    uint32_t sectors = (size + 511) / 512;
    if (sectors == 0 || ata_write_sectors(offset / 512, sectors, (uint8_t *)buffer) < 0) return -1;
    *done = sectors * 512;
    return 0;
}

static const vfs_device_t devices[] = {
    { "/dev/console", VFS_NODE_CHAR, console_read, console_write },
    { "/dev/keyboard", VFS_NODE_CHAR, console_read, 0 },
    { "/dev/null", VFS_NODE_CHAR, null_read, null_write },
    { "/dev/hda", VFS_NODE_BLOCK, disk_read, disk_write },
    { 0, 0, 0, 0 }
};

static const vfs_device_t *find_device(const char *path) {
    for (int index = 0; devices[index].path; index++) {
        if (path_equals(path, devices[index].path)) return &devices[index];
    }
    return 0;
}

static int is_directory(const char *path) {
    if (path_equals(path, "/dev")) return 1;
    return ext2_find_inode_by_path(path) >= 0;
}

int vfs_init(void) {
    console_output = 0;
    for (int index = 0; index < VFS_MAX_FDS; index++) descriptors[index].used = 0;
    descriptors[VFS_FD_STDIN].used = 1;
    descriptors[VFS_FD_STDIN].flags = VFS_O_READ;
    copy_path(descriptors[VFS_FD_STDIN].path, "/dev/console");
    descriptors[VFS_FD_STDOUT].used = 1;
    descriptors[VFS_FD_STDOUT].flags = VFS_O_WRITE;
    copy_path(descriptors[VFS_FD_STDOUT].path, "/dev/console");
    descriptors[VFS_FD_STDERR] = descriptors[VFS_FD_STDOUT];
    return 0;
}

int vfs_open(const char *path, int flags) {
    const vfs_device_t *device = find_device(path);
    int regular_file = ext2_is_mounted() ? ext2_find_inode_by_path(path) >= 0 :
                                          ramfs_find_file(path) >= 0;
    if (!device && !regular_file && !(flags & VFS_O_CREATE)) return -1;
    if (!device && !regular_file && (flags & VFS_O_CREATE)) {
        if (ext2_is_mounted() ? ext2_create_file_by_path(path) < 0 : ramfs_create_file(path) < 0) return -1;
    }
    if (!has_permission(path, flags)) return -1;

    for (int fd = 3; fd < VFS_MAX_FDS; fd++) {
        if (!descriptors[fd].used) {
            descriptors[fd].used = 1;
            descriptors[fd].flags = flags;
            descriptors[fd].offset = 0;
            copy_path(descriptors[fd].path, path);
            return fd;
        }
    }
    return -1;
}

int vfs_read(int fd, void *buffer, uint32_t size) {
    if (fd < 0 || fd >= VFS_MAX_FDS || !descriptors[fd].used || !(descriptors[fd].flags & VFS_O_READ)) return -1;
    const vfs_device_t *device = find_device(descriptors[fd].path);
    uint32_t done = 0;
    int result;
    if (device) {
        result = device->read ? device->read(buffer, size, descriptors[fd].offset, &done) : -1;
    } else {
        char file_buffer[8192];
        int file_size = ext2_is_mounted() ? ext2_read_file_by_path(descriptors[fd].path, file_buffer, sizeof(file_buffer)) :
                                           ramfs_read_file(descriptors[fd].path, file_buffer, sizeof(file_buffer));
        if (file_size < 0 || descriptors[fd].offset >= (uint32_t)file_size) return 0;
        done = (uint32_t)file_size - descriptors[fd].offset;
        if (done > size) done = size;
        for (uint32_t index = 0; index < done; index++) {
            ((char *)buffer)[index] = file_buffer[descriptors[fd].offset + index];
        }
        result = 0;
    }
    if (result < 0) return result;
    descriptors[fd].offset += device ? done : (uint32_t)result;
    return device ? (int)done : result;
}

int vfs_write(int fd, const void *buffer, uint32_t size) {
    if (fd < 0 || fd >= VFS_MAX_FDS || !descriptors[fd].used || !(descriptors[fd].flags & VFS_O_WRITE)) return -1;
    const vfs_device_t *device = find_device(descriptors[fd].path);
    uint32_t done = 0;
    int result;
    if (device) {
        result = device->write ? device->write(buffer, size, descriptors[fd].offset, &done) : -1;
    } else if (descriptors[fd].offset == 0) {
        result = ext2_is_mounted() ? ext2_write_file_by_path(descriptors[fd].path, buffer, size) :
                                     ramfs_write_file(descriptors[fd].path, buffer, size);
    } else {
        return -1;
    }
    if (result < 0) return result;
    descriptors[fd].offset += device ? done : (uint32_t)result;
    return device ? (int)done : result;
}

int vfs_close(int fd) {
    if (fd < 3 || fd >= VFS_MAX_FDS || !descriptors[fd].used) return -1;
    descriptors[fd].used = 0;
    return 0;
}

int vfs_seek(int fd, uint32_t offset) {
    if (fd < 0 || fd >= VFS_MAX_FDS || !descriptors[fd].used) return -1;
    descriptors[fd].offset = offset;
    return 0;
}

vfs_node_type_t vfs_node_type(const char *path) {
    const vfs_device_t *device = find_device(path);
    if (device) return device->type;
    if (is_directory(path)) return VFS_NODE_DIRECTORY;
    return VFS_NODE_FILE;
}