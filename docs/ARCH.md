# Architecture ports (`arch/`)

Piux keeps everything that depends on the target CPU in `arch/<name>/`, and
everything else (scheduler, VFS, drivers, desktop) architecture-independent.
Today exactly one port exists: **`arch/x86`** (32-bit x86 / i386).

```text
arch/
└── x86/
    ├── Makefile.mk        port build rules, toolchain flags and targets
    ├── boot/
    │   └── bootx.asm      Multiboot1 entry stub -> kernel_main
    ├── linker/
    │   └── kernel.ld      kernel linker script (links at 1 MiB)
    └── include/
        └── arch/
            └── arch.h     PIUX_ARCH_* description header
```

## Selecting a port

```bash
make                      # ARCH defaults to x86
make ARCH=x86             # explicit
make ARCH=x86 clean       # clean that port's build tree only
```

The top-level `Makefile` validates `ARCH` against the available
`arch/*/Makefile.mk` files and then includes the selected one; the port
makefile owns every target (`all`, `iso`, `modules`, `user-programs`,
`install-user-programs`, `run`, `debug`, `clean`).

Object files, the kernel ELF, the modules and the ISO are all written under
`build/<arch>/`:

```text
build/
├── x86/
│   ├── kernel.elf
│   ├── piux.iso
│   ├── modules/{vfs,commands,net,installer}.mod
│   ├── user/{hello,isolation_test,desktop}.elf
│   ├── init/init.elf
│   └── bin/initctl.elf
├── ext2.img              shared disk image (installed per port)
└── .arch                 port used by the last build
```

`fast-all.sh` honours `PIUX_ARCH` (default `x86`) and looks for the ISO in
`build/$ARCH/piux.iso`.

## What a port provides

| Piece | Purpose |
|---|---|
| `boot/` | The code the bootloader jumps to. For x86 this is the Multiboot1 stub that sets up a stack and calls `kernel_main(magic, info)` in `kernel/core/kmain.c`. |
| `linker/` | Kernel linker script: load address, section layout, `_os_infos_*` symbols. |
| `include/arch/arch.h` | Machine-readable description: name, bit width, ISA, boot protocol, toolchain target triples and the user-space load address. |
| `Makefile.mk` | Compiler/assembler/linker flags and the object layout for that ISA. |

The kernel sources include headers with relative paths inside `kernel/`, so a
port never forks kernel code: it only supplies the entry stub, the linker
script and the toolchain configuration.

## Adding a port

1. Create `arch/<name>/` with `boot/`, `linker/`, `include/arch/arch.h` and a
   `Makefile.mk` modelled on `arch/x86/Makefile.mk`.
2. Keep the same entry contract: the boot code must call `kernel_main` with the
   platform's boot information, and `kernel_main` must still leave the system
   in a state the generic subsystems can initialise (32-bit flat addressing,
   paging helpers, interrupt entry points).
3. Make sure any assembly under `kernel/core/` referenced by the new ISA is
   either replaced by port-local files or kept in sync.
4. Run `make ARCH=<name>` and `make ARCH=<name> run`.

Until the shared x86-only pieces (GDT/TSS, IDT stubs, PIT, paging, PS/2
keyboard, ATA/PCI ports) are pulled behind port hooks, a second port is
expected to reuse them, so treat `arch/x86` as the reference implementation
rather than a strictly separated HAL.
