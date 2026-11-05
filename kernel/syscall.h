#ifndef SYSCALL_H
#define SYSCALL_H

#include <stdint.h>

enum {
    SYS_READ = 0,
    SYS_WRITE = 1,
    SYS_OPEN = 2,
    SYS_CLOSE = 3,
    SYS_SEEK = 4,
    SYS_GETPID = 5,
    SYS_GETUID = 6,
    SYS_YIELD = 7,
    SYS_EXIT = 8,
    SYS_VIDEO_INFO = 9,
    SYS_VIDEO_CLEAR = 10,
    SYS_VIDEO_DRAW = 11,
    SYS_VIDEO_CAPTURE = 12,
    SYS_VIDEO_RESTORE = 13,
    SYS_VIDEO_BEGIN_FRAME = 14,
    SYS_VIDEO_BEGIN_REGION = 15,
    SYS_VIDEO_PRESENT = 16,
    SYS_KEYBOARD_POLL = 17,
    SYS_KEYBOARD_MODIFIERS = 18,
    SYS_MOUSE_POLL = 19,
    SYS_SYSTEM_INFO = 20,
    SYS_AUTH_INFO = 21,
    SYS_SHELL_EXEC = 22,
    SYS_MOUSE_INIT = 23
};

typedef struct {
    int framebuffer_ready;
    int columns;
    int rows;
    int pixel_width;
    int pixel_height;
} syscall_video_info_t;

typedef struct {
    uint32_t type;
    int x;
    int y;
    uint32_t value;  
    uint8_t alpha;
    uint8_t color;
    char character;
    uint32_t aux;    
    uint32_t aux2;   
} syscall_video_op_t;

typedef struct {
    int x;
    int y;
    int width;
    int height;
    uint8_t *buffer;
} syscall_video_region_t;

typedef struct {
    int8_t dx;
    int8_t dy;
    int8_t wheel;
    uint8_t buttons;
} syscall_mouse_event_t;

typedef struct {
    uint32_t memory_total_kb;
    uint32_t memory_used_kb;
    uint32_t memory_free_kb;
    uint32_t filesystem_blocks;
    uint32_t filesystem_free_blocks;
    uint32_t filesystem_block_size;
    int filesystem_mounted;
    int pid;
    int uid;
    char process_name[32];
    char os_name[32];
    char os_version[64];
} syscall_system_info_t;

typedef struct {
    int uid;
    int is_sudoer;
    char username[32];
    char current_path[128];
} syscall_auth_info_t;

typedef struct {
    uint32_t edi;
    uint32_t esi;
    uint32_t ebp;
    uint32_t original_esp;
    uint32_t ebx;
    uint32_t edx;
    uint32_t ecx;
    uint32_t eax;
} syscall_registers_t;

void syscall_init(void);
void syscall_dispatch(syscall_registers_t *registers);

static inline int syscall_invoke(uint32_t number, uint32_t argument_one,
                                 uint32_t argument_two, uint32_t argument_three) {
    int result;
    asm volatile ("int $0x80"
                  : "=a"(result)
                  : "a"(number), "b"(argument_one), "c"(argument_two), "d"(argument_three)
                  : "memory");
    return result;
}

#endif