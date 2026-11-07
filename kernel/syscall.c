#include <stdint.h>
#include "syscall.h"
#include "vfs.h"
#include "process.h"
#include "memory.h"
#include "video.h"
#include "keyboard.h"
#include "mouse.h"
#include "system.h"
#include "auth.h"
#include "ext2.h"
#include "shell.h"
#include "vga_log.h"

extern ext2_filesystem_t fs;
extern char _os_infos_start[];
extern char _os_infos_end[];
extern void vga_clear(void);

typedef struct {
    char *buffer;
    uint32_t capacity;
    uint32_t length;
} shell_capture_t;

#define SHELL_CAPTURE_KERNEL_SIZE 4096
static char shell_capture_kernel[SHELL_CAPTURE_KERNEL_SIZE];

static void capture_putc(char character, shell_capture_t *capture) {
    if (capture->length + 1 >= capture->capacity) return;
    capture->buffer[capture->length++] = character;
    capture->buffer[capture->length] = '\0';
}

static void capture_puts(const char *text, shell_capture_t *capture) {
    if (!text) return;
    while (*text) capture_putc(*text++, capture);
}

static shell_capture_t *active_shell_capture;

static void shell_output_puts(const char *text) {
    capture_puts(text, active_shell_capture);
}

static void shell_output_putc(char character) {
    capture_putc(character, active_shell_capture);
}

static void copy_fixed_string(char *destination, uint32_t capacity, const char *source) {
    uint32_t index = 0;
    if (!capacity) return;
    while (source[index] && index + 1 < capacity) {
        destination[index] = source[index];
        index++;
    }
    destination[index] = '\0';
}

static void copy_metadata_value(char *destination, uint32_t capacity, const char *label) {
    const char *line = _os_infos_start;
    uint32_t label_length = 0;
    while (label[label_length]) label_length++;
    while (line < _os_infos_end) {
        const char *end = line;
        while (end < _os_infos_end && *end != '\n' && *end != '\r') end++;
        for (const char *cursor = line; cursor + label_length <= end; cursor++) {
            uint32_t index = 0;
            while (index < label_length && cursor[index] == label[index]) index++;
            if (index == label_length) {
                uint32_t length = 0;
                const char *value = cursor + label_length;
                while (value + length < end && length + 1 < capacity) {
                    destination[length] = value[length];
                    length++;
                }
                destination[length] = '\0';
                return;
            }
        }
        line = end;
        while (line < _os_infos_end && (*line == '\n' || *line == '\r')) line++;
    }
    if (capacity) destination[0] = '\0';
}

static int user_video_region_valid(const syscall_video_region_t *region, int writable) {
    uint32_t pixels;
    if (!region || region->width <= 0 || region->height <= 0 ||
        (uint32_t)region->width > 0xffffffffU / (uint32_t)region->height) return 0;
    pixels = (uint32_t)region->width * (uint32_t)region->height;
    if (pixels > 0xffffffffU / 4U) return 0;
    return memory_user_range_valid(region->buffer, pixels * 4U, writable);
}

static int syscall_from_user(const syscall_registers_t *registers) {
    uint32_t *return_frame = (uint32_t *)(uintptr_t)registers->original_esp;
    return (return_frame[1] & 3) == 3;
}

static int user_string_valid(const char *text) {
    uintptr_t start = (uintptr_t)text;
    if (!text) return 0;
    for (uint32_t index = 0; index < 4096; index++) {
        const char *character;
        if (start + index < start) return 0;
        character = (const char *)(start + index);
        if (!memory_range_valid(character, 1, 1)) return 0;
        if (*character == '\0') return 1;
    }
    return 0;
}

static int syscall_open(const char *path, int flags) {
    if (!path) return -1;
    if (!(flags & (VFS_O_READ | VFS_O_WRITE | VFS_O_CREATE))) flags |= VFS_O_READ;
    return vfs_open(path, flags);
}

void syscall_dispatch(syscall_registers_t *registers) {
    int result = -1;
    int from_user = syscall_from_user(registers);
    switch (registers->eax) {
        case SYS_READ:
            if (from_user && !memory_user_range_valid((void *)registers->ecx, registers->edx, 0)) break;
            result = vfs_read((int)registers->ebx, (void *)registers->ecx, registers->edx);
            break;
        case SYS_WRITE:
            if (from_user && !memory_user_range_valid((void *)registers->ecx, registers->edx, 0)) break;
            result = vfs_write((int)registers->ebx, (const void *)registers->ecx, registers->edx);
            break;
        case SYS_OPEN:
            if (from_user && !user_string_valid((const char *)registers->ebx)) break;
            result = syscall_open((const char *)registers->ebx, (int)registers->ecx);
            break;
        case SYS_CLOSE:
            result = vfs_close((int)registers->ebx);
            break;
        case SYS_SEEK:
            result = vfs_seek((int)registers->ebx, registers->ecx);
            break;
        case SYS_GETPID:
            result = process_getpid();
            break;
        case SYS_GETUID:
            result = process_getuid();
            break;
        case SYS_YIELD:
            process_yield();
            result = 0;
            break;
        case SYS_EXIT:
            if (from_user) process_exit((int)registers->ebx);
            process_exit((int)registers->ebx);
            result = 0;
            break;
        case SYS_VIDEO_INFO:
            if (from_user && !memory_user_range_valid((void *)registers->ebx,
                    sizeof(syscall_video_info_t), 1)) break;
            ((syscall_video_info_t *)(uintptr_t)registers->ebx)->framebuffer_ready = video_framebuffer_ready;
            ((syscall_video_info_t *)(uintptr_t)registers->ebx)->columns = video_columns;
            ((syscall_video_info_t *)(uintptr_t)registers->ebx)->rows = video_rows;
            ((syscall_video_info_t *)(uintptr_t)registers->ebx)->pixel_width = video_pixel_width;
            ((syscall_video_info_t *)(uintptr_t)registers->ebx)->pixel_height = video_pixel_height;
            result = 0;
            break;
        case SYS_VIDEO_CLEAR:
            if (from_user) break; 
            vga_clear();
            result = 0;
            break;
        case SYS_VIDEO_DRAW: {
            syscall_video_op_t *operations = (syscall_video_op_t *)(uintptr_t)registers->ebx;
            uint32_t count = registers->ecx;
            if (count > 512 || count > 0xffffffffU / sizeof(*operations) ||
                (from_user && !memory_user_range_valid(operations, count * sizeof(*operations), 0))) break;
            for (uint32_t index = 0; index < count; index++) {
                if (operations[index].type == 1) {
                    video_put_cell(operations[index].x, operations[index].y,
                                   operations[index].character, operations[index].color);
                } else if (operations[index].type == 2) {
                    video_put_pixel(operations[index].x, operations[index].y,
                                    operations[index].value, operations[index].alpha);
                } else if (operations[index].type == 3) {
                    video_fill_rect(operations[index].x, operations[index].y,
                                    (int)operations[index].aux, (int)operations[index].aux2,
                                    operations[index].value, operations[index].alpha);
                }
            }
            result = (int)count;
            break;
        }
        case SYS_VIDEO_CAPTURE:
        case SYS_VIDEO_RESTORE: {
            syscall_video_region_t *user_region = (syscall_video_region_t *)(uintptr_t)registers->ebx;
            syscall_video_region_t region;
            if (from_user && !memory_user_range_valid(user_region, sizeof(region), 0)) break;
            region = *user_region;
            if (from_user && !user_video_region_valid(&region, registers->eax == SYS_VIDEO_CAPTURE)) break;
            if (registers->eax == SYS_VIDEO_CAPTURE)
                video_capture_region(region.x, region.y, region.width, region.height, region.buffer);
            else
                video_restore_region(region.x, region.y, region.width, region.height, region.buffer);
            result = 0;
            break;
        }
        case SYS_VIDEO_BEGIN_FRAME:
            result = video_begin_frame();
            break;
        case SYS_VIDEO_BEGIN_REGION: {
            syscall_video_region_t *user_region = (syscall_video_region_t *)(uintptr_t)registers->ebx;
            syscall_video_region_t region;
            if (from_user && !memory_user_range_valid(user_region, sizeof(region), 0)) break;
            region = *user_region;
            result = video_begin_region_frame(region.x, region.y, region.width, region.height);
            break;
        }
        case SYS_VIDEO_PRESENT:

            if (registers->ebx) video_present_region();
            else video_present();
            result = 0;
            break;
        case SYS_KEYBOARD_POLL:
            result = keyboard_try_read_char();
            break;
        case SYS_KEYBOARD_MODIFIERS:
            result = (keyboard_is_ctrl_pressed() ? 1 : 0) |
                     (keyboard_is_alt_pressed() ? 2 : 0);
            break;
        case SYS_MOUSE_POLL: {
            syscall_mouse_event_t *user_event = (syscall_mouse_event_t *)(uintptr_t)registers->ebx;
            mouse_event_t event;
            if (from_user && !memory_user_range_valid(user_event, sizeof(*user_event), 1)) break;
            result = mouse_poll(&event);
            if (result > 0) {
                user_event->dx = event.dx;
                user_event->dy = event.dy;
                user_event->wheel = event.wheel;
                user_event->buttons = event.buttons;
            }
            break;
        }
        case SYS_MOUSE_INIT:
            mouse_init();
            result = 0;
            break;
        case SYS_SPAWN: {
            const char *path = (const char *)(uintptr_t)registers->ebx;
            const char *args = (const char *)(uintptr_t)registers->ecx;
            if (from_user && (!user_string_valid(path) ||
                (args && !user_string_valid(args)))) break;
            result = process_spawn_user(path, args);
            break;
        }
        case SYS_WAIT: {
            int status = 0;
            result = process_wait((int)registers->ebx, (int)registers->ecx, &status);
            if (registers->edx &&
                (!from_user || memory_user_range_valid((void *)(uintptr_t)registers->edx, 4, 1))) {
                *(int *)(uintptr_t)registers->edx = status;
            }
            break;
        }
        case SYS_IPC: {
            void *buffer = (void *)(uintptr_t)registers->ecx;
            if (from_user && !memory_user_range_valid(buffer, sizeof(piux_ipc_msg_t), 1)) break;
            if (registers->ebx == 0) {
                result = process_ipc_recv((piux_ipc_msg_t *)buffer);
            } else if (registers->ebx == 2) {
                result = process_ipc_try_recv((piux_ipc_msg_t *)buffer);
            } else {
                piux_ipc_msg_t message = *(piux_ipc_msg_t *)buffer;
                result = process_ipc_send((int)registers->edx, &message);
            }
            break;
        }
        case SYS_GETARGS: {
            char *buffer = (char *)(uintptr_t)registers->ebx;
            if (from_user && !memory_user_range_valid(buffer, PROCESS_ARGS_SIZE, 1)) break;
            process_get_args(buffer, PROCESS_ARGS_SIZE);
            result = 0;
            break;
        }
        case SYS_SYSTEM_INFO: {
            syscall_system_info_t info = {0};
            const process_t *process = process_current();
            syscall_system_info_t *user_info = (syscall_system_info_t *)(uintptr_t)registers->ebx;
            if (from_user && !memory_user_range_valid(user_info, sizeof(info), 1)) break;
            info.memory_total_kb = system_memory_total_kb;
            info.memory_used_kb = system_memory_used_kb;
            info.memory_free_kb = system_memory_free_kb;
            info.filesystem_mounted = ext2_is_mounted();
            if (info.filesystem_mounted) {
                info.filesystem_blocks = fs.superblock.blocks_count;
                info.filesystem_free_blocks = fs.superblock.free_blocks_count;
                info.filesystem_block_size = fs.block_size;
            }
            info.pid = process ? process->pid : 0;
            info.uid = process ? (int)process->uid : (int)auth_current_uid();
            if (process) copy_fixed_string(info.process_name, sizeof(info.process_name), process->name);
            copy_metadata_value(info.os_name, sizeof(info.os_name), "Name: ");
            copy_metadata_value(info.os_version, sizeof(info.os_version), "Version: ");
            *user_info = info;
            result = 0;
            break;
        }
        case SYS_AUTH_INFO: {
            syscall_auth_info_t info = {0};
            syscall_auth_info_t *user_info = (syscall_auth_info_t *)(uintptr_t)registers->ebx;
            if (from_user && !memory_user_range_valid(user_info, sizeof(info), 1)) break;
            info.uid = process_getuid();
            info.is_sudoer = auth_current_user_is_sudoer();
            copy_fixed_string(info.username, sizeof(info.username), auth_current_username());
            *user_info = info;
            result = 0;
            break;
        }
        case SYS_SHELL_EXEC: {
            shell_capture_t capture;
            char *output = (char *)(uintptr_t)registers->ecx;
            uint32_t capacity = registers->edx;
            if ((from_user && (!user_string_valid((const char *)(uintptr_t)registers->ebx) ||
                 !memory_user_range_valid(output, capacity, 1))) || capacity == 0) break;
            capture.buffer = shell_capture_kernel;
            capture.capacity = SHELL_CAPTURE_KERNEL_SIZE;
            capture.length = 0;
            shell_capture_kernel[0] = '\0';
            active_shell_capture = &capture;
            result = shell_execute_command_line((const char *)(uintptr_t)registers->ebx,
                                                shell_output_puts, shell_output_putc);
            active_shell_capture = 0;
            {
                uint32_t copied = 0;
                if (capture.length > capacity - 1) capture.length = capacity - 1;
                while (copied < capture.length) {
                    output[copied] = shell_capture_kernel[copied];
                    copied++;
                }
                output[copied] = '\0';
            }
            break;
        }
        default:
            break;
    }
    registers->eax = (uint32_t)result;
}

void syscall_init(void);