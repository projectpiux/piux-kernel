#include <stdint.h>
#include "process.h"
#include "auth.h"
#include "elf.h"
#include "memory.h"
#include "gdt.h"
#include "vga_log.h"

typedef enum {
    PROCESS_RESUME_NONE = 0,
    PROCESS_RESUME_FIRST_USER = 1,
    PROCESS_RESUME_CONTEXT = 2,
    PROCESS_RESUME_TRAP = 3
} process_resume_kind_t;

#define TRAP_FRAME_SIZE 76

typedef struct {
    uint32_t gs;
    uint32_t fs;
    uint32_t es;
    uint32_t ds;
    uint32_t edi;
    uint32_t esi;
    uint32_t ebp;
    uint32_t esp_dummy;
    uint32_t ebx;
    uint32_t edx;
    uint32_t ecx;
    uint32_t eax;
    uint32_t int_number;
    uint32_t error_code;
    uint32_t eip;
    uint32_t cs;
    uint32_t eflags;
    uint32_t esp;
    uint32_t ss;
} trap_frame_t;

int process_context_save(process_context_t *context);
void process_context_restore(process_context_t *context) __attribute__((noreturn));
void process_enter_user_mode(void *entry) __attribute__((noreturn));

static process_t process_table[PROCESS_MAX];
static process_t *current_process;
static process_t *idle_process;
static int next_pid = 1;
static int scheduler_enabled;
static int recovery_requested;
static void (*recovery_hook)(void);
static int schedule_depth;

static uint32_t interrupts_save_and_disable(void) {
    uint32_t flags;
    asm volatile ("pushfl; popl %0; cli" : "=r"(flags) :: "memory");
    return flags;
}

static void interrupts_restore(uint32_t flags) {
    asm volatile ("pushl %0; popfl" :: "r"(flags) : "memory", "cc");
}

static void copy_name(char *destination, const char *source) {
    int index = 0;
    while (source[index] && index < PROCESS_NAME_SIZE - 1) {
        destination[index] = source[index];
        index++;
    }
    destination[index] = '\0';
}

static void copy_args(char *destination, uint32_t capacity, const char *source) {
    uint32_t index = 0;
    if (!capacity) return;
    if (source) {
        while (source[index] && index + 1 < capacity) {
            destination[index] = source[index];
            index++;
        }
    }
    destination[index] = '\0';
}

static process_t *process_by_pid(int pid) {
    if (pid <= 0) return 0;
    for (int index = 0; index < PROCESS_MAX; index++) {
        process_t *candidate = &process_table[index];
        if (candidate->state != PROCESS_UNUSED && candidate->pid == pid) return candidate;
    }
    return 0;
}

void process_init(void) {
    for (int index = 0; index < PROCESS_MAX; index++) {
        process_table[index].state = PROCESS_UNUSED;
        process_table[index].address_space.directory = 0;
        process_table[index].has_kernel_context = 0;
        process_table[index].kernel_stack_used = 0;
        process_table[index].mailbox_head = 0;
        process_table[index].mailbox_count = 0;
    }
    idle_process = &process_table[0];
    idle_process->pid = 0;
    idle_process->parent_pid = 0;
    idle_process->uid = 0;
    idle_process->state = PROCESS_RUNNING;
    copy_name(idle_process->name, "kernel");
    current_process = idle_process;
    next_pid = 1;
    scheduler_enabled = 0;
    recovery_requested = 0;
    gdt_set_esp0((uint32_t)(uintptr_t)(idle_process->kernel_stack + PROCESS_KERNEL_STACK_SIZE));
}

void process_enable_scheduler(void) {
    scheduler_enabled = 1;
}

int scheduler_is_enabled(void) {
    return scheduler_enabled;
}

void process_disable_scheduler(void) {
    scheduler_enabled = 0;
    current_process = idle_process;
    idle_process->state = PROCESS_RUNNING;
    memory_address_space_activate(0);
    gdt_set_esp0((uint32_t)(uintptr_t)(idle_process->kernel_stack + PROCESS_KERNEL_STACK_SIZE));
}

void process_set_recovery_hook(void (*hook)(void)) {
    recovery_hook = hook;
}

int process_pid1_alive(void) {
    process_t *init = process_by_pid(1);
    return init && (init->state == PROCESS_RUNNING || init->state == PROCESS_READY ||
                    init->state == PROCESS_BLOCKED);
}

static void reap_children_of(process_t *parent) {
    for (int index = 0; index < PROCESS_MAX; index++) {
        process_t *child = &process_table[index];
        if (child->state == PROCESS_UNUSED || child->parent_pid != parent->pid) continue;
        if (child->state == PROCESS_ZOMBIE) {
            child->state = PROCESS_UNUSED;
            memory_address_space_destroy(&child->address_space);
        } else {
            child->parent_pid = 1;
        }
    }
}

static process_t *reserve_slot(void) {
    for (int index = 0; index < PROCESS_MAX; index++) {
        if (process_table[index].state == PROCESS_UNUSED) return &process_table[index];
    }
    return 0;
}

static int spawn_common(const char *path, const char *args, int parent_pid, int forced_pid) {
    static char kernel_path[PROCESS_ARGS_SIZE];
    static char kernel_args[PROCESS_ARGS_SIZE];
    process_t *process = reserve_slot();
    uint32_t entry_point;
    if (!process || !path) return -1;
    copy_name(kernel_path, path);
    copy_args(kernel_args, sizeof(kernel_args), args);
    memory_address_space_destroy(&process->address_space);
    if (memory_address_space_create(&process->address_space) < 0) {
        process->state = PROCESS_UNUSED;
        return -1;
    }
    process->state = PROCESS_RUNNING;
    process->exit_status = -1;
    process->entry_point = 0;
    process->has_kernel_context = 0;
    process->kernel_stack_used = 0;
    if (forced_pid > 0) {
        process->pid = forced_pid;
        if (next_pid <= forced_pid) next_pid = forced_pid + 1;
    } else {
        process->pid = next_pid++;
    }
    process->parent_pid = parent_pid;
    process->uid = (uint32_t)auth_current_uid();
    copy_name(process->name, kernel_path);
    copy_args(process->args, sizeof(process->args), kernel_args);
    process->mailbox_head = 0;
    process->mailbox_count = 0;
    if (memory_user_map_into(&process->address_space,
                             PROCESS_USER_STACK_TOP - PROCESS_USER_STACK_SIZE,
                             PROCESS_USER_STACK_SIZE, 1) < 0) {
        memory_address_space_destroy(&process->address_space);
        process->state = PROCESS_UNUSED;
        return -1;
    }
    if (elf_load_into(&process->address_space, kernel_path, &entry_point) < 0) {
        memory_address_space_destroy(&process->address_space);
        process->state = PROCESS_UNUSED;
        return -1;
    }
    process->entry_point = entry_point;
    process->state = PROCESS_READY;
    vga_log_ok("Started %s", process->name);
    return process->pid;
}

int process_spawn_user(const char *path, const char *args) {
    return spawn_common(path, args, process_getpid(), 0);
}

int process_spawn_and_wait(const char *path, const char *args) {
    if (!scheduler_enabled) return -1;
    int pid = spawn_common(path, args, process_getpid(), 0);
    if (pid < 0) return -1;
    return process_wait(pid, 1, 0);
}

int process_wait(int pid, int block, int *status) {
    for (;;) {
        uint32_t flags = interrupts_save_and_disable();
        int found = 0;
        for (int index = 0; index < PROCESS_MAX; index++) {
            process_t *child = &process_table[index];
            if (child->state == PROCESS_UNUSED) continue;
            if (child->parent_pid != process_getpid()) continue;
            if (pid > 0 && child->pid != pid) continue;
            found = 1;
            if (child->state == PROCESS_ZOMBIE) {
                int exit_status = child->exit_status;
                int child_pid = child->pid;
                memory_address_space_destroy(&child->address_space);
                child->state = PROCESS_UNUSED;
                if (status) *status = exit_status;
                interrupts_restore(flags);
                return child_pid;
            }
        }
        if (!block || !found || !scheduler_enabled) {
            if (status) *status = -1;
            interrupts_restore(flags);
            return found ? 0 : -1;
        }
        current_process->state = PROCESS_BLOCKED;
        process_schedule();
        interrupts_restore(flags);
    }
}

int process_getpid(void) {
    return current_process ? current_process->pid : 0;
}

int process_getuid(void) {
    if (!current_process) return (int)auth_current_uid();
    return (int)current_process->uid;
}

void process_get_args(char *destination, uint32_t capacity) {
    if (!destination || !capacity) return;
    copy_args(destination, capacity, current_process ? current_process->args : 0);
}

static void notify_parent(process_t *process) {
    process_t *parent = process_by_pid(process->parent_pid);
    if (!parent || parent->state != PROCESS_BLOCKED) return;
    parent->state = PROCESS_READY;
}

static void exit_current_process(int status) {
    process_t *process = current_process;
    if (!process) for (;;) asm volatile ("cli; hlt");
    if (process == idle_process) {
        vga_log_fail("user process faulted while kernel main active (status %d)", status);
        if (recovery_hook) {
            recovery_hook();
        }
        for (;;) asm volatile ("cli; hlt");
    }
    process->exit_status = status;
    process->state = PROCESS_ZOMBIE;
    reap_children_of(process);
    notify_parent(process);
    vga_log_info("Process %s (pid %d) exited with status %d", process->name, process->pid, status);
    if (process->pid == 1) recovery_requested = 1;
    process_schedule();
    for (;;) asm volatile ("cli; hlt");
}

void process_exit(int status) {
    exit_current_process(status);
}

void process_yield(void) {
    uint32_t flags = interrupts_save_and_disable();
    if (!scheduler_enabled) {
        if (current_process && current_process->state == PROCESS_BLOCKED) {
            current_process->state = PROCESS_RUNNING;
        }
        interrupts_restore(flags);
        return;
    }
    if (current_process && current_process->state == PROCESS_RUNNING) {
        current_process->state = PROCESS_READY;
    }
    process_schedule();
    interrupts_restore(flags);
}

void process_idle_yield(void) {
    if (!scheduler_enabled) return;
    process_schedule();
}

void process_schedule(void) {
    uint32_t flags = interrupts_save_and_disable();
    process_t *previous = current_process;
    process_t *next = 0;
    if (!scheduler_enabled) {
        interrupts_restore(flags);
        return;
    }
    for (int scan = 0; scan < PROCESS_MAX; scan++) {
        process_t *candidate = &process_table[scan];
        if (candidate->state != PROCESS_READY) continue;
        next = candidate;
        break;
    }
    if (!next) {
        if (previous && previous != idle_process &&
            (previous->state == PROCESS_BLOCKED || previous->state == PROCESS_ZOMBIE)) {
            next = idle_process;
        } else {
            interrupts_restore(flags);
            return;
        }
    }
    if (previous == next) {
        if (previous->state == PROCESS_READY) previous->state = PROCESS_RUNNING;
        interrupts_restore(flags);
        return;
    }
    if (previous->state == PROCESS_RUNNING) previous->state = PROCESS_READY;
    previous->kernel_stack_used = 1;
    {
        int resumed = process_context_save(&previous->kernel_context);
        if (resumed) {
            memory_address_space_activate(current_process->address_space.directory ?
                                          &current_process->address_space : 0);
            gdt_set_esp0((uint32_t)(uintptr_t)(current_process->kernel_stack +
                                               PROCESS_KERNEL_STACK_SIZE));
            interrupts_restore(flags);
            return;
        }
    }
    current_process = next;
    next->state = PROCESS_RUNNING;
    gdt_set_esp0((uint32_t)(uintptr_t)(next->kernel_stack + PROCESS_KERNEL_STACK_SIZE));
    memory_address_space_activate(&next->address_space);
    if (next->kernel_stack_used) {
        process_context_restore(&next->kernel_context);
    }
    process_enter_user_mode((void *)(uintptr_t)next->entry_point);
}

void process_idle_yield_done(void) {
    schedule_depth = 0;
}

int process_recovery_requested(void) {
    return recovery_requested;
}

void process_take_recovery(void) {
    recovery_requested = 0;
    if (recovery_hook) recovery_hook();
}

const process_t *process_current(void) {
    return current_process;
}

int process_ipc_send(int target_pid, const piux_ipc_msg_t *message) {
    process_t *target = process_by_pid(target_pid);
    uint8_t slot;
    if (!target || target == current_process) return -1;
    if (target->mailbox_count >= PROCESS_IPC_MAILBOX_SIZE) return -2;
    if (!message || message->magic != PIUX_IPC_MAGIC) return -3;
    slot = (uint8_t)((target->mailbox_head + target->mailbox_count) % PROCESS_IPC_MAILBOX_SIZE);
    target->mailbox[slot] = *message;
    target->mailbox_count++;
    if (target->state == PROCESS_BLOCKED) target->state = PROCESS_READY;
    return 0;
}

static void process_ipc_dequeue(process_t *process, piux_ipc_msg_t *message) {
    *message = process->mailbox[process->mailbox_head];
    process->mailbox_head = (uint8_t)((process->mailbox_head + 1) %
                                      PROCESS_IPC_MAILBOX_SIZE);
    process->mailbox_count--;
}

int process_ipc_try_recv(piux_ipc_msg_t *message) {
    if (!current_process || !message) return -1;
    uint32_t flags = interrupts_save_and_disable();
    if (current_process->mailbox_count == 0) {
        interrupts_restore(flags);
        return -2;
    }
    process_ipc_dequeue(current_process, message);
    interrupts_restore(flags);
    return 0;
}

int process_ipc_recv(piux_ipc_msg_t *message) {
    if (!current_process || !message) return -1;
    for (;;) {
        uint32_t flags = interrupts_save_and_disable();
        if (current_process->mailbox_count > 0) {
            process_ipc_dequeue(current_process, message);
            interrupts_restore(flags);
            return 0;
        }
        if (!scheduler_enabled) {
            interrupts_restore(flags);
            return -2;
        }
        current_process->state = PROCESS_BLOCKED;
        process_schedule();
        interrupts_restore(flags);
    }
}

int process_exec(const char *path) {
    if (!scheduler_enabled) return -1;
    int status = -1;
    int pid = spawn_common(path, 0, process_getpid(), 0);
    if (pid < 0) return -1;
    if (process_wait(pid, 1, &status) < 0) return -1;
    return status;
}

void process_handle_user_exception(uint32_t vector) {
    process_t *process = current_process;
    if (process && process != idle_process) {
        vga_log_fail("%s killed by exception 0x%x", process->name, vector);
        exit_current_process(128 + (int)vector);
    }
    vga_log_fail("user exception 0x%x with no user process active", vector);
    if (recovery_hook) {
        recovery_hook();
    }
    for (;;) asm volatile ("cli; hlt");
}
