

static inline int piux_syscall(int number, int a, int b, int c) {
    int result;
    asm volatile ("int $0x80"
                  : "=a"(result)
                  : "a"(number), "b"(a), "c"(b), "d"(c)
                  : "memory");
    return result;
}

void _start(void) {
    const char *message = "Hello from user mode!\n";
    int length = 0;
    while (message[length]) length++;
    piux_syscall(1 , 1 , (int)(long)message, length);
    piux_syscall(8 , 42, 0, 0);
    for (;;) {
    }
}
