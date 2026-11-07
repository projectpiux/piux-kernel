BITS 32

section .text
global process_context_save
global process_context_restore
extern process_exit

process_context_save:
    mov eax, [esp+4]
    mov ecx, [esp]
    mov [eax], ecx
    lea edx, [esp+4]
    mov [eax+4], edx
    mov [eax+8], ebp
    mov [eax+12], ebx
    mov [eax+16], esi
    mov [eax+20], edi
    pushfd
    pop edx
    mov [eax+24], edx
    xor eax, eax
    ret

process_context_restore:
    mov eax, [esp+4]
    mov ebp, [eax+8]
    mov ebx, [eax+12]
    mov esi, [eax+16]
    mov edi, [eax+20]
    mov ecx, [eax]
    mov esp, [eax+4]
    mov eax, [eax+24]
    push eax
    popfd
    mov eax, 1
    jmp ecx

process_start_user:
    mov eax, [esp+4]
    mov ecx, [esp+8]
    mov dx, 0x23
    mov ds, dx
    mov es, dx
    mov fs, dx
    mov gs, dx
    push dword 0x23
    push ecx
    pushfd
    pop edx
    or edx, 0x200
    push edx
    push dword 0x1b
    push eax
    iretd
