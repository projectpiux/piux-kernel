# Piux Init Contract

Since the init model rewrite, `/sbin/init` is a real ELF executable started by
the kernel as PID 1. The configuration moved to `/etc/piux/init.conf`. The
kernel is implementation-agnostic: it only spawns `/sbin/init.elf`, reaps
orphan processes and starts the recovery shell if PID 1 dies. Any executable
that implements the contract below can replace it.

## Boot contract

1. The kernel starts `/sbin/init.elf` (PID 1, user mode) after mount, modules
   and login are done. If the file is missing or not an ELF, the kernel starts
   the recovery shell instead and logs `[ FAILED ] ... starting recovery shell`
   on the VGA console.
2. PID 1 owns service management, reads `/etc/piux/init.conf` and stays alive
   for the whole session.
3. If init exits or crashes, the kernel reports it and falls back to the
   recovery shell. Orphaned processes are re-parented to PID 1 and reaped by it
   through `SYS_WAIT`.

## Configuration: `/etc/piux/init.conf`

The `[STARTUP]` section keeps the legacy format: one component name per line,
`#` comments. The kernel never parses this file — init does.

```ini
# Piux init configuration.

[STARTUP]
pde

[SERVICE:net]
type = oneshot
```

An alternative init may interpret the sections freely, as long as the
`[STARTUP]` section keeps the same meaning.

## Control protocol: PIUX-IPC/1 (versioned)

`initctl` is a user-space client that talks to PID 1 over `SYS_IPC`, a generic
kernel mailbox primitive (no init-specific syscalls exist). An alternative init
that implements this protocol works with the same `initctl`.

Message layout (64 bytes):

| field    | type     | meaning                                       |
|----------|----------|-----------------------------------------------|
| magic    | u32      | `0x50495058` (`PIPX`)                          |
| version  | u8       | `1`                                            |
| op       | u8       | operation code                                 |
| flags    | u8       | reserved                                       |
| reserved | u8       | reserved                                       |
| seq      | u32      | correlation id, echoed by the reply            |
| arg_pid  | u32      | reply destination (client pid in requests)     |
| name[32] | char[32] | service name (NUL-terminated)                  |
| extra[20]| char[20] | status payload in replies                      |

Operations:

| op | name         | request        | reply                              | errors |
|----|--------------|----------------|-------------------------------------|--------|
| 1  | START        | name           | OK + child pid in `arg_pid`         | ENOSVC, EALREADY, EIO |
| 2  | STOP         | name           | OK                                  | ENOSVC |
| 3  | RESTART      | name           | OK + new pid                        | ENOSVC, EIO |
| 4  | RELOAD       | name           | OK                                  | ENOSVC, EINVAL |
| 5  | RELOAD_CONFIG| —              | OK                                  | ECONF |
| 6  | STATUS       | name           | OK; `extra[1]` state, `arg_pid` pid | ENOSVC |
| 7  | PING         | —              | OK                                  | EVERSION |

Replies set bit `0x80` in `op` and keep `seq` unchanged. Status codes in
`extra[0..3]` (little-endian u32):

| code | name     | meaning                          |
|------|----------|----------------------------------|
| 0    | OK       | request completed                |
| 1    | ENOSVC   | unknown service                  |
| 2    | EALREADY | service already running          |
| 3    | EBUSY    | service cannot be stopped now    |
| 4    | EINVAL   | malformed request                |
| 5    | ECONF    | configuration could not be reloaded |
| 6    | EIO      | transport/service failure        |
| 7    | EVERSION | protocol version mismatch        |

Service states (`extra[1]` of STATUS): `0` stopped, `1` starting, `2` running,
`3` exited. If a client gets no reply it reports
`init did not reply` instead of hanging.

## The `initctl` command

```sh
initctl start <service>    # start a service
initctl stop <service>     # stop a service
initctl restart <service>  # restart a service
initctl reload <service>   # reload a service
initctl reload             # reload /etc/piux/init.conf
initctl status <service>   # query one service
```

`initctl` (the shell command) spawns `/usr/bin/initctl` — a user process — and
is only the client of the protocol above; it is not PID 1 and must not be
confused with `/sbin/init.elf`.

## Replacing init

Any ELF that:

1. runs as PID 1 without special kernel privileges,
2. implements the PIUX-IPC/1 protocol on its mailbox,
3. accepts `/etc/piux/init.conf` with the `[STARTUP]` meaning,

can be installed as `/sbin/init.elf` and used with the stock `initctl`.
