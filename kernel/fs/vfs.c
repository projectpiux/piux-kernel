#include <stdint.h>

#if defined(PIUX_MODULE_VFS)

#include "vfs.h"
#include "../core/vfs_module.h"
#include "ext2.h"
#include "ata.h"
#include "procfs.h"
#include "../core/keyboard.h"
#include "../core/auth.h"
#include "../core/timer.h"
#include "../core/process.h"
#include "../core/system.h"
#include "../core/vga_log.h"
#include "../drivers/rtl8139.h"
#include "../net/ip.h"
#include "../core/io.h"
#include "../core/piux_module.h"

#define PIUX_EXPORT(symbol) ((const void *)(uintptr_t)(symbol))

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

typedef struct {
    const char *source;
    const char *target;
    const char *type;
    const char *options;
} vfs_mount_t;

extern void vga_putc(char c);
extern int vfs_module_ready;
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

/* ------------------------------------------------------------------------- */
/* Minimal formatted text builder, used by the generated /proc files.        */
/* ------------------------------------------------------------------------- */

typedef struct {
    char *buffer;
    uint32_t capacity;
    uint32_t length;
} text_buffer_t;

static void text_clear(text_buffer_t *text) {
    text->length = 0;
    if (text->capacity) text->buffer[0] = '\0';
}

static void text_putc(text_buffer_t *text, char character) {
    if (text->length + 1 >= text->capacity) return;
    text->buffer[text->length++] = character;
    text->buffer[text->length] = '\0';
}

static void text_puts(text_buffer_t *text, const char *string) {
    if (!string) return;
    while (*string) text_putc(text, *string++);
}

static void text_put_u32(text_buffer_t *text, uint32_t value) {
    char digits[11];
    int count = 0;
    if (value == 0) { text_putc(text, '0'); return; }
    while (value && count < 10) {
        digits[count++] = (char)('0' + value % 10);
        value /= 10;
    }
    while (count > 0) text_putc(text, digits[--count]);
}

static void text_put_hex8(text_buffer_t *text, uint8_t value, int upper) {
    const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    text_putc(text, digits[(value >> 4) & 0xf]);
    text_putc(text, digits[value & 0xf]);
}

static void text_put_hex32(text_buffer_t *text, uint32_t value) {
    for (int shift = 28; shift >= 0; shift -= 4) {
        text_putc(text, "0123456789abcdef"[(value >> shift) & 0xf]);
    }
}

static void text_put_ip(text_buffer_t *text, uint32_t address) {
    text_put_u32(text, (address >> 24) & 0xff);
    text_putc(text, '.');
    text_put_u32(text, (address >> 16) & 0xff);
    text_putc(text, '.');
    text_put_u32(text, (address >> 8) & 0xff);
    text_putc(text, '.');
    text_put_u32(text, address & 0xff);
}

static const char *process_state_text(process_state_t state) {
    switch (state) {
        case PROCESS_RUNNING: return "R (running)";
        case PROCESS_READY: return "R (ready)";
        case PROCESS_BLOCKED: return "S (blocked)";
        case PROCESS_ZOMBIE: return "Z (zombie)";
        default: return "?";
    }
}

/* ------------------------------------------------------------------------- */
/* Device nodes: the console, the raw disk and a few system pseudo-files.    */
/* ------------------------------------------------------------------------- */

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

static int zero_read(void *buffer, uint32_t size, uint32_t offset, uint32_t *done) {
    (void)offset;
    memset_safe(buffer, 0, size);
    *done = size;
    return 0;
}

static int zero_write(const void *buffer, uint32_t size, uint32_t offset, uint32_t *done) {
    (void)buffer;
    (void)offset;
    *done = size;
    return 0;
}

static int disk_read(void *buffer, uint32_t size, uint32_t offset, uint32_t *done) {
    if (offset % 512) return -1;
    uint32_t sectors = (size + 511) / 512;
    if (sectors == 0 || ata_read_sectors(offset / 512, sectors, buffer) < 0) return -1;
    *done = size;
    return 0;
}

static int disk_write(const void *buffer, uint32_t size, uint32_t offset, uint32_t *done) {
    if (offset % 512) return -1;
    uint32_t sectors = (size + 511) / 512;
    if (sectors == 0 || ata_write_sectors(offset / 512, sectors, (uint8_t *)buffer) < 0) return -1;
    *done = size;
    return 0;
}

/*
 * /proc has one writer in the kernel side (procfs.c): it regenerates the whole
 * snapshot inside the descriptor on every read, so a partial read always sees a
 * coherent file and stat-like entries never go stale. See procfs.c.
 */

static const vfs_device_t devices[] = {
    { "/dev/console", VFS_NODE_CHAR, console_read, console_write },
    { "/dev/tty", VFS_NODE_CHAR, console_read, console_write },
    { "/dev/stdin", VFS_NODE_CHAR, console_read, 0 },
    { "/dev/stdout", VFS_NODE_CHAR, 0, console_write },
    { "/dev/stderr", VFS_NODE_CHAR, 0, console_write },
    { "/dev/keyboard", VFS_NODE_CHAR, console_read, 0 },
    { "/dev/null", VFS_NODE_CHAR, null_read, null_write },
    { "/dev/zero", VFS_NODE_CHAR, zero_read, zero_write },
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
    if (procfs_is_proc_path(path) && procfs_node_type(path) == VFS_NODE_DIRECTORY) return 1;
    return ext2_find_inode_by_path(path) >= 0;
}

/* ------------------------------------------------------------------------- */
/* Generated files (procfs hook).                                            */
/* ------------------------------------------------------------------------- */

static int write_uptime(void *file) {
    text_buffer_t text = { procfs_file_buffer(file), PROC_BUFFER_SIZE, 0 };
    uint32_t ticks = timer_ticks();
    text_clear(&text);
    text_put_u32(&text, ticks / 100);
    text_putc(&text, '.');
    text_put_u32(&text, (ticks % 100) / 10);
    text_put_u32(&text, ticks % 10);
    text_puts(&text, " ");
    text_put_u32(&text, ticks / 100);
    text_puts(&text, ".00\n");
    return 0;
}

static int write_meminfo(void *file) {
    text_buffer_t text = { procfs_file_buffer(file), PROC_BUFFER_SIZE, 0 };
    text_clear(&text);
    text_puts(&text, "MemTotal:       ");
    text_put_u32(&text, system_memory_total_kb);
    text_puts(&text, " kB\nMemFree:        ");
    text_put_u32(&text, system_memory_free_kb);
    text_puts(&text, " kB\nKernelText:     ");
    text_put_u32(&text, system_memory_used_kb);
    text_puts(&text, " kB\nLowTotal:       ");
    text_put_u32(&text, system_memory_lower_kb);
    text_puts(&text, " kB\nHighTotal:      ");
    text_put_u32(&text, system_memory_upper_kb);
    text_puts(&text, " kB\n");
    return 0;
}

static int write_version(void *file) {
    text_buffer_t text = { procfs_file_buffer(file), PROC_BUFFER_SIZE, 0 };
    text_clear(&text);
    text_puts(&text, "Piux Rolling release (piux-1.0) ");
    text_puts(&text, "i386 gcc/syscall-int80\n");
    return 0;
}

static int write_cmdline(void *file) {
    text_buffer_t text = { procfs_file_buffer(file), PROC_BUFFER_SIZE, 0 };
    text_clear(&text);
    text_puts(&text, "root=/dev/hda filesystem=ext2 init=/sbin/init.elf\n");
    return 0;
}

static int write_uname(void *file) {
    text_buffer_t text = { procfs_file_buffer(file), PROC_BUFFER_SIZE, 0 };
    text_clear(&text);
    text_puts(&text, "sysname=Piux\nnodename=piux\nrelease=1.0\nmachine=i386\n");
    return 0;
}

static int write_netdev(void *file) {
    text_buffer_t text = { procfs_file_buffer(file), PROC_BUFFER_SIZE, 0 };
    uint8_t mac[6];
    text_clear(&text);
    if (!rtl8139_is_ready()) {
        text_puts(&text, "Inter-|   Receive |  Transmit\n face |bytes packets|bytes packets\n");
        return 0;
    }
    rtl8139_get_mac(mac);
    text_puts(&text, "eth0 ");
    for (int index = 0; index < 6; index++) text_put_hex8(&text, mac[index], 0);
    text_puts(&text, " ip=");
    text_put_ip(&text, ip_get_local_ip());
    text_puts(&text, " mask=");
    text_put_ip(&text, ip_get_netmask());
    text_puts(&text, " gw=");
    text_put_ip(&text, ip_get_gateway());
    text_puts(&text, " dns=");
    text_put_ip(&text, ip_get_dns());
    text_puts(&text, "\n");
    return 0;
}

static int write_routes(void *file) {
    text_buffer_t text = { procfs_file_buffer(file), PROC_BUFFER_SIZE, 0 };
    text_clear(&text);
    if (!rtl8139_is_ready()) return 0;
    text_puts(&text, "Iface\tDestination\tGateway\tMask\tFlags\neth0\t0.0.0.0\t");
    text_put_ip(&text, ip_get_gateway());
    text_puts(&text, "\t0.0.0.0\t0003\n");
    return 0;
}

static int write_mounts(void *file) {
    text_buffer_t text = { procfs_file_buffer(file), PROC_BUFFER_SIZE, 0 };
    text_clear(&text);
    if (ext2_is_mounted()) text_puts(&text, "/dev/hda / ext2 rw 0 0\n");
    else text_puts(&text, "ramfs / ramfs rw 0 0\n");
    text_puts(&text, "proc /proc proc ro 0 0\n");
    text_puts(&text, "devfs /dev devfs rw 0 0\n");
    return 0;
}

static int parse_pid(const char *path, int *pid, const char **rest) {
    const char *cursor = path + 5;
    int value = 0;
    if (*cursor != '/') return -1;
    cursor++;
    if (*cursor < '0' || *cursor > '9') return -1;
    while (*cursor >= '0' && *cursor <= '9') {
        value = value * 10 + (*cursor - '0');
        cursor++;
    }
    *pid = value;
    *rest = (*cursor == '/') ? cursor + 1 : "";
    return 0;
}

static const process_t *process_for_pid(int pid) {
    const process_t *fallback = 0;
    for (int index = 0; index < PROCESS_MAX; index++) {
        const process_t *process = process_at(index);
        if (!process) continue;
        if (process->pid == pid) return process;
        if (!fallback) fallback = process;
    }
    /* The kernel shell has no process context (pid 0): fall back to the first
       live process so /proc/self still reports something useful. */
    return pid <= 0 ? fallback : 0;
}

static int write_process_status(void *file) {
    text_buffer_t text = { procfs_file_buffer(file), PROC_BUFFER_SIZE, 0 };
    int pid;
    const char *name;
    const process_t *process;
    if (parse_pid(procfs_file_path(file), &pid, &name) < 0) return -1;
    process = process_for_pid(pid);
    if (!process) return -1;
    text_clear(&text);
    text_puts(&text, "Name:\t");
    text_puts(&text, process->name);
    text_puts(&text, "\nState:\t");
    text_puts(&text, process_state_text(process->state));
    text_puts(&text, "\nPid:\t");
    text_put_u32(&text, (uint32_t)process->pid);
    text_puts(&text, "\nPPid:\t");
    text_put_u32(&text, (uint32_t)process->parent_pid);
    text_puts(&text, "\nUid:\t");
    text_put_u32(&text, process->uid);
    text_puts(&text, "\nEntry:\t0x");
    text_put_hex32(&text, process->entry_point);
    text_puts(&text, "\nArgs:\t");
    text_puts(&text, process->args);
    text_puts(&text, "\n");
    return 0;
}

static int write_process_cmdline(void *file) {
    text_buffer_t text = { procfs_file_buffer(file), PROC_BUFFER_SIZE, 0 };
    int pid;
    const char *name;
    const process_t *process;
    if (parse_pid(procfs_file_path(file), &pid, &name) < 0) return -1;
    process = process_for_pid(pid);
    if (!process) return -1;
    text_clear(&text);
    text_puts(&text, process->name);
    if (process->args[0]) {
        text_putc(&text, ' ');
        text_puts(&text, process->args);
    }
    text_putc(&text, '\n');
    return 0;
}

static int write_process_stat(void *file) {
    text_buffer_t text = { procfs_file_buffer(file), PROC_BUFFER_SIZE, 0 };
    int pid;
    const char *name;
    const process_t *process;
    if (parse_pid(procfs_file_path(file), &pid, &name) < 0) return -1;
    process = process_for_pid(pid);
    if (!process) return -1;
    text_clear(&text);
    text_put_u32(&text, (uint32_t)process->pid);
    text_puts(&text, " (");
    text_puts(&text, process->name);
    text_puts(&text, ") ");
    text_putc(&text, process_state_text(process->state)[0]);
    text_putc(&text, ' ');
    text_put_u32(&text, (uint32_t)process->parent_pid);
    text_puts(&text, " 0 0 0\n");
    return 0;
}

static int procfs_write_file(void *file) {
    const char *path = procfs_file_path(file);
    int pid;
    const char *rest;
    if (path_equals(path, "/proc/uptime")) return write_uptime(file);
    if (path_equals(path, "/proc/meminfo")) return write_meminfo(file);
    if (path_equals(path, "/proc/version")) return write_version(file);
    if (path_equals(path, "/proc/cmdline")) return write_cmdline(file);
    if (path_equals(path, "/proc/uname")) return write_uname(file);
    if (path_equals(path, "/proc/mounts")) return write_mounts(file);
    if (path_equals(path, "/proc/net/dev")) return write_netdev(file);
    if (path_equals(path, "/proc/net/route")) return write_routes(file);
    if (path_equals(path, "/proc/net/if_inet6")) { procfs_file_buffer(file)[0] = '\0'; return 0; }
    if (procfs_is_proc_path(path) && parse_pid(path, &pid, &rest) == 0 && rest[0]) {
        if (path_equals(rest, "status")) return write_process_status(file);
        if (path_equals(rest, "cmdline")) return write_process_cmdline(file);
        if (path_equals(rest, "stat")) return write_process_stat(file);
        if (path_equals(rest, "maps")) return write_process_stat(file);
        if (path_equals(rest, "environ")) { procfs_file_buffer(file)[0] = '\0'; return 0; }
    }
    return -1;
}

/* ------------------------------------------------------------------------- */

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
    procfs_set_hook(procfs_write_file);
    return 0;
}

int vfs_open(const char *path, int flags) {
    const vfs_device_t *device;
    int regular_file;
    int fd;
    if (!path) return -1;
    if (procfs_is_proc_path(path)) {
        int proc_fd = procfs_open(path, flags);
        if (proc_fd < 0) return -1;
        for (fd = 3; fd < VFS_MAX_FDS; fd++) {
            if (!descriptors[fd].used) {
                descriptors[fd].used = 1;
                descriptors[fd].flags = flags | VFS_O_PROCFS;
                descriptors[fd].offset = (uint32_t)proc_fd;
                copy_path(descriptors[fd].path, path);
                return fd;
            }
        }
        procfs_close(proc_fd);
        return -1;
    }
    device = find_device(path);
    regular_file = ext2_is_mounted() ? ext2_find_inode_by_path(path) >= 0 :
                                      ramfs_find_file(path) >= 0;
    if (!device && !regular_file && is_directory(path)) return -1;
    if (!regular_file && !device && (flags & VFS_O_CREATE)) {
        if (ext2_is_mounted() ? ext2_create_file_by_path(path) < 0 : ramfs_create_file(path) < 0) return -1;
        regular_file = 1;
    }
    /* Device nodes and /proc are kernel objects, not ext2 inodes: the inode
       permission check below only applies to files that live on disk. */
    if (regular_file && !has_permission(path, flags)) return -1;

    for (fd = 3; fd < VFS_MAX_FDS; fd++) {
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
    if (descriptors[fd].flags & VFS_O_PROCFS) {
        int result = procfs_read((int)descriptors[fd].offset, (char *)buffer, size);
        if (result < 0) return -1;
        return result;
    }
    const vfs_device_t *device = find_device(descriptors[fd].path);
    uint32_t done = 0;
    int result;
    if (device) {
        result = device->read ? device->read(buffer, size, descriptors[fd].offset, &done) : -1;
    } else if (ext2_is_mounted()) {
        result = ext2_read_file_range_by_path(descriptors[fd].path, descriptors[fd].offset,
                                              (char *)buffer, size);
        if (result < 0) return result;
        done = (uint32_t)result;
    } else {
        char file_buffer[8192];
        int file_size = ramfs_read_file(descriptors[fd].path, file_buffer, sizeof(file_buffer));
        if (file_size < 0 || descriptors[fd].offset >= (uint32_t)file_size) return 0;
        done = (uint32_t)file_size - descriptors[fd].offset;
        if (done > size) done = size;
        for (uint32_t index = 0; index < done; index++) {
            ((char *)buffer)[index] = file_buffer[descriptors[fd].offset + index];
        }
        result = (int)done;
    }
    if (result < 0) return result;
    descriptors[fd].offset += device ? done : (uint32_t)result;
    return device ? (int)done : result;
}

int vfs_write(int fd, const void *buffer, uint32_t size) {
    if (fd < 0 || fd >= VFS_MAX_FDS || !descriptors[fd].used || !(descriptors[fd].flags & VFS_O_WRITE)) return -1;
    if (descriptors[fd].flags & VFS_O_PROCFS) return -1;
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

int vfs_is_regular(int fd) {
    if (fd < 0 || fd >= VFS_MAX_FDS || !descriptors[fd].used) return 0;
    if (descriptors[fd].flags & VFS_O_PROCFS) return 1;
    return find_device(descriptors[fd].path) == 0;
}

int vfs_close(int fd) {
    if (fd < 3 || fd >= VFS_MAX_FDS || !descriptors[fd].used) return -1;
    if (descriptors[fd].flags & VFS_O_PROCFS) procfs_close((int)descriptors[fd].offset);
    descriptors[fd].used = 0;
    return 0;
}

int vfs_seek(int fd, uint32_t offset) {
    if (fd < 0 || fd >= VFS_MAX_FDS || !descriptors[fd].used) return -1;
    if (descriptors[fd].flags & VFS_O_PROCFS) return procfs_seek((int)descriptors[fd].offset, offset);
    descriptors[fd].offset = offset;
    return 0;
}

vfs_node_type_t vfs_node_type(const char *path) {
    const vfs_device_t *device = find_device(path);
    if (device) return device->type;
    if (is_directory(path)) return VFS_NODE_DIRECTORY;
    return VFS_NODE_FILE;
}

#if defined(PIUX_MODULE_VFS)

int vfs_module_ready = 0;

extern int (*vfs_open_dispatch)(const char *path, int flags);
extern int (*vfs_read_dispatch)(int fd, void *buffer, uint32_t size);
extern int (*vfs_write_dispatch)(int fd, const void *buffer, uint32_t size);
extern int (*vfs_close_dispatch)(int fd);
extern int (*vfs_seek_dispatch)(int fd, uint32_t offset);
extern vfs_node_type_t (*vfs_node_type_dispatch)(const char *path);
extern int (*vfs_is_regular_dispatch)(int fd);
extern vfs_console_putc_t (*vfs_console_output_dispatch)(vfs_console_putc_t putc);

static int vfs_module_init(void) {
    vfs_module_ready = 0;
    if (vfs_init() < 0) return -1;
    vfs_open_dispatch = vfs_open;
    vfs_read_dispatch = vfs_read;
    vfs_write_dispatch = vfs_write;
    vfs_close_dispatch = vfs_close;
    vfs_seek_dispatch = vfs_seek;
    vfs_node_type_dispatch = vfs_node_type;
    vfs_is_regular_dispatch = vfs_is_regular;
    vfs_console_output_dispatch = vfs_set_console_output;
    vfs_module_ready = 1;
    return 0;
}

static void vfs_module_exit(void) {
    vfs_module_ready = 0;
}

static const char *const vfs_module_deps[] = { };

PIUX_MODULE(
    .name = "vfs",
    .version = PIUX_MODULE_VERSION,
    .deps = 0,
    .dep_count = 0,
    .init = vfs_module_init,
    .exit = vfs_module_exit,
    .description = "Unix-like virtual filesystem: file descriptors, device nodes and /proc"
);

void piux_module_exports(const char *const **names, const void *const **addresses,
                         uint32_t *count) {
    static const char *const export_names[] = { "vfs_module_init" };
    static const void *const export_addresses[] = { (const void *)(uintptr_t)&vfs_module_init };
    *names = export_names;
    *addresses = export_addresses;
    *count = 1;
}

#endif

#endif
