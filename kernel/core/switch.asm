BITS 32

section .text
global process_enter_user_mode

%define PROCESS_USER_STACK_TOP 0x84000000

process_enter_user_mode:
    mov ecx, [esp+4]
    mov dx, 0x23
    mov ds, dx
    mov es, dx
    mov fs, dx
    mov gs, dx
    push dword 0x23
    push dword PROCESS_USER_STACK_TOP
    pushfd
    pop edx
    or edx, 0x200
    push edx
    push dword 0x1b
    push ecx
    iretd
