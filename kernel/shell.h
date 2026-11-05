#ifndef PIUX_SHELL_H
#define PIUX_SHELL_H

int shell_execute_command_line(const char *line, void (*puts)(const char *), void (*putc)(char));

void vga_puts(const char *text);
void vga_putc(char character);
void vga_clear(void);

#endif
