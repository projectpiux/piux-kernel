#include <stdint.h>
#include "gdt.h"

typedef struct {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed)) gdt_pointer_t;

typedef struct {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t base_middle;
    uint8_t access;
    uint8_t granularity;
    uint8_t base_high;
} __attribute__((packed)) gdt_entry_t;

typedef struct {
    uint32_t previous;
    uint32_t esp0;
    uint32_t ss0;
    uint32_t esp1;
    uint32_t ss1;
    uint32_t esp2;
    uint32_t ss2;
    uint32_t cr3;
    uint32_t eip;
    uint32_t eflags;
    uint32_t eax;
    uint32_t ecx;
    uint32_t edx;
    uint32_t ebx;
    uint32_t esp;
    uint32_t ebp;
    uint32_t esi;
    uint32_t edi;
    uint32_t es;
    uint32_t cs;
    uint32_t ss;
    uint32_t ds;
    uint32_t fs;
    uint32_t gs;
    uint32_t ldt;
    uint16_t trap;
    uint16_t iomap_base;
} __attribute__((packed)) tss_t;

static gdt_entry_t entries[6];
static tss_t task_state __attribute__((aligned(16)));
extern void gdt_flush(uint32_t address);

static uint8_t user_trap_stack[8192] __attribute__((aligned(16)));
static const uint8_t *user_trap_stack_top = user_trap_stack + sizeof(user_trap_stack);

static void set_entry(int index, uint32_t base, uint32_t limit, uint8_t access, uint8_t granularity) {
    entries[index].limit_low = limit & 0xffff;
    entries[index].base_low = base & 0xffff;
    entries[index].base_middle = (base >> 16) & 0xff;
    entries[index].access = access;
    entries[index].granularity = (limit >> 16) & 0x0f;
    entries[index].granularity |= granularity & 0xf0;
    entries[index].base_high = (base >> 24) & 0xff;
}

void gdt_init(void) {
    gdt_pointer_t pointer;
    for (uint32_t byte = 0; byte < sizeof(task_state); byte++) ((uint8_t *)&task_state)[byte] = 0;
    set_entry(0, 0, 0, 0, 0);
    set_entry(1, 0, 0xffffffff, 0x9a, 0xcf);
    set_entry(2, 0, 0xffffffff, 0x92, 0xcf);
    set_entry(3, 0, 0xffffffff, 0xfa, 0xcf);
    set_entry(4, 0, 0xffffffff, 0xf2, 0xcf);
    set_entry(5, (uint32_t)(uintptr_t)&task_state, sizeof(task_state) - 1, 0x89, 0x40);
    task_state.esp0 = (uint32_t)(uintptr_t)user_trap_stack_top;
    task_state.ss0 = 0x10;
    task_state.iomap_base = sizeof(task_state);
    pointer.base = (uint32_t)(uintptr_t)entries;
    pointer.limit = sizeof(entries) - 1;
    gdt_flush((uint32_t)(uintptr_t)&pointer);
    asm volatile ("ltr %%ax" : : "a"((uint16_t)0x28));
}