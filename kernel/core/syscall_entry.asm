BITS 32

section .text
extern syscall_dispatch
global syscall_entry

syscall_entry:
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
    lea eax, [esp + 16]
    push eax
    call syscall_dispatch
    add esp, 4
    pop gs
    pop fs
    pop es
    pop ds
    popa
    iretd