# Everything is a file

Piux follows Unix's *everything is a file* rule: anything a program can talk to
is reached through the VFS with the same four calls.

```c
int fd = vfs_open(path, flags);
vfs_read(fd, buffer, size);
vfs_write(fd, buffer, size);
vfs_close(fd);
```

There is no separate API for "read the clock", "read memory usage" or "list the
processes": those are ordinary files under `/proc`, opened and read like any
other file. A shell command such as `cat` needs no special case.

## Namespaces

| Path | Kind | Backed by |
|---|---|---|
| `/dev/console`, `/dev/tty`, `/dev/stdin`, `/dev/stdout`, `/dev/stderr` | character | keyboard (read) and the active console sink (write) |
| `/dev/keyboard` | character | raw keyboard queue (read) |
| `/dev/null` | character | discards writes, reads EOF |
| `/dev/zero` | character | reads zero bytes forever, discards writes |
| `/dev/hda` | block | raw ATA sectors, addressed by byte offset |
| `/proc/*` | generated | live kernel state, rendered on read |
| anything else | regular | ext2 (or RAMFS when no disk is mounted) |

`/dev` and `/proc` are in-memory namespaces: they exist whether or not a disk is
mounted and are read-only from userland.

## `/proc`

Each entry is generated from kernel state the moment it is read, so it never
goes stale:

| File | Contents |
|---|---|
| `/proc/uptime` | seconds since boot (`centiseconds`) |
| `/proc/meminfo` | `MemTotal`, `MemFree`, kernel text and lower/upper memory, in kB |
| `/proc/version` | kernel banner |
| `/proc/cmdline` | boot command line |
| `/proc/uname` | simple `key=value` identity lines |
| `/proc/mounts` | mounted filesystems (`/dev/hda / ext2 rw`, `proc /proc proc ro`, ...) |
| `/proc/net/dev` | `eth0` MAC and IPv4 configuration, when the NIC is up |
| `/proc/net/route` | default route |
| `/proc/<pid>/status` | name, state, pid, ppid, uid, entry point, args |
| `/proc/<pid>/cmdline` | argv-style command line |
| `/proc/<pid>/stat` | pid, comm, state, ppid |
| `/proc/self/...` | the same entries for the calling process |

```text
piux > cat /proc/version
Piux Rolling release (piux-1.0) i386 gcc/syscall-int80

piux > cat /proc/meminfo
MemTotal:       523000 kB
MemFree:        ...
```

## Implementation

The core owns only the namespace plumbing; the format code lives in the VFS
module, keeping the core/module boundary intact:

- `kernel/fs/procfs.c` — descriptor pool, path matching (`/proc` is a directory,
  `/proc/uptime` a file, `/proc/1` a directory, ...) and `/proc/self` resolution.
  It asks a hook for the bytes of one entry.
- `kernel/fs/vfs.c` — the hook itself (`procfs_write_file`), which formats
  uptime, memory, mounts, network and process entries. Device nodes with
  character/block semantics live here too.
- `kernel/fs/vfs_core.c` — core-side dispatch stubs, including
  `vfs_is_regular()`, so callers can tell a device from a regular file.
- `kernel/core/core_symbols.c` — the kernel symbol table that lets `vfs.mod`
  reach `procfs_*`, `process_at`, timer and memory counters.

### Adding an entry

1. Add it to `procfs_node_type()` / `procfs_list()` in `kernel/fs/procfs.c`.
2. Add a `write_*` formatter in `kernel/fs/vfs.c` and dispatch it from
   `procfs_write_file()`.
3. If it needs a new kernel service, export it from `kernel/core/core_symbols.c`
   as well — a module can only resolve symbols listed there.

### Adding a device

Append it to the `devices[]` table in `kernel/fs/vfs.c`:

```c
static const vfs_device_t devices[] = {
    { "/dev/mydevice", VFS_NODE_CHAR, my_read, my_write },
    ...
};
```

`read` and `write` receive the descriptor offset, so devices can be seekable.
