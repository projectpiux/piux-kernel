# User and Kernel Space

## Current boundary

The kernel keeps its identity mapping supervisor-only. `process_exec` creates a
private page directory for each synchronous ELF execution, assigns it a
temporary PID/UID process record, and maps user pages in the virtual range
`0x80000000` to `0x84000000`. ELF `PT_LOAD` segments get user permissions,
non-writable segments are made read-only, and segments cannot overlap the
64 KiB user stack at the top of the range. Syscall buffers are checked against
the active page tables; output buffers must also be writable. A user-mode CPU
exception ends that execution and returns its status to the kernel caller.

PDE and its registered applications are built as `build/user/desktop.elf`, not
linked into `kernel.elf`. The desktop uses `int 0x80` services for batched
video drawing, keyboard and mouse input, system and account snapshots, and
captured shell-command output. Startup and the `pde` command load
`/desktop.elf` with `process_exec`.

Build user programs with `make user-programs`. `make install-user-programs`
installs the desktop ELF on `build/ext2.img`; `make run` and `fast-all.sh`
perform this automatically. `build/user/isolation_test.elf` attempts to read
kernel memory and should exit with status `142` (page fault) when installed
and launched with `run /isolation_test`.

## Remaining Work

User ELF execution is synchronous; `process_yield` does not schedule another
user process. `process_create` callbacks, the shell and its command handlers
execute in kernel mode — although the command *implementations* now live in
the `commands` module, loaded at runtime (see `docs/MODULES.md`) — and the
desktop terminal invokes kernel shell handlers through a checked syscall
rather than running each command as a separate user process.

The frame allocator is limited to the first 64 MiB and further capped by the
contiguous upper-memory size reported by Multiboot; it does not yet consume the
full Multiboot memory map. The current 32-bit non-PAE setup has no NX bit, so
the CPU cannot enforce non-executable user data pages.