#include <stdint.h>
#include "memory.h"

extern char __kernel_end;

#define USER_MEMORY_LIMIT (64U * 1024U * 1024U)
#define PAGE_ENTRIES 1024
#define IDENTITY_PAGE_COUNT (1024U * 1024U)
#define FRAME_COUNT (USER_MEMORY_LIMIT / PAGE_SIZE)
#define FRAME_BITMAP_SIZE (FRAME_COUNT / 8)
#define PAGE_PRESENT 0x001U
#define PAGE_WRITABLE 0x002U
#define PAGE_USER 0x004U
#define PAGE_ADDRESS 0xfffff000U

static uint32_t page_directory[PAGE_ENTRIES] __attribute__((aligned(PAGE_SIZE)));
static uint32_t page_tables[IDENTITY_PAGE_COUNT] __attribute__((aligned(PAGE_SIZE)));
static uint8_t frame_bitmap[FRAME_BITMAP_SIZE];
static uint32_t heap_end;
static uint32_t kernel_directory_address;
static uint32_t frame_limit;
static int paging_active;

static uint32_t align_up(uint32_t value) {
    return (value + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
}

static void enable_paging(uint32_t directory) {
    asm volatile ("mov %0, %%cr3\n\t"
                  "mov %%cr0, %%eax\n\t"
                  "or $0x80010000, %%eax\n\t"
                  "mov %%eax, %%cr0"
                  : : "r"(directory) : "eax", "memory");
}

static void set_frame_used(uint32_t frame, int used) {
    uint8_t mask = (uint8_t)(1U << (frame & 7U));
    if (used) frame_bitmap[frame >> 3] |= mask;
    else frame_bitmap[frame >> 3] &= (uint8_t)~mask;
}

static int frame_is_used(uint32_t frame) {
    return (frame_bitmap[frame >> 3] & (uint8_t)(1U << (frame & 7U))) != 0;
}

static uint32_t allocate_frame(void) {
    for (uint32_t frame = frame_limit; frame > 0; frame--) {
        uint32_t index = frame - 1;
        uint32_t address = index * PAGE_SIZE;
        if (address < heap_end || frame_is_used(index)) continue;
        set_frame_used(index, 1);
        return address;
    }
    return 0;
}

static void release_frame(uint32_t address) {
    uint32_t frame = address / PAGE_SIZE;
    if (frame < frame_limit && address >= heap_end) set_frame_used(frame, 0);
}

static uint32_t *active_directory(void) {
    uint32_t address;
    asm volatile ("mov %%cr3, %0" : "=r"(address));
    return (uint32_t *)(uintptr_t)address;
}

static uint32_t *table_for(uint32_t *directory, uint32_t address) {
    uint32_t entry = directory[address >> 22];
    if (!(entry & PAGE_PRESENT)) return 0;
    return (uint32_t *)(uintptr_t)(entry & PAGE_ADDRESS);
}

void memory_init(uint32_t upper_memory_kb) {
    uint32_t page_count = IDENTITY_PAGE_COUNT;
    uint32_t reserved_frames;
    uint32_t max_upper_kb = (USER_MEMORY_LIMIT / 1024U) - 1024U;
    if (upper_memory_kb > max_upper_kb) upper_memory_kb = max_upper_kb;
    frame_limit = (1024U + upper_memory_kb) / (PAGE_SIZE / 1024U);
    if (frame_limit > FRAME_COUNT) frame_limit = FRAME_COUNT;
    for (uint32_t index = 0; index < FRAME_BITMAP_SIZE; index++) frame_bitmap[index] = 0;
    for (uint32_t index = 0; index < PAGE_ENTRIES; index++) page_directory[index] = 0;
    for (uint32_t page = 0; page < page_count; page++) {
        page_tables[page] = page * PAGE_SIZE | PAGE_PRESENT | PAGE_WRITABLE;
    }
    for (uint32_t table = 0; table < page_count / PAGE_ENTRIES; table++) {
        page_directory[table] = (uint32_t)&page_tables[table * PAGE_ENTRIES] |
                                PAGE_PRESENT | PAGE_WRITABLE;
    }
    heap_end = align_up((uint32_t)(uintptr_t)&__kernel_end);
    reserved_frames = heap_end / PAGE_SIZE;
    for (uint32_t frame = 0; frame < reserved_frames && frame < FRAME_COUNT; frame++) {
        set_frame_used(frame, 1);
    }
    kernel_directory_address = (uint32_t)(uintptr_t)page_directory;
    enable_paging((uint32_t)(uintptr_t)page_directory);
    paging_active = 1;
}

void *kmalloc(uint32_t size) {
    uint32_t address, end;
    if (size == 0 || size > USER_MEMORY_LIMIT) return 0;
    address = align_up(heap_end);
    size = align_up(size);
    end = address + size;
    if (address < heap_end || end < address || end > frame_limit * PAGE_SIZE) return 0;
    for (uint32_t page = address / PAGE_SIZE; page < end / PAGE_SIZE; page++) {
        if (frame_is_used(page)) return 0;
    }
    for (uint32_t page = address / PAGE_SIZE; page < end / PAGE_SIZE; page++) set_frame_used(page, 1);
    heap_end = end;
    return (void *)(uintptr_t)address;
}

void kfree(void *address) { (void)address; }

int memory_user_range_valid(const void *address, uint32_t size, int writable) {
    uint32_t start = (uint32_t)(uintptr_t)address;
    uint32_t end;
    if (start + size < start) return 0;
    end = start + size;
    uint32_t *directory = active_directory();
    if (start < USER_SPACE_BASE || end > USER_SPACE_TOP) return 0;
    if (size == 0) return 1;
    for (uint32_t page = start & ~(PAGE_SIZE - 1); page < end; page += PAGE_SIZE) {
        uint32_t directory_entry = directory[page >> 22];
        uint32_t *table = table_for(directory, page);
        uint32_t entry;
        if (!(directory_entry & PAGE_USER) || (writable && !(directory_entry & PAGE_WRITABLE)) || !table) return 0;
        entry = table[(page >> 12) & 0x3ff];
        if (!(entry & PAGE_PRESENT) || !(entry & PAGE_USER) ||
            (writable && !(entry & PAGE_WRITABLE))) return 0;
    }
    return 1;
}

int memory_range_valid(const void *address, uint32_t size, int user_space) {
    uint32_t start = (uint32_t)(uintptr_t)address;
    uint32_t end;
    if (user_space) return memory_user_range_valid(address, size, 0);
    if (start + size < start) return 0;
    end = start + size;
    return start >= 0x00100000U && end <= 0xfffff000U;
}

int memory_address_space_create(memory_address_space_t *address_space) {
    uint32_t frame;
    if (!address_space || !kernel_directory_address) return -1;
    frame = allocate_frame();
    if (!frame) return -1;
    address_space->directory = (uint32_t *)(uintptr_t)frame;
    for (uint32_t index = 0; index < PAGE_ENTRIES; index++) {
        address_space->directory[index] = page_directory[index];
    }
    for (uint32_t address = USER_SPACE_BASE; address < USER_SPACE_TOP; address += 0x400000U) {
        address_space->directory[address >> 22] = 0;
    }
    return 0;
}

void memory_address_space_activate(const memory_address_space_t *address_space) {
    uint32_t directory = address_space && address_space->directory ?
                         (uint32_t)(uintptr_t)address_space->directory : kernel_directory_address;
    enable_paging(directory);
}

void memory_address_space_destroy(memory_address_space_t *address_space) {
    if (!address_space || !address_space->directory ||
        (uint32_t)(uintptr_t)address_space->directory == kernel_directory_address) return;
    for (uint32_t address = USER_SPACE_BASE; address < USER_SPACE_TOP; address += 0x400000U) {
        uint32_t directory_index = address >> 22;
        uint32_t entry = address_space->directory[directory_index];
        uint32_t *table;
        if (!(entry & PAGE_PRESENT)) continue;
        table = (uint32_t *)(uintptr_t)(entry & PAGE_ADDRESS);
        for (uint32_t index = 0; index < PAGE_ENTRIES; index++) {
            if ((table[index] & (PAGE_PRESENT | PAGE_USER)) == (PAGE_PRESENT | PAGE_USER)) {
                release_frame(table[index] & PAGE_ADDRESS);
            }
        }
        release_frame(entry & PAGE_ADDRESS);
        address_space->directory[directory_index] = 0;
    }
    release_frame((uint32_t)(uintptr_t)address_space->directory);
    address_space->directory = 0;
}

int memory_user_map(uint32_t address, uint32_t size, int writable) {
    uint32_t start, end;
    uint32_t *directory = active_directory();
    if (!size || address + size < address) return -1;
    end = address + size;
    if (address < USER_SPACE_BASE || end > USER_SPACE_TOP) return -1;
    start = address & ~(PAGE_SIZE - 1);
    end = align_up(end);
    for (uint32_t page = start; page < end; page += PAGE_SIZE) {
        uint32_t directory_index = page >> 22;
        uint32_t table_index = (page >> 12) & 0x3ff;
        uint32_t *table;
        uint32_t frame;
        if (!(directory[directory_index] & PAGE_PRESENT)) {
            frame = allocate_frame();
            if (!frame) return -1;
            table = (uint32_t *)(uintptr_t)frame;
            for (uint32_t index = 0; index < PAGE_ENTRIES; index++) table[index] = 0;
            directory[directory_index] = frame | PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER;
        } else {
            if (!(directory[directory_index] & PAGE_USER)) return -1;
            table = table_for(directory, page);
        }
        if (!(table[table_index] & PAGE_PRESENT)) {
            frame = allocate_frame();
            if (!frame) return -1;
            uint8_t *bytes = (uint8_t *)(uintptr_t)frame;
            for (uint32_t index = 0; index < PAGE_SIZE; index++) bytes[index] = 0;
            table[table_index] = frame | PAGE_PRESENT | PAGE_USER |
                                 (writable ? PAGE_WRITABLE : 0);
        } else if (writable) {
            table[table_index] |= PAGE_WRITABLE;
        }
        asm volatile ("invlpg (%0)" : : "r"(page) : "memory");
    }
    return 0;
}

int memory_user_protect(uint32_t address, uint32_t size, int writable) {
    uint32_t end;
    uint32_t *directory = active_directory();
    if (!size || address + size < address) return -1;
    end = address + size;
    if (address < USER_SPACE_BASE || end > USER_SPACE_TOP) return -1;
    for (uint32_t page = address & ~(PAGE_SIZE - 1); page < end; page += PAGE_SIZE) {
        if (!(directory[page >> 22] & PAGE_USER)) return -1;
        uint32_t *table = table_for(directory, page);
        uint32_t *entry;
        if (!table) return -1;
        entry = &table[(page >> 12) & 0x3ff];
        if ((*entry & (PAGE_PRESENT | PAGE_USER)) != (PAGE_PRESENT | PAGE_USER)) return -1;
        if (writable) *entry |= PAGE_WRITABLE;
        else *entry &= ~PAGE_WRITABLE;
        asm volatile ("invlpg (%0)" : : "r"(page) : "memory");
    }
    return 0;
}

int paging_enabled(void) { return paging_active; }