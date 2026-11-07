#ifndef PROCESS_H
#define PROCESS_H

#include <stdint.h>
#include "memory.h"
#include "syscall.h"

#define PROCESS_MAX 32
#define PROCESS_NAME_SIZE 32
#define PROCESS_USER_STACK_SIZE USER_STACK_SIZE
#define PROCESS_KERNEL_STACK_SIZE 8192
#define PROCESS_ARGS_SIZE 128
#define PROCESS_IPC_MAILBOX_SIZE 4

typedef enum {
    PROCESS_UNUSED,
    PROCESS_READY,
    PROCESS_RUNNING,
    PROCESS_BLOCKED,
    PROCESS_ZOMBIE
} process_state_t;

typedef struct {
    uint32_t eip;
    uint32_t esp;
    uint32_t ebp;
    uint32_t ebx;
    uint32_t esi;
    uint32_t edi;
    uint32_t eflags;
} process_context_t;

typedef struct process_struct {
    int pid;
    int parent_pid;
    uint32_t uid;
    process_state_t state;
    int exit_status;
    char name[PROCESS_NAME_SIZE];
    char args[PROCESS_ARGS_SIZE];
    memory_address_space_t address_space;
    uint32_t entry_point;
    process_context_t kernel_context;
    uint8_t has_kernel_context;
    uint8_t kernel_stack_used;
    uint8_t kernel_stack[PROCESS_KERNEL_STACK_SIZE] __attribute__((aligned(16)));
    piux_ipc_msg_t mailbox[PROCESS_IPC_MAILBOX_SIZE];
    uint8_t mailbox_head;
    uint8_t mailbox_count;
} process_t;

#define PROCESS_USER_STACK_TOP USER_SPACE_TOP

void process_enable_scheduler(void);
int scheduler_is_enabled(void);
void process_disable_scheduler(void);
void process_schedule(void);
void process_set_recovery_hook(void (*hook)(void));
void process_init(void);
int process_spawn_user(const char *path, const char *args);
int process_spawn_and_wait(const char *path, const char *args);
int process_wait(int pid, int block, int *status);
int process_getpid(void);
int process_getuid(void);
void process_exit(int status) __attribute__((noreturn));
void process_yield(void);
void process_idle_yield(void);
int process_pid1_alive(void);
const process_t *process_current(void);
int process_ipc_send(int target_pid, const piux_ipc_msg_t *message);
int process_ipc_recv(piux_ipc_msg_t *message);
int process_ipc_try_recv(piux_ipc_msg_t *message);
void process_get_args(char *destination, uint32_t capacity);

int process_exec(const char *path);
void process_handle_user_exception(uint32_t vector) __attribute__((noreturn));

#endif
