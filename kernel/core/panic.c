#include "panic.h"
#include "process.h"
#include "vga_log.h"
#include "../video/video.h"

extern void vga_putc(char character);

/*
 * Full-screen kernel panic report.
 *
 * Everything below is safe to run from a fault handler: no allocation, no
 * locks, no printf - only static buffers and the raw video layer. The screen
 * is painted straight into the framebuffer so user-space windows cannot cover
 * it, and the scheduler is switched off before drawing so no user process can
 * paint over it afterwards.
 */

#define PANIC_BG 0x2B0B0Bu
#define PANIC_CARD 0x3D1212u
#define PANIC_BORDER 0xE04444u
#define PANIC_ACCENT 0xFF7A7Au
#define PANIC_TEXT 0xF4EDEDu
#define PANIC_MUTED 0xD8B6B6u
#define PANIC_TITLE 0xFFFFFFu

#define PANIC_MARGIN 24
#define PANIC_TEXT_WIDTH 8
#define PANIC_LINE_MAX 160

static const char *const panic_exception_names[32] = {
    "Divide Error",
    "Debug Exception",
    "Non-Maskable Interrupt",
    "Breakpoint",
    "Overflow",
    "BOUND Range Exceeded",
    "Invalid Opcode",
    "Device Not Available",
    "Double Fault",
    "Coprocessor Segment Overrun",
    "Invalid TSS",
    "Segment Not Present",
    "Stack-Segment Fault",
    "General Protection Fault",
    "Page Fault",
    "Reserved",
    "x87 Floating-Point Error",
    "Alignment Check",
    "Machine Check",
    "SIMD Floating-Point Exception",
    "Virtualization Exception",
    "Control Protection Exception",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Hypervisor Injection Exception",
    "VMM Communication Exception",
    "Security Exception",
    "Reserved"
};

static const char *const panic_exception_hints[32] = {
    "a division by zero or a divider overflow reached the kernel",
    "a debug trap fired in kernel mode",
    "the NMI reported an unrecoverable hardware condition",
    "an int3 breakpoint was hit in kernel mode",
    "an INTO overflow trap reached the kernel",
    "an array bound check failed in kernel mode",
    "the CPU executed an unknown or unsupported instruction",
    "the FPU context was used while unavailable",
    "a second fault happened while handling another exception",
    "a segment descriptor was loaded outside its limits",
    "the task state segment or its descriptor is invalid",
    "the CPU loaded a segment whose descriptor is not present",
    "the stack pointer or stack segment is invalid",
    "the kernel violated a protection rule",
    "the kernel touched an unmapped or protected address",
    "the reserved exception vector was raised",
    "an x87 floating-point instruction failed",
    "an unaligned memory access was detected",
    "the CPU reported a fatal machine check",
    "an SSE/AVX instruction raised an unmasked error",
    "the CPU reported a virtualization error",
    "a control flow protection check failed",
    "the reserved exception vector was raised",
    "the reserved exception vector was raised",
    "the reserved exception vector was raised",
    "the reserved exception vector was raised",
    "the reserved exception vector was raised",
    "the reserved exception vector was raised",
    "the hypervisor raised an injection exception",
    "the hypervisor reported a communication failure",
    "the CPU reported a security violation",
    "the reserved exception vector was raised"
};

static char panic_line[PANIC_LINE_MAX];

static const char *panic_exception_name(uint32_t vector) {
    return vector < 32 ? panic_exception_names[vector] : "Unknown Exception";
}

static const char *panic_exception_hint(uint32_t vector) {
    return vector < 32 ? panic_exception_hints[vector] : "an unknown fault was reported";
}

/* --- tiny formatting helpers ------------------------------------------- */

static void panic_format_hex(char *out, uint32_t value) {
    static const char digits[] = "0123456789ABCDEF";
    *out++ = '0';
    *out++ = 'x';
    for (int shift = 28; shift >= 0; shift -= 4) *out++ = digits[(value >> shift) & 0x0Fu];
    *out = '\0';
}

static void panic_format_dec(char *out, uint32_t value) {
    char digits[12];
    int length = 0;
    if (value == 0) {
        *out++ = '0';
        *out = '\0';
        return;
    }
    while (value > 0 && length < (int)sizeof(digits)) {
        digits[length++] = (char)('0' + value % 10u);
        value /= 10u;
    }
    while (length > 0) *out++ = digits[--length];
    *out = '\0';
}

/* --- drawing ------------------------------------------------------------ */

#define PANIC_TEXT_LEFT 24
#define PANIC_LABEL_GAP 10

typedef struct {
    int scale;
    int step;
    int card_x;
    int card_width;
    int text_x;
    int right;
    int y;
    int bottom;
    int columns;
} panic_layout_t;

static panic_layout_t layout;

static void panic_rect(int x, int y, int width, int height, uint32_t color) {
    video_fill_rect(x, y, width, height, color, 255);
}

static void panic_text(int x, int y, const char *text, uint32_t color, int scale) {
    while (*text) {
        video_put_scaled_cell(x, y, scale, *text, color);
        x += PANIC_TEXT_WIDTH * scale;
        text++;
    }
}

static void panic_text_at(const char *text, uint32_t color, int scale) {
    panic_text(layout.text_x, layout.y, text, color, scale);
}

static void panic_newline(void) {
    layout.y += layout.step;
}

/* Draws text word by word, wrapping at the card edge and stopping before the
   fixed footer. */
static void panic_paragraph(const char *text, uint32_t color) {
    char line[PANIC_LINE_MAX];
    int column = 0;
    int limit = layout.columns;
    line[0] = '\0';
    if (layout.y > layout.bottom - 2 * layout.step) return;
    while (*text) {
        const char *word = text;
        int word_length = 0;
        while (word[word_length] && word[word_length] != ' ') word_length++;
        if (column > 0 && column + 1 + word_length > limit) {
            panic_text_at(line, color, layout.scale);
            panic_newline();
            if (layout.y > layout.bottom - 2 * layout.step) return;
            column = 0;
            line[0] = '\0';
        }
        if (column > 0 && column < PANIC_LINE_MAX - 1) line[column++] = ' ';
        for (int index = 0; index < word_length && column < PANIC_LINE_MAX - 1; index++)
            line[column++] = word[index];
        line[column] = '\0';
        text = word + word_length;
        while (*text == ' ') text++;
    }
    panic_text_at(line, color, layout.scale);
    panic_newline();
}

static void panic_divider(void) {
    layout.y += layout.step / 3;
    panic_rect(layout.card_x + PANIC_TEXT_LEFT, layout.y,
               layout.right - layout.card_x - 2 * PANIC_TEXT_LEFT, 2, PANIC_BORDER);
    layout.y += layout.step / 3;
}

static void panic_section(const char *title) {
    panic_text_at(title, PANIC_ACCENT, layout.scale);
    panic_newline();
}

static void panic_append(char *out, uint32_t *length, const char *text) {
    while (*text && *length < PANIC_LINE_MAX - 1) out[(*length)++] = *text++;
    out[*length] = '\0';
}

static void panic_serial_puts(const char *text) {
    while (*text) vga_putc(*text++);
}

static void panic_serial_line(const char *label, const char *value) {
    panic_serial_puts(label);
    panic_serial_puts(value);
    panic_serial_puts("\n");
}


void kernel_panic(const panic_frame_t *frame, const char *message) {
    panic_frame_t report;
    const char *name;
    const char *hint;
    const process_t *faulted;
    uint32_t width;
    uint32_t height;
    int label_x;
    int scale;
    int step;
    int card_x, card_y, card_width;
    char hex[16];
    char dec[16];
    uint32_t length;

    asm volatile ("cli" ::: "memory");

    if (frame) {
        report = *frame;
    } else {
        uint32_t *slot = (uint32_t *)&report;
        for (uint32_t index = 0; index < sizeof(report) / sizeof(uint32_t); index++) slot[index] = 0;
    }

    name = panic_exception_name(report.vector);
    hint = panic_exception_hint(report.vector);
    faulted = process_current();

    /* 1. Interrupt every user-space service: no scheduling, no ring 3. */
    process_disable_scheduler();

    /* 2. Mirror the report on the serial port for logs and debugging. */
    panic_serial_puts("\n*** KERNEL PANIC ***\n");
    panic_serial_line("reason: ", message ? message : "unknown");
    panic_serial_line("exception: ", name);
    panic_serial_line("hint: ", hint);
    panic_format_hex(hex, report.vector);
    panic_serial_line("vector: ", hex);
    panic_format_hex(hex, report.error_code);
    panic_serial_line("error code: ", hex);
    panic_format_hex(hex, report.eip);
    panic_serial_line("eip: ", hex);
    panic_format_hex(hex, report.cs);
    panic_serial_line("cs: ", hex);
    panic_format_hex(hex, report.eflags);
    panic_serial_line("eflags: ", hex);
    panic_format_hex(hex, report.cr2);
    panic_serial_line("cr2: ", hex);
    panic_format_hex(hex, report.cr3);
    panic_serial_line("cr3: ", hex);
    if (faulted) {
        panic_serial_line("process: ", faulted->name);
        panic_format_dec(dec, (uint32_t)faulted->pid);
        panic_serial_line("pid: ", dec);
    }

    /* 3. Paint the full-screen report over every user-space surface. */
    width = video_pixel_width > 0 ? (uint32_t)video_pixel_width : 640u;
    height = video_pixel_height > 0 ? (uint32_t)video_pixel_height : 480u;
    video_clear();
    if (width < 320) width = 320;
    if (height < 240) height = 240;

    scale = (int)width >= 720 ? 2 : 1;
    step = 10 * scale + 2;
    if (height < 520) step -= 2;
    panic_rect(0, 0, (int)width, (int)height, PANIC_BG);

    card_x = PANIC_MARGIN;
    card_y = PANIC_MARGIN;
    card_width = (int)width - 2 * PANIC_MARGIN;
    if (card_width < 280) {
        card_x = 8;
        card_y = 8;
        card_width = (int)width - 16;
    }
    panic_rect(card_x, card_y, card_width, (int)height - 2 * card_y, PANIC_CARD);
    panic_rect(card_x, card_y, card_width, 3, PANIC_BORDER);
    panic_rect(card_x, card_y, 4, (int)height - 2 * card_y, PANIC_BORDER);

    layout.scale = scale;
    layout.step = step;
    layout.card_x = card_x;
    layout.card_width = card_width;
    layout.text_x = card_x + PANIC_TEXT_LEFT;
    layout.right = card_x + card_width;
    layout.y = card_y + 16;
    layout.bottom = (int)height - card_y - 12;
    layout.columns = (card_width - 2 * PANIC_TEXT_LEFT) / (PANIC_TEXT_WIDTH * scale);
    if (layout.columns > PANIC_LINE_MAX - 1) layout.columns = PANIC_LINE_MAX - 1;
    label_x = layout.text_x;

    panic_text(label_x, layout.y, "KERNEL PANIC", PANIC_TITLE, scale >= 2 ? 3 : 2);
    layout.y += (scale >= 2 ? 30 : 22) + step;
    panic_paragraph("The system crashed inside the kernel.", PANIC_TEXT);
    panic_paragraph("It is recommended to restart the device.", PANIC_TEXT);

    panic_divider();
    panic_section("CAUSE");
    length = 0;
    panic_append(panic_line, &length, name);
    panic_append(panic_line, &length, " during kernel execution");
    panic_paragraph(panic_line, PANIC_TEXT);
    panic_paragraph(message && *message ? message : hint, PANIC_MUTED);

    panic_divider();
    panic_section("EXCEPTION");
    length = 0;
    panic_append(panic_line, &length, name);
    panic_append(panic_line, &length, " (vector ");
    panic_format_dec(dec, report.vector);
    panic_append(panic_line, &length, dec);
    panic_append(panic_line, &length, ")");
    panic_text_at(panic_line, PANIC_TEXT, scale);
    panic_newline();
    length = 0;
    panic_append(panic_line, &length, "error code ");
    panic_format_hex(hex, report.error_code);
    panic_append(panic_line, &length, hex);
    panic_text_at(panic_line, PANIC_MUTED, scale);
    panic_newline();
    length = 0;
    panic_append(panic_line, &length, "eip ");
    panic_format_hex(hex, report.eip);
    panic_append(panic_line, &length, hex);
    panic_append(panic_line, &length, "  cs ");
    panic_format_hex(hex, report.cs);
    panic_append(panic_line, &length, hex);
    panic_text_at(panic_line, PANIC_MUTED, scale);
    panic_newline();
    length = 0;
    panic_append(panic_line, &length, "eflags ");
    panic_format_hex(hex, report.eflags);
    panic_append(panic_line, &length, hex);
    panic_text_at(panic_line, PANIC_MUTED, scale);
    panic_newline();

    panic_divider();
    panic_section("MEMORY");
    length = 0;
    panic_append(panic_line, &length, "cr2 ");
    panic_format_hex(hex, report.cr2);
    panic_append(panic_line, &length, hex);
    panic_append(panic_line, &length, "  cr3 ");
    panic_format_hex(hex, report.cr3);
    panic_append(panic_line, &length, hex);
    panic_text_at(panic_line, PANIC_TEXT, scale);
    panic_newline();

    if (faulted) {
        panic_divider();
        panic_section("FAULTING PROCESS");
        length = 0;
        panic_append(panic_line, &length, faulted->name);
        panic_append(panic_line, &length, " (pid ");
        panic_format_dec(dec, (uint32_t)faulted->pid);
        panic_append(panic_line, &length, dec);
        panic_append(panic_line, &length, ")");
        panic_text_at(panic_line, PANIC_TEXT, scale);
        panic_newline();
    }

    layout.y += layout.step;
    if (layout.y > layout.bottom - layout.step) layout.y = layout.bottom - layout.step;
    panic_rect(layout.card_x + PANIC_TEXT_LEFT, layout.y,
               layout.right - layout.card_x - 2 * PANIC_TEXT_LEFT, 2, PANIC_BORDER);
    layout.y += layout.step / 2 + 2;
    panic_text_at("All user services stopped. System halted.", PANIC_MUTED, scale);

    for (;;) asm volatile ("cli; hlt");
}

void kernel_panic_msg(const char *message) {
    kernel_panic(0, message);
}
