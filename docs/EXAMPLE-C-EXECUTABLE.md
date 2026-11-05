# Example of an executable on Piux: ELF Hello World

A minimal example of a freestanding `.elf` executable for **Piux**.

The program prints `Hello, world!` to standard output and then exits cleanly. It uses no C standard library: it talks to the system directly through raw syscalls.

## How it works

The program defines its own entry point, `_start`, instead of `main`, and performs two syscalls through the `int $0x80` software interrupt:

| Syscall     | Number | Arguments                                 |
|-------------|--------|-------------------------------------------|
| `SYS_WRITE` | `1`    | `fd`, pointer to buffer, buffer length    |
| `SYS_EXIT`  | `8`    | exit code                                 |

Arguments are passed in the `ebx`, `ecx` and `edx` registers, the syscall number goes in `eax`, and the return value comes back in `eax`.

## Source code

```c
#define SYS_WRITE 1
#define SYS_EXIT 8
#define STDOUT 1

static inline int piux_syscall(int number, int arg1, int arg2, int arg3) {
    int result;
    asm volatile ("int $0x80"
                  : "=a"(result)
                  : "a"(number), "b"(arg1), "c"(arg2), "d"(arg3)
                  : "memory");
    return result;
}

void _start(void) {
    const char message[] = "Hello, world!\n";
    int length = sizeof(message) - 1;

    piux_syscall(SYS_WRITE, STDOUT, (int)(long)message, length);
    piux_syscall(SYS_EXIT, 0, 0, 0);

    for (;;) {}
}
```

## Building on Linux

```sh
gcc -Wall -Wextra -nostdlib -nodefaultlibs -fPIC -static -o main.elf main.c
```

### Flags explained

- `-nostdlib -nodefaultlibs`: do not link the C standard library or default libraries, since the program provides its own `_start`.
- `-static`: produce a statically linked executable with no dynamic loader.
- `-fPIC`: generate position-independent code.
- `-Wall -Wextra`: enable extra compiler warnings.

> **Note:** the syscall convention above uses 32-bit registers (`int $0x80`). If your toolchain defaults to 64-bit, you may need to add `-m32` to the command (this requires 32-bit development libraries, e.g. `gcc-multilib`).

## Running

Copy `main.elf` to your Piux environment and execute it. Expected output:

```
Hello, world!
```