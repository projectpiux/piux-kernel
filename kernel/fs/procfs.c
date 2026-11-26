#include <stdint.h>
#include "procfs.h"
#include "vfs.h"
#include "../core/process.h"

/*
 * procfs: "everything is a file" for the live kernel state.
 *
 * The core exports /proc as a read-only namespace whose entries are generated
 * on demand instead of living on disk. The format code lives in the VFS module
 * (kernel/fs/vfs.c) and is installed here through a hook, so the core keeps its
 * small dispatch boundary: this file owns the descriptor pool and the path
 * matching, the hook fills the bytes of one virtual file.
 *
 * Every ->read of the underlying file regenerates the whole snapshot inside the
 * descriptor (entries are dynamic), then serves the requested byte range.
 */

#define PROCFS_MAX_FDS 4

typedef struct {
    int used;
    int flags;
    uint32_t offset;
    char path[64];
    char data[PROC_BUFFER_SIZE];
} procfs_file_t;

static procfs_file_t procfs_files[PROCFS_MAX_FDS];
static procfs_read_fn procfs_read_hook;

/*
 * The snapshot is rebuilt on every ->read so entries such as /proc/uptime
 * never go stale, and while the caller drains one file the bytes must stay
 * stable: bytes stored since the end of the last drain are served as-is.
 */

const char *procfs_file_path(void *file) {
    return ((procfs_file_t *)file)->path;
}

char *procfs_file_buffer(void *file) {
    return ((procfs_file_t *)file)->data;
}

int procfs_set_hook(procfs_read_fn hook) {
    procfs_read_hook = hook;
    return 0;
}

static int text_is(const char *left, const char *right) {
    int index = 0;
    while (left[index] && right[index] && left[index] == right[index]) index++;
    return left[index] == right[index];
}

static int text_prefix(const char *text, const char *prefix) {
    int index = 0;
    while (prefix[index] && text[index] == prefix[index]) index++;
    return prefix[index] == '\0';
}

static void copy_text(char *destination, const char *source, uint32_t capacity) {
    uint32_t index = 0;
    if (!destination || !source || capacity == 0) return;
    while (source[index] && index + 1 < capacity) {
        destination[index] = source[index];
        index++;
    }
    destination[index] = '\0';
}

static int pid_prefix(const char *path, const char **rest) {
    const char *cursor = path + 5;
    if (*cursor != '/') return 0;
    cursor++;
    if (*cursor < '0' || *cursor > '9') return 0;
    while (*cursor >= '0' && *cursor <= '9') cursor++;
    if (*cursor == '\0') { *rest = cursor; return 1; }
    if (*cursor != '/') return 0;
    cursor++;
    if (text_is(cursor, "status") || text_is(cursor, "cmdline") ||
        text_is(cursor, "stat") || text_is(cursor, "maps") ||
        text_is(cursor, "environ")) {
        *rest = cursor;
        return 1;
    }
    return 0;
}

int procfs_is_proc_path(const char *path) {
    int index = 0;
    if (!path) return 0;
    while (index < 5 && path[index] && path[index] == "/proc"[index]) index++;
    if (index < 5) return 0;
    return path[5] == '\0' || path[5] == '/';
}

static int is_proc_directory(const char *path) {
    const char *rest;
    if (text_is(path, "/proc") || text_is(path, "/proc/.")) return 1;
    if (text_is(path, "/proc/self") || text_is(path, "/proc/net")) return 1;
    if (pid_prefix(path, &rest) && rest[0] == '\0') return 1;
    return 0;
}

int procfs_node_type(const char *path) {
    if (!procfs_is_proc_path(path)) return VFS_NODE_FILE;
    if (is_proc_directory(path)) return VFS_NODE_DIRECTORY;
    return VFS_NODE_FILE;
}

void procfs_list(void (*puts)(const char *)) {
    puts("[FILE] uptime\n");
    puts("[FILE] meminfo\n");
    puts("[FILE] version\n");
    puts("[FILE] cmdline\n");
    puts("[FILE] uname\n");
    puts("[FILE] mounts\n");
    puts("[DIR]  self\n");
    puts("[DIR]  net\n");
    puts("[DIR]  1\n");
    puts("[DIR]  2\n");
}

static void procfs_resolve_self(char *buffer) {
    char target[64];
    char digits[12];
    int pid;
    int count = 0;
    int length = 0;
    if (!text_prefix(buffer, "/proc/self/")) return;
    pid = process_getpid();
    if (pid <= 0) pid = 1;
    while (pid > 0) { digits[count++] = (char)('0' + pid % 10); pid /= 10; }
    copy_text(target, "/proc/", sizeof(target));
    length = 6;
    while (count > 0) { target[length++] = digits[--count]; }
    target[length++] = '/';
    target[length] = '\0';
    copy_text(target + length, buffer + 11, sizeof(target) - (uint32_t)length);
    copy_text(buffer, target, 64);
}

int procfs_open(const char *path, int flags) {
    if (!path || !procfs_is_proc_path(path)) return -1;
    if (procfs_node_type(path) == VFS_NODE_DIRECTORY) return -1;
    if (flags & (VFS_O_WRITE | VFS_O_CREATE)) return -1;
    for (int fd = 0; fd < PROCFS_MAX_FDS; fd++) {
        if (!procfs_files[fd].used) {
            char resolved[64];
            procfs_files[fd].used = 1;
            procfs_files[fd].flags = flags;
            procfs_files[fd].offset = 0;
            procfs_files[fd].data[0] = '\0';
            copy_text(resolved, path, sizeof(resolved));
            procfs_resolve_self(resolved);
            copy_text(procfs_files[fd].path, resolved, sizeof(procfs_files[fd].path));
            return fd;
        }
    }
    return -1;
}

int procfs_read(int fd, char *buffer, uint32_t size) {
    procfs_file_t *file;
    uint32_t length = 0;
    uint32_t available;
    uint32_t copied;
    if (fd < 0 || fd >= PROCFS_MAX_FDS || !procfs_files[fd].used || !buffer) return -1;
    file = &procfs_files[fd];
    if (!procfs_read_hook || procfs_read_hook(file) < 0) return -1;
    while (length < sizeof(file->data) - 1 && file->data[length]) length++;
    if (length < file->offset) file->offset = length;
    if (file->offset >= length) return 0;
    available = length - file->offset;
    copied = available < size ? available : size;
    for (uint32_t index = 0; index < copied; index++) {
        buffer[index] = file->data[file->offset + index];
    }
    file->offset += copied;
    return (int)copied;
}

void procfs_close(int fd) {
    if (fd < 0 || fd >= PROCFS_MAX_FDS) return;
    procfs_files[fd].used = 0;
}

int procfs_seek(int fd, uint32_t offset) {
    if (fd < 0 || fd >= PROCFS_MAX_FDS || !procfs_files[fd].used) return -1;
    procfs_files[fd].offset = offset;
    return 0;
}
