# Piux Modules

## How the modular monolithic kernel works

Piux boots a single, small core binary (`kernel.elf`) in kernel mode. At boot
the core loads a set of subsystem modules from the ext2 filesystem and links
them into itself at runtime. Everything keeps running in one address space and
one privilege ring — the architecture stays monolithic — but the subsystems
are compiled, versioned and shipped as independent files that the core can
load, list, unload and reload while the system is up.

```
kernel.elf (core)                 /lib/modules/*.mod
+--------------------+            +------------------+
| loader + ksym tab  | <--------- | vfs.mod          |
| vfs dispatch stubs |            | commands.mod     |
| shell, syscalls,   |            | net.mod          |
| ext2/ata, video,   |            | installer.mod    |
| process, http      |            +------------------+
+--------------------+              ld -r relocatable
                                    i386 ELF (ET_REL)
```

### Module ABI

A module is a relocatable i386 ELF object produced by `ld -r`. It must define
one global symbol, `piux_module_descriptor`, placed in the `.piux.module`
section (see `kernel/piux_module.h`):

```c
static int my_module_init(void) { ...; return 0; }

PIUX_MODULE(
    .name = "my_module",
    .version = PIUX_MODULE_VERSION,
    .deps = (const char *const[]){ "vfs" },
    .dep_count = 1,
    .init = my_module_init,
    .description = "What this module provides"
);
```

The descriptor carries a `PIUX_MODULE_SIGNATURE` (`PIUX`), an `abi` number
(`PIUX_MODULE_ABI` in the header) and a `release` string that must match the
core it is loaded into. A module built for a different release or a newer ABI
is rejected at load time instead of being executed against wrong layouts.

A module optionally exports a public symbol table through a second well-known
symbol, `piux_module_exports`:

```c
void piux_module_exports(const char *const **names,
                         const void *const **addresses,
                         uint32_t *count);
```

Only the symbols listed there become visible to the rest of the system
(see `bin/commands.c` for the pattern). Note that the loader resolves
`piux_module_exports` inside the module image itself, not through the global
symbol table — every module defines a symbol with that name, and the table
keeps only the first definition.

### The loader (`kernel/module.c`)

`module_load_by_name("vfs")` reads `/lib/modules/vfs.mod`, then:

1. **Parse.** The ELF header, section table and symbol table are validated
   (`ET_REL`, `EM_386`, ELF32 LSB, no TLS sections, no unsupported array
   sections). A relocatable object has no load addresses, so the loader
   computes its own flat layout: `SHF_ALLOC` sections (`.text`, `.data`,
   `.rodata`, `.bss`) are laid out in order with their natural alignment and
   `section->addr` is reused to hold that flat offset.
2. **Build the image.** `arena_allocate()` carves `image_size` bytes out of a
   4 MiB module arena (allocated once by `module_init()`); PROGBITS sections
   are copied from the file and NOBITS ones (`.bss`) are left zeroed. This is
   why the image is *not* a byte copy of the file: `.bss` occupies space only
   in the flat layout.
3. **Relocate.** Every `REL`/`RELA` section is applied to the image. Defined
   symbols resolve to `image_base + section->addr + value`; undefined symbols
   are looked up in the kernel symbol table (below). Unresolved names are
   reported verbatim in `module_last_detail`.
4. **Validate.** The relocated `piux_module_descriptor` is checked: strings
   are only valid *after* relocation, so name/version/release validation
   happens at this point, not before.
5. **Dependencies.** `deps` are loaded first, recursively, up to `MODULE_MAX`
   levels. A cycle simply reports the missing module, since nothing becomes
   visible until its `init()` has run.
6. **Activate.** The module's public export table is registered in the symbol
   table (owner = the module slot, so `module_unload` can drop them), then
   `init()` runs. `init() != 0` fails the load; `exit()` runs on unload.

The kernel symbol table (`ksym_*` in `module.c`) holds core exports registered
by `core_symbols_init()`/`core_services_init()`, plus everything each loaded
module exported. First definition wins: a module cannot silently override a
core symbol, but a module's exports are immediately available to the next
module loaded (this is how `commands.mod` depends on `vfs.mod`).

### Core/module boundaries

The core keeps small dispatch stubs so that nothing in the kernel links
against a subsystem implementation:

- `kernel/vfs_core.c` — `vfs_open`/`vfs_read`/... call through dispatch
  pointers that stay NULL (and fail cleanly) until `vfs.mod` fills them in its
  `init()`. `core_vfs_ready()` tells the shell whether the filesystem is up.
- `kernel/net_core.c` — byte-order and checksum primitives stay in the core;
  DNS/host resolution dispatches into `net.mod` when loaded.
- `kernel/kmain.c` — `dispatch_command_lookup` and `dispatch_installer_run`
  are bound to the first public export of the `commands` and `installer`
  modules after loading; with the module missing, every non-`mod` shell
  command reports `vfs module is not loaded` and startup falls back to the
  built-in default command.

Consequently, adding a shell command, changing the protocol stack or
replacing the installer is a module rebuild plus `mod reload`, not a kernel
rebuild.

### Using modules from the shell

The `mod` command is part of the core (like `kmod` on Linux), so it keeps
working even when every other module failed to load:

```
mod list              # name, version, state and description of each module
mod info vfs          # path, version, release, ABI, dependency list
mod load net          # load /lib/modules/net.mod (plus dependencies)
mod load-path <path>  # load from an explicit path
mod unload vfs        # run exit hook and drop the module's exported symbols
mod reload commands   # unload then load again
```

### Build and install

```sh
make modules                  # build/modules/{vfs,commands,net,installer}.mod
make install-modules          # copy them to /lib/modules on build/ext2.img
make                          # kernel + modules + user programs
make install-user-programs    # desktop.elf + the modules on the disk image
```

Each module is compiled from the same sources as the core (for example
`kernel/vfs.c` for `vfs.mod`) with `-DPIUX_MODULE_<NAME>` and
`-DPIUX_MODULE_RELEASE=<core release>`, then linked with `ld -r`. The module
files are written into `/lib/modules/` on the ext2 image by `debugfs`.

### Current modules

| Module | Provides | Depends on |
|---|---|---|
| `vfs` | Unix-like VFS: descriptor table, `/dev` nodes, permissions; fills the core dispatch slots | — |
| `installer` | First-boot TUI installer | — |
| `net` | ARP, IPv4, ICMP, UDP, TCP, DHCP, DNS helpers | — |
| `commands` | Shell built-ins (`ls`, `cat`, `nano`, `net`, `pix`, ...) exported through `commands_lookup` | `vfs` |

### Limitations

- The module arena is a bump allocator with no free; unloaded module images
  stay resident until reboot (documented behaviour of `module_unload`).
- Relocations are limited to the flat 386 set; GOT/TLS-based code cannot be
  loaded.
- Symbol resolution is by name against a fixed-size table; there is no
  versioned per-symbol ABI, only the module-level `abi`/`release` check.
- `commands_lookup` is bound once at boot; reloading `commands` updates the
  module list but the shell keeps calling the previously bound entry until the
  next boot.
