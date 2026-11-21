#ifndef ARCH_ARCH_H
#define ARCH_ARCH_H

/*
 * Architecture description for x86 (i386).
 *
 * Every arch/<name>/include/arch directory provides the same header so that
 * architecture-independent code (and the tooling) can query the target
 * without knowing which port is being built.
 */

#define PIUX_ARCH_NAME      "x86"
#define PIUX_ARCH_BITS      32
#define PIUX_ARCH_ISA       "i386"
#define PIUX_ARCH_FAMILY    "x86"
#define PIUX_ARCH_BOOT      "GRUB Multiboot1"

/* GNU target triples used by the toolchain. */
#define PIUX_ARCH_ELF_TARGET "elf32-i386"
#define PIUX_ARCH_OBJCOPY_TARGET "elf32-i386"
#define PIUX_ARCH_OBJCOPY_BFD "i386"

/* USER_LOAD_ADDR must match the load address in the user linker script. */
#define PIUX_ARCH_USER_LOAD_ADDR 0x80000000u

#endif
