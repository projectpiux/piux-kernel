# User and Kernel Space

## Current boundary

The kernel keeps its identity mapping supervisor-only. Every user process gets
its own page directory over `0x80000000`-`0x84000000`, a 4 KiB kernel stack and
a table entry managed by a round-robin preemptive scheduler (10 timer ticks per
slice). `process_spawn_user` loads an ELF asynchronously; `SYS_WAIT` blocks a
parent until a child exits, zombies keep the exit status until reaped, and
orphans are re-parented to PID 1. `SYS_IPC` provides a small generic mailbox
(see `docs/INIT.md` for the versioned PIUX-IPC/1 protocol built on top).
PID 1 is `/sbin/init.elf`; if it is missing or dies the kernel falls back to
the recovery shell. ELF `PT_LOAD` segments get user permissions, non-writable
segments are made read-only, and segments cannot overlap the 64 KiB user stack
at the top of the range. Syscall buffers are checked against the active page
tables; output buffers must also be writable. A user-mode CPU exception kills
only the offending process and reports it on the VGA console.

PDE and its registered applications are built as `build/user/desktop.elf`, not
linked into `kernel.elf`. The desktop uses `int 0x80` services for batched
video drawing, keyboard and mouse input, system and account snapshots, and
captured shell-command output. Startup and the `pde` command load
`/bin/desktop.elf` with `process_exec`.

Build user programs with `make user-programs`. `make install-user-programs`
installs the desktop ELF, `/sbin/init.elf` and `/usr/bin/initctl` on
`build/ext2.img`; `make run` and `fast-all.sh` perform this automatically.
`build/user/isolation_test.elf` attempts to read kernel memory and should exit
with status `142` (page fault) when installed and launched with
`run /isolation_test`.

## Remaining Work

The scheduler is round-robin only (no priorities, no timer preemption: context
switches happen on blocking syscalls), there is no copy-on-write fork, and the
ELF loader still stages whole files in a 256 KiB static buffer. ELF images are
mapped and copied into a target address space through the new
`memory_user_map_into`/`memory_user_protect_into`/`memory_user_copy_into`
primitives, so spawning never switches page directories; user pointers passed
to `SYS_SPAWN` are copied into kernel memory before use. The kernel VGA log and
user graphics share one framebuffer, so kernel messages can appear over a
running graphical session until the next redraw. The frame allocator
is limited to the first 64 MiB and further capped by the contiguous
upper-memory size reported by Multiboot; it does not yet consume the full
Multiboot memory map. The current 32-bit non-PAE setup has no NX bit, so the
CPU cannot enforce non-executable user data pages.