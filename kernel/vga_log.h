#ifndef VGA_LOG_H
#define VGA_LOG_H

void vga_log_ok(const char *format, ...);
void vga_log_fail(const char *format, ...);
void vga_log_warn(const char *format, ...);
void vga_log_info(const char *format, ...);

#endif
