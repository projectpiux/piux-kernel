#include <stdint.h>
#include "syscall.h"
#include "panic.h"

typedef struct {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed)) idt_pointer_t;

typedef struct {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t zero;
    uint8_t type_attributes;
    uint16_t offset_high;
} __attribute__((packed)) idt_entry_t;

static idt_entry_t idt[256];
extern void syscall_entry(void);
extern void timer_entry(void);
extern void mouse_entry(void);
extern uint32_t kernel_panic_stub_table[32];

static void idt_set_gate(uint8_t vector, void (*handler)(void), uint8_t attributes) {
    uint32_t address = (uint32_t)handler;
    idt[vector].offset_low = (uint16_t)address;
    idt[vector].selector = 0x08;
    idt[vector].zero = 0;
    idt[vector].type_attributes = attributes;
    idt[vector].offset_high = (uint16_t)(address >> 16);
}

void syscall_init(void) {
    idt_pointer_t pointer;
    for (int index = 0; index < 256; index++) {
        idt[index].offset_low = 0;
        idt[index].selector = 0;
        idt[index].zero = 0;
        idt[index].type_attributes = 0;
        idt[index].offset_high = 0;
    }
    /* DPL 3 so faults raised in ring 3 also land in the panic stub, which
       tears every user process down and reports the crash full screen. */
    for (uint32_t vector = 0; vector < 32; vector++) {
        idt_set_gate((uint8_t)vector, (void (*)(void))(uintptr_t)kernel_panic_stub_table[vector], 0xEE);
    }
    idt_set_gate(0x80, syscall_entry, 0xEE);
    idt_set_gate(0x20, timer_entry, 0x8e);
    idt_set_gate(0x2c, mouse_entry, 0x8e);
    pointer.limit = sizeof(idt) - 1;
    pointer.base = (uint32_t)idt;
    asm volatile ("lidt %0" : : "m"(pointer));
}