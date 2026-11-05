#ifndef PROCESS_H
#define PROCESS_H

#include <stdint.h>
#include "memory.h"

#define PROCESS_MAX 32
#define PROCESS_NAME_SIZE 32
#define PROCESS_USER_STACK_SIZE USER_STACK_SIZE

typedef void (*process_entry_t)(void *argument);

typedef enum {
    PROCESS_UNUSED,
    PROCESS_READY,
    PROCESS_RUNNING,
    PROCESS_SLEEPING,
    PROCESS_ZOMBIE
} process_state_t;

typedef struct {
    int pid;
    int parent_pid;
    uint32_t uid;
    process_state_t state;
    process_entry_t entry;
    void *argument;
    char name[PROCESS_NAME_SIZE];
} process_t;

#define PROCESS_USER_STACK_TOP USER_SPACE_TOP

typedef struct {
    uint32_t eip;
    uint32_t esp;
    uint32_t ebp;
    uint32_t ebx;
    uint32_t esi;
    uint32_t edi;
    uint32_t eflags;
} process_context_t;

int process_context_save(process_context_t *context);
void process_context_restore(process_context_t *context, int value) __attribute__((noreturn));
void process_enter_user_mode(uint32_t entry, uint32_t user_stack) __attribute__((noreturn));

void process_init(void);
int process_create(const char *name, process_entry_t entry, void *argument);
int process_getpid(void);
int process_getuid(void);
void process_exit(int status);
void process_yield(void);
int process_schedule_once(void);
const process_t *process_current(void);

int process_exec(const char *path);
void process_exit_user(int status) __attribute__((noreturn));
void process_handle_user_exception(uint32_t vector) __attribute__((noreturn));

#endif