#include <stdint.h>
#include "../kernel/fs/ext2.h"
#include "../kernel/core/system.h"

typedef void (*vga_puts_t)(const char *);
typedef void (*vga_putc_t)(char);

extern ext2_filesystem_t fs;
extern int ext2_is_mounted(void);

static void print_u32(uint32_t value, vga_putc_t putc) {
    char digits[11];
    int length = 0;
    if (value == 0) {
        putc('0');
        return;
    }
    while (value > 0) {
        digits[length++] = (char)('0' + value % 10);
        value /= 10;
    }
    while (length > 0) putc(digits[--length]);
}

static void print_megabytes(uint32_t kilobytes, vga_putc_t putc) {
    print_u32(kilobytes / 1024, putc);
    putc('.');
    print_u32((kilobytes % 1024) * 10 / 1024, putc);
    putc(' ');
    putc('M');
    putc('B');
}

static void print_disk_size(uint32_t blocks, uint32_t block_size, vga_putc_t putc) {
    uint32_t megabytes = blocks / (1024 * 1024 / block_size);
    print_u32(megabytes, putc);
    putc(' ');
    putc('M');
    putc('B');
}

void cmd_top(const char *param, vga_puts_t puts, vga_putc_t putc) {
    uint32_t used_blocks;
    (void)param;
    puts("Piux resource monitor\n");
    puts("=====================\n\n");
    puts("RAM\n");
    puts("  Total:     ");
    if (system_memory_total_kb) print_megabytes(system_memory_total_kb, putc);
    else puts("unavailable");
    puts("\n  Kernel:    ");
    if (system_memory_used_kb) print_megabytes(system_memory_used_kb, putc);
    else puts("unavailable");
    puts("\n  Free:      ");
    if (system_memory_total_kb) print_megabytes(system_memory_free_kb, putc);
    else puts("unavailable");
    puts("\n  Note:      free RAM is total memory minus the kernel image.\n\n");
    puts("DISK\n");
    if (!ext2_is_mounted()) {
        puts("  ext2 disk: not mounted\n");
        puts("  Total/free usage unavailable\n");
        return;
    }
    used_blocks = fs.superblock.blocks_count - fs.superblock.free_blocks_count;
    puts("  Total:     ");
    print_disk_size(fs.superblock.blocks_count, fs.block_size, putc);
    puts("\n  Used:      ");
    print_disk_size(used_blocks, fs.block_size, putc);
    puts("\n  Free:      ");
    print_disk_size(fs.superblock.free_blocks_count, fs.block_size, putc);
    puts("\n  Block size: ");
    print_u32(fs.block_size, putc);
    puts(" bytes\n");
}
