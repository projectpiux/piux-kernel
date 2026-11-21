/* Piux x86 (i386) kernel linker script.
 *
 * The kernel is loaded by Multiboot1 at 1 MiB and linked at the same address
 * (identity mapping). arch/<name>/linker/kernel.ld is selected by the Makefile
 * for ARCH=<name>, next to arch/<name>/boot/.
 */

ENTRY(_start)

SECTIONS
{
    . = 0x00100000;
    
    .multiboot ALIGN(4K) :
    {
        *(.multiboot)
    }
    
    .text ALIGN(4K) :
    {
        *(.text)
    }
    
    .rodata ALIGN(4K) :
    {
        *(.rodata)
        . = ALIGN(4K);
        _os_infos_start = .;
        KEEP(*(SORT_BY_NAME(.os_infos)))
        _os_infos_end = .;
    }
    
    .data ALIGN(4K) :
    {
        *(.data)
    }
    
    .bss ALIGN(4K) :
    {
        *(COMMON)
        *(.bss)
    }

    __kernel_end = .;
}
