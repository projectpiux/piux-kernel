#ifndef PIUX_SHELL_H
#define PIUX_SHELL_H

int shell_execute_command_line(const char *line, void (*puts)(const char *), void (*putc)(char));

#endif
