#include <stdint.h>
#include "process.h"
#include "auth.h"
#include "elf.h"
#include "memory.h"

static process_t process_table[PROCESS_MAX];
static process_t *current_process;
static int next_pid = 1;
static process_context_t exec_context;
static int exec_exit_status;
static int user_execution_active;
static memory_address_space_t exec_address_space;
static process_t *exec_parent_process;
static process_t *exec_user_process;

static void copy_name(char *destination, const char *source) {
    int index = 0;
    while (source[index] && index < PROCESS_NAME_SIZE - 1) {
        destination[index] = source[index];
        index++;
    }
    destination[index] = '\0';
}

void process_init(void) {
    for (int index = 0; index < PROCESS_MAX; index++) process_table[index].state = PROCESS_UNUSED;
    current_process = &process_table[0];
    current_process->pid = next_pid++;
    current_process->parent_pid = 0;
    current_process->uid = auth_current_uid();
    current_process->state = PROCESS_RUNNING;
    copy_name(current_process->name, "kernel");
}

int process_create(const char *name, process_entry_t entry, void *argument) {
    for (int index = 0; index < PROCESS_MAX; index++) {
        if (process_table[index].state == PROCESS_UNUSED) {
            process_table[index].pid = next_pid++;
            process_table[index].parent_pid = current_process ? current_process->pid : 0;
            process_table[index].uid = process_getuid();
            process_table[index].state = PROCESS_READY;
            process_table[index].entry = entry;
            process_table[index].argument = argument;
            copy_name(process_table[index].name, name);
            return process_table[index].pid;
        }
    }
    return -1;
}

int process_getpid(void) { return current_process ? current_process->pid : 0; }

int process_getuid(void) {
    if (!current_process) return (int)auth_current_uid();
    if (current_process->pid == 1) current_process->uid = auth_current_uid();
    return (int)current_process->uid;
}

void process_exit(int status) {
    (void)status;
    if (current_process) current_process->state = PROCESS_ZOMBIE;
}

void process_yield(void) {
    if (user_execution_active) return;
    if (current_process && current_process->state == PROCESS_RUNNING) current_process->state = PROCESS_READY;
    process_schedule_once();
}

int process_schedule_once(void) {
    for (int index = 0; index < PROCESS_MAX; index++) {
        process_t *candidate = &process_table[index];
        if (candidate->state != PROCESS_READY || !candidate->entry) continue;
        current_process = candidate;
        current_process->state = PROCESS_RUNNING;
        current_process->entry(current_process->argument);
        if (current_process->state == PROCESS_RUNNING) current_process->state = PROCESS_ZOMBIE;
        current_process = &process_table[0];
        current_process->state = PROCESS_RUNNING;
        return candidate->pid;
    }
    return 0;
}

const process_t *process_current(void) { return current_process; }

static process_t *reserve_user_process(const char *name) {
    for (int index = 1; index < PROCESS_MAX; index++) {
        process_t *candidate = &process_table[index];
        if (candidate->state != PROCESS_UNUSED) continue;
        candidate->pid = next_pid++;
        candidate->parent_pid = current_process ? current_process->pid : 0;
        candidate->uid = process_getuid();
        candidate->state = PROCESS_RUNNING;
        candidate->entry = 0;
        candidate->argument = 0;
        copy_name(candidate->name, name);
        return candidate;
    }
    return 0;
}

int process_exec(const char *path) {
    uint32_t entry_point;
    if (!path || user_execution_active) return -1;
    exec_exit_status = -1;
    user_execution_active = 0;
    exec_parent_process = current_process;
    exec_user_process = 0;
    exec_address_space.directory = 0;
    if (process_context_save(&exec_context) == 0) {
        if (memory_address_space_create(&exec_address_space) < 0) return -1;
        memory_address_space_activate(&exec_address_space);
        if (memory_user_map(PROCESS_USER_STACK_TOP - PROCESS_USER_STACK_SIZE,
                            PROCESS_USER_STACK_SIZE, 1) < 0 ||
            elf_load(path, &entry_point) < 0) {
            memory_address_space_activate(0);
            memory_address_space_destroy(&exec_address_space);
            return -1;
        }
        exec_user_process = reserve_user_process(path);
        if (!exec_user_process) {
            memory_address_space_activate(0);
            memory_address_space_destroy(&exec_address_space);
            exec_parent_process = 0;
            return -1;
        }
        exec_exit_status = 0;
        user_execution_active = 1;
        current_process = exec_user_process;
        process_enter_user_mode(entry_point, PROCESS_USER_STACK_TOP);
    }
    user_execution_active = 0;
    asm volatile ("mov $0x10, %%ax\n\t"
                  "mov %%ax, %%ds\n\t"
                  "mov %%ax, %%es\n\t"
                  "mov %%ax, %%fs\n\t"
                  "mov %%ax, %%gs"
                  : : : "ax", "memory");
    memory_address_space_activate(0);
    memory_address_space_destroy(&exec_address_space);
    if (exec_user_process) exec_user_process->state = PROCESS_UNUSED;
    current_process = exec_parent_process;
    if (current_process) current_process->state = PROCESS_RUNNING;
    exec_parent_process = 0;
    exec_user_process = 0;
    return exec_exit_status;
}

void process_exit_user(int status) {
    if (!user_execution_active) for (;;) asm volatile ("cli; hlt");
    exec_exit_status = status;
    user_execution_active = 0;
    process_context_restore(&exec_context, 1);
}

void process_handle_user_exception(uint32_t vector) {
    process_exit_user(128 + (int)vector);
}