#include <stdint.h>
#include "../kernel/ext2.h"

typedef void (*vga_puts_t)(const char*);
typedef void (*vga_putc_t)(char);

extern int ext2_mount(void);

extern ext2_filesystem_t fs;

void cmd_fs(const char *param, vga_puts_t vga_puts, vga_putc_t vga_putc) {
    vga_puts("Checking ext2 filesystem...\n");

    int result = ext2_mount();

    if (result == 0) {
        vga_puts("ext2 mounted successfully!\n");
        vga_puts("Block size: ");

        char buf[16];
        int pos = 0;
        uint32_t temp = fs.block_size;
        while (temp > 0) {
            buf[pos++] = '0' + (temp % 10);
            temp /= 10;
        }
        for (int i = pos - 1; i >= 0; i--) {
            vga_putc(buf[i]);
        }
        vga_putc('\n');
    } else {
        vga_puts("ext2 mount failed (no disk or bad magic)\n");
    }
}
