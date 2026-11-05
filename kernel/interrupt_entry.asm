BITS 32

section .text
extern timer_tick
extern outb
extern mouse_irq_handler
extern exception_dispatch
global timer_entry
global mouse_entry
global exception_stub_table

%macro EXCEPTION_NO_ERROR 1
exception_stub_%1:
    push dword 0
    push dword %1
    jmp exception_common
%endmacro

%macro EXCEPTION_ERROR 1
exception_stub_%1:
    push dword %1
    jmp exception_common
%endmacro

EXCEPTION_NO_ERROR 0
EXCEPTION_NO_ERROR 1
EXCEPTION_NO_ERROR 2
EXCEPTION_NO_ERROR 3
EXCEPTION_NO_ERROR 4
EXCEPTION_NO_ERROR 5
EXCEPTION_NO_ERROR 6
EXCEPTION_NO_ERROR 7
EXCEPTION_ERROR 8
EXCEPTION_NO_ERROR 9
EXCEPTION_ERROR 10
EXCEPTION_ERROR 11
EXCEPTION_ERROR 12
EXCEPTION_ERROR 13
EXCEPTION_ERROR 14
EXCEPTION_NO_ERROR 15
EXCEPTION_NO_ERROR 16
EXCEPTION_ERROR 17
EXCEPTION_NO_ERROR 18
EXCEPTION_NO_ERROR 19
EXCEPTION_NO_ERROR 20
EXCEPTION_NO_ERROR 21
EXCEPTION_NO_ERROR 22
EXCEPTION_NO_ERROR 23
EXCEPTION_NO_ERROR 24
EXCEPTION_NO_ERROR 25
EXCEPTION_NO_ERROR 26
EXCEPTION_NO_ERROR 27
EXCEPTION_NO_ERROR 28
EXCEPTION_NO_ERROR 29
EXCEPTION_NO_ERROR 30
EXCEPTION_NO_ERROR 31

exception_common:
    pusha
    push esp
    call exception_dispatch
    add esp, 4
    popa
    add esp, 8
    iretd

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
    mov gs, ax

section .rodata
exception_stub_table:
%assign exception_index 0
%rep 32
    dd exception_stub_%+exception_index
%assign exception_index exception_index + 1
%endrep

section .text