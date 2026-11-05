typedef void (*vga_puts_t)(const char*);
typedef void (*vga_putc_t)(char);

extern void vga_clear(void);

void cmd_clean(const char *param, vga_puts_t vga_puts, vga_putc_t vga_putc) {
    (void)param;
    (void)vga_puts;
    (void)vga_putc;
    vga_clear();
}

void cmd_clear(const char *param, vga_puts_t vga_puts, vga_putc_t vga_putc) {
    cmd_clean(param, vga_puts, vga_putc);
}
