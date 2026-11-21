; Piux x86 (i386) entry stub.
;
; GRUB jumps here in 32-bit protected mode with a Multiboot1 hand-off. The
; stub sets up a bootstrap stack and calls the architecture-independent
; kernel_main(magic, multiboot_info) in kernel/core/kmain.c.
;
; Selected by the Makefile for ARCH=x86, which is the only port for now.

BITS 32

MULTIBOOT_MAGIC equ 0x1badb002
MULTIBOOT_FLAGS equ 0x00000007
MULTIBOOT_CHECKSUM equ -(MULTIBOOT_MAGIC + MULTIBOOT_FLAGS)

section .multiboot
align 4
    dd MULTIBOOT_MAGIC
    dd MULTIBOOT_FLAGS
    dd MULTIBOOT_CHECKSUM
    dd 0
    dd 0
    dd 0
    dd 32

section .bootstrap_stack
align 4
    resb 16384

section .text
extern kernel_main

global _start
_start:
    mov esp, stack_top
    cli

    push ebx
    push eax

    call kernel_main

    cli
    hlt
    jmp _start

section .bss
align 4
stack_bottom:
    resb 16384
global stack_top
stack_top: