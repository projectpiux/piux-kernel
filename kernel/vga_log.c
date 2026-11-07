#include <stdint.h>
#include "vga_log.h"
#include "io.h"

extern void vga_puts(const char *text);
extern void vga_putc(char character);

static int serial_ready;

static void serial_putc(char character) {
    if (!serial_ready) {
        outb(0x3f8 + 1, 0);
        outb(0x3f8 + 3, 0x80);
        outb(0x3f8, 1);
        outb(0x3f8 + 1, 0);
        outb(0x3f8 + 3, 0x03);
        outb(0x3f8 + 2, 0xC7);
        outb(0x3f8 + 4, 0x0B);
        serial_ready = 1;
    }
    outb(0x3f8, (uint8_t)character);
}

typedef __builtin_va_list va_list;
#define va_start(list, last) __builtin_va_start(list, last)
#define va_arg(list, type) __builtin_va_arg(list, type)
#define va_end(list) __builtin_va_end(list)

static const char LOG_COLOR_OK = 0;
static const char LOG_COLOR_FAIL = 0;

static void emit_prefix(const char *tag, const char *color) {
    (void)color;
    while (*tag) {
        vga_putc(*tag);
        serial_putc(*tag);
        tag++;
    }
}

static void emit_string(const char *text) {
    vga_puts(text);
}

static void emit_signed(int value) {
    char digits[12];
    int length = 0;
    unsigned int magnitude = value < 0 ? (unsigned int)(-value) : (unsigned int)value;
    if (value < 0) vga_putc('-');
    if (magnitude == 0) {
        vga_putc('0');
        return;
    }
    while (magnitude > 0) {
        digits[length++] = (char)('0' + magnitude % 10);
        magnitude /= 10;
    }
    while (length > 0) vga_putc(digits[--length]);
}

static void emit_hex(uint32_t value) {
    static const char hex[] = "0123456789abcdef";
    vga_puts("0x");
    for (int shift = 28; shift >= 0; shift -= 4) vga_putc(hex[(value >> shift) & 0xf]);
    for (int shift = 28; shift >= 0; shift -= 4) serial_putc(hex[(value >> shift) & 0xf]);
}

static void emit_string_serial(const char *text) {
    if (!text) return;
    while (*text) serial_putc(*text++);
}

static void emit_signed_serial(int value) {
    char digits[12];
    int length = 0;
    unsigned int magnitude = value < 0 ? (unsigned int)(-value) : (unsigned int)value;
    if (value < 0) serial_putc('-');
    if (magnitude == 0) {
        serial_putc('0');
        return;
    }
    while (magnitude > 0) {
        digits[length++] = (char)('0' + magnitude % 10);
        magnitude /= 10;
    }
    while (length > 0) serial_putc(digits[--length]);
}

static void emit_log(const char *tag, const char *format, va_list arguments) {
    emit_prefix(tag, 0);
    for (int index = 0; format[index]; index++) {
        if (format[index] != '%') {
            vga_putc(format[index]);
            serial_putc(format[index]);
            continue;
        }
        index++;
        if (!format[index]) break;
        if (format[index] == 's') {
            const char *text = va_arg(arguments, const char *);
            emit_string(text);
            emit_string_serial(text);
        } else if (format[index] == 'd' || format[index] == 'u') {
            int value = (int)va_arg(arguments, uint32_t);
            emit_signed(value);
            emit_signed_serial(value);
        } else if (format[index] == 'x') {
            emit_hex(va_arg(arguments, uint32_t));
        } else if (format[index] == '%') {
            vga_putc('%');
        } else {
            vga_putc('%');
            vga_putc(format[index]);
        }
    }
    vga_putc('\n');
    serial_putc('\r');
    serial_putc('\n');
}

void vga_log_ok(const char *format, ...) {
    va_list arguments;
    va_start(arguments, format);
    emit_log("[ OK ] ", format, arguments);
    va_end(arguments);
}

void vga_log_fail(const char *format, ...) {
    va_list arguments;
    va_start(arguments, format);
    emit_log("[ FAILED ] ", format, arguments);
    va_end(arguments);
}

void vga_log_warn(const char *format, ...) {
    va_list arguments;
    va_start(arguments, format);
    emit_log("[ WARN ] ", format, arguments);
    va_end(arguments);
}

void vga_log_info(const char *format, ...) {
    va_list arguments;
    va_start(arguments, format);
    emit_log("[    ] ", format, arguments);
    va_end(arguments);
}
