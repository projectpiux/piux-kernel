#ifndef PANIC_H
#define PANIC_H

#include <stdint.h>

/*
 * Kernel crash registers captured by the exception stubs in
 * kernel/core/panic_entry.asm before any C code runs. The field order is
 * mirrored by the stub, keep the two in sync.
 */
typedef struct {
    uint32_t eax, ebx, ecx, edx, esi, edi, ebp, esp;
    uint32_t vector, error_code, eip, cs, eflags;
    uint32_t cr0, cr2, cr3, cr4;
    uint32_t user_esp, user_ss;
} panic_frame_t;

/*
 * Stops the whole system and paints a full-screen kernel panic report over
 * every user-space surface. Interrupts are masked and the scheduler is
 * switched off, so no user service can draw over the screen again.
 */
void kernel_panic(const panic_frame_t *frame, const char *message) __attribute__((noreturn));

/* Same effect, for C code that detects a fatal condition by itself. */
void kernel_panic_msg(const char *message) __attribute__((noreturn));

#endif
