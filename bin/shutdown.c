#include <stdint.h>
#include "../kernel/core/io.h"

typedef void (*vga_puts_t)(const char*);
typedef void (*vga_putc_t)(char);

static int is_now(const char *param) {
	return param[0] == 'n' && param[1] == 'o' && param[2] == 'w' && param[3] == '\0';
}

static void wait_100ms(void) {
	uint16_t count = 11932;
	uint8_t speaker = inb(0x61);

	outb(0x61, speaker | 1);
	outb(0x43, 0xB0);
	outb(0x42, count & 0xFF);
	outb(0x42, count >> 8);
	while ((inb(0x61) & 0x20) == 0) {
	}
	outb(0x61, speaker);
}

static void power_off(void) {
	outw(0x604, 0x2000);
	outw(0xB004, 0x2000);

	for (;;) {
		asm volatile ("cli; hlt");
	}
}

void cmd_shutdown(const char *param, vga_puts_t vga_puts, vga_putc_t vga_putc) {
	(void)vga_putc;

	if (param[0] != '\0' && !is_now(param)) {
		vga_puts("Usage: shutdown [now]\n");
		return;
	}

	if (is_now(param)) {
		vga_puts("Shutting down now...\n");
		power_off();
	}

	vga_puts("Shutdown scheduled in 60 seconds...\n");
	for (uint32_t tick = 0; tick < 600; tick++) {
		wait_100ms();
	}
	power_off();
}
