BITS 32

section .text
extern timer_tick
extern process_timer_tick
extern outb
extern mouse_irq_handler
global timer_entry
global mouse_entry
timer_entry:
    pusha
    push ds
    push es
    push fs
    push gs
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    call timer_tick
    mov al, 0x20
    mov dx, 0x20
    out dx, al
    pop gs
    pop fs
    pop es
    pop ds
    popa
    iretd

mouse_entry:
    pusha
    push ds
    push es
    push fs
    push gs
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    call mouse_irq_handler
    mov al, 0x20
    mov dx, 0xa0
    pop gs
    pop fs
    pop es
    pop ds
    out dx, al
    mov dx, 0x20
    out dx, al
    popa
    iretd
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
