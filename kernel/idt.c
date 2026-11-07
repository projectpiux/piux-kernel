#include <stdint.h>
#include "syscall.h"
#include "process.h"
#include "vga_log.h"

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
extern uint32_t exception_stub_table[32];
extern void vga_puts(const char *text);
extern void vga_putc(char character);

static void exception_print_hex(uint32_t value) {
    static const char digits[] = "0123456789ABCDEF";
    for (int shift = 28; shift >= 0; shift -= 4)
        vga_putc(digits[(value >> shift) & 0x0f]);
}

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
    for (uint32_t vector = 0; vector < 32; vector++) {
        idt_set_gate((uint8_t)vector, (void (*)(void))(uintptr_t)exception_stub_table[vector], 0x8e);
    }
    idt_set_gate(0x80, syscall_entry, 0xEE);
    idt_set_gate(0x20, timer_entry, 0x8e);
    idt_set_gate(0x2c, mouse_entry, 0x8e);
    pointer.limit = sizeof(idt) - 1;
    pointer.base = (uint32_t)idt;
    asm volatile ("lidt %0" : : "m"(pointer));
}

void exception_dispatch(uint32_t *frame) {
    uint32_t vector = frame[8];
    uint32_t code_selector = frame[11];
    if ((code_selector & 3U) == 3U) process_handle_user_exception(vector);
    {
        const process_t *faulted = process_current();
        vga_puts("\nKernel page fault: cr2=0x");
        {
            uint32_t cr2;
            asm volatile ("mov %%cr2, %0" : "=r"(cr2));
            exception_print_hex(cr2);
        }
        vga_puts(" eip=0x");
        exception_print_hex(frame[10]);
        if (faulted) {
            vga_puts(" while servicing ");
            vga_puts(faulted->name);
        }
        vga_putc('\n');
    }
    for (;;) asm volatile ("cli; hlt");
}