#include <stdint.h>
#include "elf.h"
#include "vfs.h"
#include "memory.h"

#define ELF_MAX_FILE_SIZE (256U * 1024U)
#define ELF_PT_LOAD 1
#define ELF_CLASS_32 1
#define ELF_TYPE_EXEC 2
#define ELF_MACHINE_386 3
#define ELF_PT_WRITE 2
#define ELF_PT_EXEC 1

typedef struct {
    uint8_t e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint32_t e_entry;
    uint32_t e_phoff;
    uint32_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} __attribute__((packed)) elf32_header_t;

typedef struct {
    uint32_t p_type;
    uint32_t p_offset;
    uint32_t p_vaddr;
    uint32_t p_paddr;
    uint32_t p_filesz;
    uint32_t p_memsz;
    uint32_t p_flags;
    uint32_t p_align;
} __attribute__((packed)) elf32_program_header_t;

static uint8_t file_buffer[ELF_MAX_FILE_SIZE];

static int read_whole_file(const char *path) {
    int fd = vfs_open(path, VFS_O_READ);
    int total = 0;
    if (fd < 0) return -1;
    for (;;) {
        int chunk = vfs_read(fd, file_buffer + total, sizeof(file_buffer) - (uint32_t)total);
        if (chunk <= 0) break;
        total += chunk;
        if ((uint32_t)total >= sizeof(file_buffer)) break;
    }
    vfs_close(fd);
    return total;
}

static int elf_load_segments(memory_address_space_t *address_space, uint32_t total,
                             uint32_t *entry_point) {
    elf32_header_t *header = (elf32_header_t *)file_buffer;
    int entry_is_executable = 0;
    for (uint16_t index = 0; index < header->e_phnum; index++) {
        elf32_program_header_t *program = (elf32_program_header_t *)
            (file_buffer + header->e_phoff + index * header->e_phentsize);
        if (program->p_type != ELF_PT_LOAD) continue;
        if (!program->p_memsz) {
            if (program->p_filesz) return -1;
            continue;
        }
        if (program->p_filesz > program->p_memsz ||
            program->p_vaddr + program->p_memsz < program->p_vaddr ||
            program->p_offset + program->p_filesz < program->p_offset ||
            program->p_offset + program->p_filesz > total ||
            memory_user_map_into(address_space, program->p_vaddr, program->p_memsz, 1) < 0) return -1;
        if (memory_user_copy_into(address_space, program->p_vaddr,
                                  file_buffer + program->p_offset, program->p_filesz) < 0) return -1;
        {
            static uint8_t zeros[PAGE_SIZE];
            uint32_t remaining = program->p_memsz - program->p_filesz;
            uint32_t done = 0;
            for (uint32_t index2 = 0; index2 < PAGE_SIZE; index2++) zeros[index2] = 0;
            while (remaining > 0) {
                uint32_t chunk = remaining > PAGE_SIZE ? PAGE_SIZE : remaining;
                if (memory_user_copy_into(address_space, program->p_vaddr + program->p_filesz + done,
                                          zeros, chunk) < 0) return -1;
                done += chunk;
                remaining -= chunk;
            }
        }
        if ((program->p_flags & ELF_PT_EXEC) &&
            header->e_entry >= program->p_vaddr &&
            header->e_entry - program->p_vaddr < program->p_memsz) entry_is_executable = 1;
    }
    if (!entry_is_executable) return -1;
    for (uint16_t index = 0; index < header->e_phnum; index++) {
        elf32_program_header_t *program = (elf32_program_header_t *)
            (file_buffer + header->e_phoff + index * header->e_phentsize);
        if (program->p_type != ELF_PT_LOAD || !program->p_memsz ||
            (program->p_flags & ELF_PT_WRITE)) continue;
        uint32_t first_page = program->p_vaddr & ~(PAGE_SIZE - 1);
        uint32_t last_page = (program->p_vaddr + program->p_memsz - 1) & ~(PAGE_SIZE - 1);
        for (uint32_t page = first_page; page <= last_page; page += PAGE_SIZE) {
            int writable_overlap = 0;
            for (uint16_t other_index = 0; other_index < header->e_phnum; other_index++) {
                elf32_program_header_t *other = (elf32_program_header_t *)
                    (file_buffer + header->e_phoff + other_index * header->e_phentsize);
                uint32_t page_end = page + PAGE_SIZE;
                uint32_t other_end = other->p_vaddr + other->p_memsz;
                if (other->p_type == ELF_PT_LOAD && (other->p_flags & ELF_PT_WRITE) &&
                    other->p_vaddr < page_end && other_end > page) writable_overlap = 1;
            }
            if (!writable_overlap &&
                memory_user_protect_into(address_space, page, PAGE_SIZE, 0) < 0) return -1;
            if (page == last_page) break;
        }
    }
    *entry_point = header->e_entry;
    return 0;
}

int elf_load_into(const memory_address_space_t *address_space, const char *path,
                  uint32_t *entry_point) {
    int total;
    elf32_header_t *header;
    if (!address_space || !address_space->directory) return -1;
    total = read_whole_file(path);
    if (total < (int)sizeof(elf32_header_t)) return -1;
    header = (elf32_header_t *)file_buffer;
    if (header->e_ident[0] != 0x7f || header->e_ident[1] != 'E' ||
        header->e_ident[2] != 'L' || header->e_ident[3] != 'F') return -1;
    if (header->e_ident[4] != ELF_CLASS_32 || header->e_type != ELF_TYPE_EXEC ||
        header->e_machine != ELF_MACHINE_386 || header->e_phentsize < sizeof(elf32_program_header_t)) return -1;
    if (header->e_phoff > (uint32_t)total ||
        (uint32_t)header->e_phnum > ((uint32_t)total - header->e_phoff) / header->e_phentsize) return -1;
    return elf_load_segments((memory_address_space_t *)address_space, (uint32_t)total, entry_point);
}

int elf_load(const char *path, uint32_t *entry_point) {
    int total = read_whole_file(path);
    elf32_header_t *header;
    int entry_is_executable = 0;
    if (total < (int)sizeof(elf32_header_t)) return -1;

    header = (elf32_header_t *)file_buffer;
    if (header->e_ident[0] != 0x7f || header->e_ident[1] != 'E' ||
        header->e_ident[2] != 'L' || header->e_ident[3] != 'F') return -1;
    if (header->e_ident[4] != ELF_CLASS_32 || header->e_type != ELF_TYPE_EXEC ||
        header->e_machine != ELF_MACHINE_386 || header->e_phentsize < sizeof(elf32_program_header_t)) return -1;
    if (header->e_phoff > (uint32_t)total ||
        (uint32_t)header->e_phnum > ((uint32_t)total - header->e_phoff) / header->e_phentsize) return -1;

    for (uint16_t index = 0; index < header->e_phnum; index++) {
        elf32_program_header_t *program = (elf32_program_header_t *)
            (file_buffer + header->e_phoff + index * header->e_phentsize);
        uint8_t *destination;
        uint8_t *source;
        if (program->p_type != ELF_PT_LOAD) continue;
        if (!program->p_memsz) {
            if (program->p_filesz) return -1;
            continue;
        }
        if (program->p_filesz > program->p_memsz ||
            program->p_vaddr + program->p_memsz < program->p_vaddr ||
            program->p_offset + program->p_filesz < program->p_offset ||
            program->p_offset + program->p_filesz > (uint32_t)total ||
            (program->p_vaddr < USER_SPACE_TOP &&
             program->p_vaddr + program->p_memsz > USER_SPACE_TOP - USER_STACK_SIZE) ||
            memory_user_map(program->p_vaddr, program->p_memsz, 1) < 0 ||
            !memory_range_valid((void *)(uintptr_t)program->p_vaddr, program->p_memsz, 1)) return -1;
        destination = (uint8_t *)(uintptr_t)program->p_vaddr;
        source = file_buffer + program->p_offset;
        for (uint32_t byte = 0; byte < program->p_filesz; byte++) destination[byte] = source[byte];
        for (uint32_t byte = program->p_filesz; byte < program->p_memsz; byte++) destination[byte] = 0;
        if ((program->p_flags & ELF_PT_EXEC) &&
            header->e_entry >= program->p_vaddr &&
            header->e_entry - program->p_vaddr < program->p_memsz) entry_is_executable = 1;
    }

    if (!entry_is_executable) return -1;
    for (uint16_t index = 0; index < header->e_phnum; index++) {
        elf32_program_header_t *program = (elf32_program_header_t *)
            (file_buffer + header->e_phoff + index * header->e_phentsize);
        if (program->p_type != ELF_PT_LOAD || !program->p_memsz ||
            (program->p_flags & ELF_PT_WRITE)) continue;
        uint32_t first_page = program->p_vaddr & ~(PAGE_SIZE - 1);
        uint32_t last_page = (program->p_vaddr + program->p_memsz - 1) & ~(PAGE_SIZE - 1);
        for (uint32_t page = first_page; page <= last_page; page += PAGE_SIZE) {
            int writable_overlap = 0;
            for (uint16_t other_index = 0; other_index < header->e_phnum; other_index++) {
                elf32_program_header_t *other = (elf32_program_header_t *)
                    (file_buffer + header->e_phoff + other_index * header->e_phentsize);
                uint32_t page_end = page + PAGE_SIZE;
                uint32_t other_end = other->p_vaddr + other->p_memsz;
                if (other->p_type == ELF_PT_LOAD && (other->p_flags & ELF_PT_WRITE) &&
                    other->p_vaddr < page_end && other_end > page) writable_overlap = 1;
            }
            if (!writable_overlap && memory_user_protect(page, PAGE_SIZE, 0) < 0) return -1;
            if (page == last_page) break;
        }
    }
    *entry_point = header->e_entry;
    return 0;
}
