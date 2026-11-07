#ifndef CONTEXT_H
#define CONTEXT_H

#include <stdint.h>
#include "process.h"

int process_context_save(process_context_t *context);
void process_context_restore(uint32_t *kernel_esp, int value) __attribute__((noreturn));
void process_kernel_to_user(process_t *process) __attribute__((noreturn));

#endif
