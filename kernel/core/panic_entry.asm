BITS 32

; Piux kernel panic entry stubs.
;
; Installed as the high-half (DPL 3) descriptor of every exception gate in
; kernel/core/idt.c. When a fault happens in kernel mode the CPU has already
; pushed eflags/cs/eip (+ error code) and these stubs push a clean, interrupt
; disabled context before calling kernel_panic(), so the report can be drawn
; regardless of which stack the kernel was using.

section .bss
align 16
panic_stack_bottom:
    resb 32768
panic_stack_top:

section .text

extern kernel_panic

%macro PANIC_STUB_NO_ERROR 1
panic_stub_%1:
    push dword 0
    push dword %1
    jmp panic_common
%endmacro

%macro PANIC_STUB_ERROR 1
panic_stub_%1:
    push dword %1
    jmp panic_common
%endmacro

section .text

PANIC_STUB_NO_ERROR 0
PANIC_STUB_NO_ERROR 1
PANIC_STUB_NO_ERROR 2
PANIC_STUB_NO_ERROR 3
PANIC_STUB_NO_ERROR 4
PANIC_STUB_NO_ERROR 5
PANIC_STUB_NO_ERROR 6
PANIC_STUB_NO_ERROR 7
PANIC_STUB_ERROR 8
PANIC_STUB_NO_ERROR 9
PANIC_STUB_ERROR 10
PANIC_STUB_ERROR 11
PANIC_STUB_ERROR 12
PANIC_STUB_ERROR 13
PANIC_STUB_ERROR 14
PANIC_STUB_NO_ERROR 15
PANIC_STUB_NO_ERROR 16
PANIC_STUB_ERROR 17
PANIC_STUB_NO_ERROR 18
PANIC_STUB_NO_ERROR 19
PANIC_STUB_NO_ERROR 20
PANIC_STUB_NO_ERROR 21
PANIC_STUB_NO_ERROR 22
PANIC_STUB_NO_ERROR 23
PANIC_STUB_NO_ERROR 24
PANIC_STUB_NO_ERROR 25
PANIC_STUB_NO_ERROR 26
PANIC_STUB_NO_ERROR 27
PANIC_STUB_NO_ERROR 28
PANIC_STUB_NO_ERROR 29
PANIC_STUB_NO_ERROR 30
PANIC_STUB_NO_ERROR 31

; panic_common expects, on the stack pushed by the stub:
;   [esp+0]  vector
;   [esp+4]  error code
;   [esp+8]  eip
;   [esp+12] cs
;   [esp+16] eflags
;   [esp+20] esp   (only present when the fault came from user mode)
;   [esp+24] ss    (only present when the fault came from user mode)
;
; The report struct is written field by field so its layout always matches
; kernel/core/panic.h regardless of the pushad ordering.
panic_common:
    cli
    cld
    pushad
    mov esi, esp

    mov edi, panic_stack_top
    sub edi, 128
    mov esp, edi

    ; pushad order from the stack pointer upwards (esi):
    ;   +0 edi  +4 esi  +8 ebp  +12 esp  +16 ebx  +20 edx  +24 ecx  +28 eax
    ; then the stub-pushed vector and error code, then the CPU frame:
    ;   +32 vector  +36 error code  +40 eip  +44 cs  +48 eflags
    ;   +52 user esp  +56 user ss (ring 3 faults only)
    mov eax, [esi + 28]
    mov [edi + 0], eax
    mov eax, [esi + 16]
    mov [edi + 4], eax
    mov eax, [esi + 24]
    mov [edi + 8], eax
    mov eax, [esi + 20]
    mov [edi + 12], eax
    mov eax, [esi + 4]
    mov [edi + 16], eax
    mov eax, [esi + 0]
    mov [edi + 20], eax
    mov eax, [esi + 8]
    mov [edi + 24], eax
    mov eax, [esi + 12]
    mov [edi + 28], eax

    mov eax, [esi + 32]
    mov [edi + 32], eax
    mov eax, [esi + 36]
    mov [edi + 36], eax
    mov eax, [esi + 40]
    mov [edi + 40], eax
    mov eax, [esi + 44]
    mov [edi + 44], eax
    mov eax, [esi + 48]
    mov [edi + 48], eax

    mov eax, cr0
    mov [edi + 52], eax
    mov eax, cr2
    mov [edi + 56], eax
    mov eax, cr3
    mov [edi + 60], eax
    mov eax, cr4
    mov [edi + 64], eax

    mov eax, [esi + 52]
    mov [edi + 68], eax
    mov eax, [esi + 56]
    mov [edi + 72], eax

    push dword 0
    push edi
    call kernel_panic

.halt:
    cli
    hlt
    jmp .halt

section .rodata
global kernel_panic_stub_table
kernel_panic_stub_table:
%assign panic_index 0
%rep 32
    dd panic_stub_%+panic_index
%assign panic_index panic_index + 1
%endrep

section .text
