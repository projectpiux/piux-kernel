

#include <stdint.h>
#include <stddef.h>
#include "module.h"
#include "ksym.h"
#include "vfs_core.h"
#include "ext2.h"
#include "ramfs.h"
#include "memory.h"

#define MODULE_FILE_MAX   (256u * 1024u)
#define MODULE_ARENA_MAX  (4u * 1024u * 1024u)

#define ET_REL        1
#define EM_386        3
#define SHT_PROGBITS  1
#define SHT_SYMTAB    2
#define SHT_STRTAB    3
#define SHT_RELA      4
#define SHT_REL       9
#define SHT_NOBITS    8
#define SHF_ALLOC     0x0002
#define SHF_TLS       0x0400
#define SHN_UNDEF     0

typedef struct {
    uint8_t ident[16];
    uint16_t type;
    uint16_t machine;
    uint32_t version;
    uint32_t entry;
    uint32_t phoff;
    uint32_t shoff;
    uint32_t flags;
    uint16_t ehsize;
    uint16_t phentsize;
    uint16_t phnum;
    uint16_t shentsize;
    uint16_t shnum;
    uint16_t shstrndx;
} elf_header_t;

typedef struct {
    uint32_t name;
    uint32_t type;
    uint32_t flags;
    uint32_t addr;
    uint32_t offset;
    uint32_t size;
    uint32_t link;
    uint32_t info;
    uint32_t addralign;
    uint32_t entsize;
} elf_section_t;

typedef struct {
    uint32_t name;
    uint32_t value;
    uint32_t size;
    uint8_t info;
    uint8_t other;
    uint16_t shndx;
} elf_symbol_t;

typedef struct {
    uint32_t offset;
    uint32_t info;
    int32_t addend;
} elf_rela_t;

typedef struct {
    uint32_t offset;
    uint32_t info;
} elf_rel_t;

#define ELF_R_SYM(value)  ((value) >> 8)
#define ELF_R_TYPE(value) ((uint8_t)(value))

#define R_386_NONE    0
#define R_386_32      1
#define R_386_PC32    2
#define R_386_PLT32   4
#define R_386_GOTOFF  9
#define R_386_16      20
#define R_386_PC16    21
#define R_386_8       22
#define R_386_PC8     23

#define ELF_MAX_SECTIONS 64

#define DESCRIPTOR_SYMBOL "piux_module_descriptor"
#define EXPORTS_SYMBOL    "piux_module_exports"

static uint32_t text_length(const char *text) {
    uint32_t length = 0;
    while (text && text[length]) length++;
    return length;
}

static int text_compare(const char *left, const char *right) {
    while (*left && *left == *right) { left++; right++; }
    return (int)(unsigned char)*left - (int)(unsigned char)*right;
}

static void text_copy(char *destination, const char *source, uint32_t capacity) {
    uint32_t index = 0;
    if (!capacity || !source) return;
    while (source[index] && index + 1 < capacity) {
        destination[index] = source[index];
        index++;
    }
    destination[index] = '\0';
}

static void memory_copy(void *destination, const void *source, uint32_t size) {
    uint8_t *d = (uint8_t *)destination;
    const uint8_t *s = (const uint8_t *)source;
    for (uint32_t index = 0; index < size; index++) d[index] = s[index];
}

static char ksym_names[KSYM_MAX_SYMBOLS][KSYM_NAME_SIZE];
static uint32_t ksym_values[KSYM_MAX_SYMBOLS];
static int16_t ksym_owners[KSYM_MAX_SYMBOLS];   
static int ksym_count;

static int ksym_add(const char *name, uint32_t value, int owner) {
    if (!name || !name[0]) return -1;
    if (text_length(name) >= KSYM_NAME_SIZE) return -1;
    for (int index = 0; index < ksym_count; index++) {
        if (text_compare(ksym_names[index], name) == 0) return 0;   
    }
    if (ksym_count >= KSYM_MAX_SYMBOLS) return -1;
    text_copy(ksym_names[ksym_count], name, KSYM_NAME_SIZE);
    ksym_values[ksym_count] = value;
    ksym_owners[ksym_count] = (int16_t)owner;
    ksym_count++;
    return 0;
}

static int ksym_lookup(const char *name, uint32_t *value) {
    for (int index = 0; index < ksym_count; index++) {
        if (text_compare(ksym_names[index], name) == 0) {
            if (value) *value = ksym_values[index];
            return 1;
        }
    }
    return 0;
}

void ksym_forget_owner(int owner) {
    int output = 0;
    for (int index = 0; index < ksym_count; index++) {
        if (ksym_owners[index] == (int16_t)owner) continue;
        if (output != index) {
            for (int c = 0; c < KSYM_NAME_SIZE; c++) ksym_names[output][c] = ksym_names[index][c];
            ksym_values[output] = ksym_values[index];
            ksym_owners[output] = ksym_owners[index];
        }
        output++;
    }
    ksym_count = output;
}

int module_register_core_symbols(const char *const *names,
                                 const void *const *addresses,
                                 uint32_t count) {
    for (uint32_t index = 0; index < count; index++) {
        if (ksym_add(names[index], (uint32_t)(uintptr_t)addresses[index], 0) < 0) return -1;
    }
    return 0;
}

static module_image_t images[MODULE_MAX];
static uint8_t *module_arena;
static uint32_t module_arena_used;

void module_init(void) {
    module_arena = (uint8_t *)kmalloc(MODULE_ARENA_MAX);
    module_arena_used = 0;
    ksym_count = 0;
    for (int index = 0; index < MODULE_MAX; index++) {
        images[index].state = MODULE_STATE_EMPTY;
        images[index].name[0] = '\0';
        images[index].path[0] = '\0';
        images[index].info = 0;
        images[index].base = 0;
        images[index].size = 0;
        images[index].entry = 0;
    }
}

static uint8_t *arena_allocate(uint32_t size) {
    uint32_t aligned = (size + 15u) & ~15u;
    if (!module_arena) return 0;
    if (module_arena_used + aligned > MODULE_ARENA_MAX) return 0;
    {
        uint8_t *result = module_arena + module_arena_used;
        module_arena_used += aligned;
        return result;
    }
}

int module_count(void) { return MODULE_MAX; }

const module_image_t *module_at(int index) {
    if (index < 0 || index >= MODULE_MAX) return 0;
    if (images[index].state == MODULE_STATE_EMPTY) return 0;
    return &images[index];
}

const module_image_t *module_find(const char *name) {
    if (!name) return 0;
    for (int index = 0; index < MODULE_MAX; index++) {
        if (images[index].state == MODULE_STATE_EMPTY) continue;
        if (text_compare(images[index].name, name) == 0) return &images[index];
    }
    return 0;
}

static int empty_slot(void) {
    for (int index = 0; index < MODULE_MAX; index++) {
        if (images[index].state == MODULE_STATE_EMPTY) return index;
    }
    return -1;
}

typedef struct {
    const uint8_t *file;
    uint32_t size;
    const elf_header_t *header;
    elf_section_t *sections;
    elf_section_t section_table[ELF_MAX_SECTIONS];
    const elf_symbol_t *symbols;
    uint32_t symbol_count;
    const char *strings;
    uint32_t strings_size;
    uint8_t *image;
    uint32_t image_size;
    const piux_module_t *descriptor;
} elf_view_t;

static int range_ok(uint32_t size, uint32_t offset, uint32_t length) {
    return offset <= size && length <= size - offset;
}

static const elf_section_t *section_of(const elf_view_t *view, uint32_t index) {
    if (index == 0 || index >= view->header->shnum) return 0;
    return &view->sections[index];
}

static const char *symbol_name(const elf_view_t *view, const elf_symbol_t *symbol) {
    if (symbol->name >= view->strings_size) return "";
    return view->strings + symbol->name;
}

static int view_open(elf_view_t *view, const uint8_t *file, uint32_t size) {
    uint32_t index;
    uint32_t flat = 0;

    view->file = file;
    view->size = size;
    view->image = 0;
    view->image_size = 0;
    view->descriptor = 0;
    view->symbols = 0;
    view->strings = 0;
    view->symbol_count = 0;
    view->strings_size = 0;
    view->sections = view->section_table;

    if (size < sizeof(elf_header_t)) return -1;
    view->header = (const elf_header_t *)file;
    if (view->header->ident[0] != 0x7f || view->header->ident[1] != 'E' ||
        view->header->ident[2] != 'L' || view->header->ident[3] != 'F') return -1;
    if (view->header->ident[4] != 1 || view->header->ident[5] != 1) return -1;  
    if (view->header->type != ET_REL || view->header->machine != EM_386) return -1;
    if (view->header->shentsize != sizeof(elf_section_t) || view->header->shnum == 0) return -1;
    if (view->header->shnum > ELF_MAX_SECTIONS) return -1;
    if (!range_ok(size, view->header->shoff,
                  (uint32_t)view->header->shnum * sizeof(elf_section_t))) return -1;
    if (view->header->shstrndx >= view->header->shnum) return -1;

    memory_copy(view->section_table, file + view->header->shoff,
                (uint32_t)view->header->shnum * sizeof(elf_section_t));
    if (view->sections[view->header->shstrndx].type != SHT_STRTAB) return -1;

    for (index = 1; index < view->header->shnum; index++) {
        elf_section_t *section = &view->sections[index];
        uint32_t alignment;
        if (!(section->flags & SHF_ALLOC)) continue;
        if (section->flags & SHF_TLS) return -1;
        if (section->type != SHT_PROGBITS && section->type != SHT_NOBITS) continue;
        if (section->type == SHT_PROGBITS &&
            !range_ok(size, section->offset, section->size)) return -1;
        alignment = section->addralign ? section->addralign : 1;
        if (alignment > 32) alignment = 32;
        flat = (flat + alignment - 1) & ~(alignment - 1);
        section->addr = flat;
        flat += section->size;
    }
    view->image_size = flat;

    for (index = 1; index < view->header->shnum; index++) {
        const elf_section_t *section = &view->sections[index];
        const elf_section_t *strings;
        if (section->type != SHT_SYMTAB) continue;
        if (section->link >= view->header->shnum) return -1;
        strings = &view->sections[section->link];
        if (strings->type != SHT_STRTAB) return -1;
        if (!range_ok(size, strings->offset, strings->size)) return -1;
        if (!range_ok(size, section->offset, section->size)) return -1;
        if (section->size % sizeof(elf_symbol_t)) return -1;
        view->symbols = (const elf_symbol_t *)(file + section->offset);
        view->symbol_count = section->size / sizeof(elf_symbol_t);
        view->strings = (const char *)(file + strings->offset);
        view->strings_size = strings->size;
        break;
    }
    if (!view->symbols) return -1;
    return 0;
}

static int view_build_image(elf_view_t *view, uint8_t *image) {
    uint32_t index;
    view->image = image;
    for (index = 0; index < view->image_size; index++) image[index] = 0;
    for (index = 1; index < view->header->shnum; index++) {
        const elf_section_t *section = &view->sections[index];
        if (!(section->flags & SHF_ALLOC)) continue;
        if (section->type != SHT_PROGBITS) continue;   
        memory_copy(image + section->addr, view->file + section->offset, section->size);
    }
    return 0;
}

static int view_descriptor(elf_view_t *view, char *detail) {
    uint32_t index;
    (void)detail;
    for (index = 0; index < view->symbol_count; index++) {
        const elf_symbol_t *symbol = &view->symbols[index];
        const elf_section_t *section;
        uint32_t offset;
        if (symbol->shndx == SHN_UNDEF) continue;
        if (text_compare(symbol_name(view, symbol), DESCRIPTOR_SYMBOL) != 0) continue;
        section = section_of(view, symbol->shndx);
        if (!section || !(section->flags & SHF_ALLOC)) return -1;
        if (symbol->value + sizeof(piux_module_t) > section->size) return -1;
        offset = section->addr + symbol->value;
        if (!range_ok(view->image_size, offset, sizeof(piux_module_t))) return -1;
        view->descriptor = (const piux_module_t *)(view->image + offset);
        if (view->descriptor->signature != PIUX_MODULE_SIGNATURE) return -1;
        if (view->descriptor->abi > PIUX_MODULE_ABI) return -3;
        return 0;
    }
    return -1;
}

static int view_validate(const elf_view_t *view, char *detail) {
    const piux_module_t *info = view->descriptor;
    if (!info) return -1;
    if (!info->name || !info->name[0]) return -1;
    if (!info->version) return -1;
    if (!info->release || text_compare(info->release, PIUX_MODULE_RELEASE) != 0) {
        text_copy(detail, info->release ? info->release : "?", 64);
        return -3;
    }
    if (info->dep_count > PIUX_MODULE_MAX_DEPS) return -1;
    if (info->dep_count && !info->deps) return -1;
    return 0;
}

static uint32_t symbol_address(const elf_view_t *view, const elf_symbol_t *symbol) {
    const elf_section_t *section = section_of(view, symbol->shndx);
    if (!section) return 0;
    return section->addr + symbol->value;
}

static int apply_relocation_table(elf_view_t *view, const elf_section_t *table, char *detail) {
    const elf_section_t *target;
    uint32_t entry_size = table->type == SHT_RELA ? sizeof(elf_rela_t) : sizeof(elf_rel_t);
    uint32_t offset;

    target = section_of(view, table->info);
    if (!target || !(target->flags & SHF_ALLOC)) return 0;
    if (!range_ok(view->size, table->offset, table->size)) return -1;
    if (!entry_size || table->size % entry_size) return -1;

    for (offset = 0; offset + entry_size <= table->size; offset += entry_size) {
        const uint8_t *raw = view->file + table->offset + offset;
        uint32_t symbol_index, relocation_offset, info;
        int32_t addend;
        const elf_symbol_t *symbol;
        uint8_t type;
        uint32_t value;
        uint8_t *place;
        uint32_t address;

        if (table->type == SHT_RELA) {
            const elf_rela_t *relocation = (const elf_rela_t *)raw;
            symbol_index = ELF_R_SYM(relocation->info);
            relocation_offset = relocation->offset;
            info = relocation->info;
            addend = relocation->addend;
        } else {
            const elf_rel_t *relocation = (const elf_rel_t *)raw;
            symbol_index = ELF_R_SYM(relocation->info);
            relocation_offset = relocation->offset;
            info = relocation->info;
            if (relocation_offset + 4 > target->size) return -1;

            addend = (int32_t)*(const uint32_t *)(view->image + target->addr + relocation_offset);
        }

        if (relocation_offset + 4 > target->size) return -1;
        if (symbol_index >= view->symbol_count) return -1;
        symbol = &view->symbols[symbol_index];
        type = ELF_R_TYPE(info);
        place = view->image + target->addr + relocation_offset;

        if (symbol->shndx == SHN_UNDEF) {
            if (!ksym_lookup(symbol_name(view, symbol), &value)) {
                text_copy(detail, symbol_name(view, symbol), 64);
                return -2;
            }
        } else {
            value = (uint32_t)(uintptr_t)view->image + symbol_address(view, symbol);
        }

        address = (uint32_t)(uintptr_t)place;
        switch (type) {
            case R_386_NONE:
                break;
            case R_386_32:
                *(uint32_t *)place = (uint32_t)addend + value;
                break;
            case R_386_PC32:
            case R_386_PLT32:
                *(uint32_t *)place = (uint32_t)addend + value - address;
                break;
            case R_386_GOTOFF:
                *(uint32_t *)place = (uint32_t)addend + value - (uint32_t)(uintptr_t)view->image;
                break;
            case R_386_16:
                *(uint16_t *)place = (uint16_t)((uint32_t)addend + value);
                break;
            case R_386_PC16:
                *(uint16_t *)place = (uint16_t)((uint32_t)addend + value - address);
                break;
            case R_386_8:
                *(uint8_t *)place = (uint8_t)((uint32_t)addend + value);
                break;
            case R_386_PC8:
                *(uint8_t *)place = (uint8_t)((uint32_t)addend + value - address);
                break;
            default:
                return -4;   
        }
    }
    return 0;
}

static int apply_relocations(elf_view_t *view, char *detail) {
    uint32_t index;
    for (index = 1; index < view->header->shnum; index++) {
        const elf_section_t *table = &view->sections[index];
        int result;
        if (table->type != SHT_RELA && table->type != SHT_REL) continue;
        result = apply_relocation_table(view, table, detail);
        if (result) return result;
    }
    return 0;
}

static void register_exports(elf_view_t *view, int slot) {
    module_exports_fn exports = 0;
    uint32_t index;

    for (index = 0; index < view->symbol_count; index++) {
        const elf_symbol_t *symbol = &view->symbols[index];
        if (symbol->shndx == SHN_UNDEF) continue;
        if (text_compare(symbol_name(view, symbol), EXPORTS_SYMBOL) != 0) continue;
        exports = (module_exports_fn)(uintptr_t)((uint32_t)(uintptr_t)view->image +
                                                 symbol_address(view, symbol));
        break;
    }
    if (!exports) return;
    {
        const char *const *names = 0;
        const void *const *addresses = 0;
        uint32_t count = 0;
        exports(&names, &addresses, &count);
        for (index = 0; index < count; index++) {
            ksym_add(names[index], (uint32_t)(uintptr_t)addresses[index], slot + 1);
        }
    }
}

static int load_module_file(const char *name, int depth, char *detail);

static int load_image(const char *requested_name, int depth, char *detail) {
    static uint8_t file[MODULE_FILE_MAX];
    char path[PIUX_MODULE_PATH_SIZE];
    elf_view_t view;
    int size;
    int slot;
    int result;
    uint8_t *image;

    detail[0] = '\0';
    text_copy(path, "/lib/modules/", sizeof(path));
    text_copy(path + text_length("/lib/modules/"), requested_name,
              sizeof(path) - text_length("/lib/modules/") - 5);
    text_copy(path + text_length(path), ".mod", sizeof(path) - text_length(path));

    size = ext2_is_mounted() ? ext2_read_file_by_path(path, (char *)file, MODULE_FILE_MAX) : -1;
    if (size <= 0) size = ramfs_read_file(path, (char *)file, MODULE_FILE_MAX);
    if (size <= 0) {
        text_copy(detail, path, 64);
        return -5;
    }

    if (view_open(&view, file, (uint32_t)size) < 0) {
        text_copy(detail, "invalid ELF (magic/type/machine)", 64);
        return -1;
    }

    slot = empty_slot();
    if (slot < 0) {
        text_copy(detail, "no free module slot", 64);
        return -1;
    }

    image = arena_allocate(view.image_size ? view.image_size : 4);
    if (!image) {
        text_copy(detail, "module arena full", 64);
        return -1;
    }
    view_build_image(&view, image);

    result = view_descriptor(&view, detail);
    if (result < 0) {
        if (!detail[0]) text_copy(detail, "no piux_module_descriptor", 64);
        return result;
    }
    if (module_find(view.descriptor->name)) return 1;
    if (depth > MODULE_MAX) {
        text_copy(detail, "dependency chain too deep", 64);
        return -1;
    }

    result = apply_relocations(&view, detail);
    if (result < 0) {
        if (!detail[0]) text_copy(detail, "relocation error", 64);
        return result;
    }

    result = view_descriptor(&view, detail);
    if (result < 0) {
        text_copy(detail, "descriptor lost after relocation", 64);
        return -1;
    }
    result = view_validate(&view, detail);
    if (result < 0) {
        if (!detail[0]) text_copy(detail, "invalid descriptor", 64);
        return result;
    }

    for (uint32_t index = 0; index < view.descriptor->dep_count; index++) {
        const char *dependency = view.descriptor->deps[index];
        if (!dependency || !dependency[0]) {
            text_copy(detail, "invalid dependency list", 64);
            return -1;
        }
        result = load_module_file(dependency, depth + 1, detail);
        if (result < 0) return result;
    }

    images[slot].info = view.descriptor;
    text_copy(images[slot].name, view.descriptor->name, PIUX_MODULE_NAME_SIZE);
    text_copy(images[slot].path, path, PIUX_MODULE_PATH_SIZE);
    images[slot].base = (uint32_t)(uintptr_t)image;
    images[slot].size = view.image_size;
    images[slot].entry = 0;
    images[slot].state = MODULE_STATE_LOADED;

    register_exports(&view, slot);

    if (view.descriptor->init && view.descriptor->init() != 0) {
        ksym_forget_owner(slot + 1);
        images[slot].state = MODULE_STATE_FAILED;
        text_copy(detail, "init() failed", 64);
        return -1;
    }

    {
        const module_image_t *loaded = &images[slot];
        (void)loaded;

        {
            module_exports_fn exports = 0;
            uint32_t index;
            for (index = 0; index < view.symbol_count; index++) {
                const elf_symbol_t *symbol = &view.symbols[index];
                if (symbol->shndx == SHN_UNDEF) continue;
                if (text_compare(symbol_name(&view, symbol), EXPORTS_SYMBOL) != 0) continue;
                exports = (module_exports_fn)(uintptr_t)((uint32_t)(uintptr_t)view.image +
                                                         symbol_address(&view, symbol));
                break;
            }
            if (exports) {
                const char *const *names = 0;
                const void *const *addresses = 0;
                uint32_t count = 0;
                exports(&names, &addresses, &count);
                images[slot].entry = (count && addresses[0]) ?
                    (void (*)(void))(uintptr_t)addresses[0] : 0;
            }
        }
    }

    images[slot].state = MODULE_STATE_RUNNING;
    return 0;
}

static int load_module_file(const char *name, int depth, char *detail) {
    if (!name || !name[0]) return -1;
    if (module_find(name)) return 1;
    if (depth > MODULE_MAX) return -1;
    return load_image(name, depth, detail);
}

char module_last_detail[80];

int module_load(const char *path) {
    const char *name = path;
    if (!path || !path[0]) return -1;
    while (*path) {
        if (*path == '/') name = path + 1;
        path++;
    }
    return module_load_by_name(name);
}

int module_load_by_name(const char *name) {
    char detail[80];
    int result;
    if (!name || !name[0]) return -1;
    detail[0] = '\0';
    result = load_module_file(name, 0, detail);
    if (result < 0) {
        if (!detail[0]) text_copy(detail, "load failed", 64);
        text_copy(module_last_detail, detail, sizeof(module_last_detail));
    }
    return result;
}

int module_unload(const char *name) {
    for (int index = MODULE_MAX - 1; index >= 0; index--) {
        if (images[index].state == MODULE_STATE_EMPTY) continue;
        if (text_compare(images[index].name, name) != 0) continue;
        if (images[index].info && images[index].info->exit) images[index].info->exit();
        ksym_forget_owner(index + 1);
        images[index].state = MODULE_STATE_EMPTY;
        images[index].info = 0;
        images[index].entry = 0;
        return 0;
    }
    return -1;
}

int module_reload(const char *name) {
    if (module_unload(name) < 0) return -1;
    return module_load_by_name(name);
}

void module_describe(const module_image_t *image, char *buffer, uint32_t size) {
    const char *state = "?";
    uint32_t position = 0;
    if (!image || !size) return;
    switch (image->state) {
        case MODULE_STATE_LOADED:  state = "loaded";  break;
        case MODULE_STATE_RUNNING: state = "running"; break;
        case MODULE_STATE_FAILED:  state = "failed";  break;
        default: break;
    }
    buffer[0] = '\0';
    {
        const char *parts[3];
        parts[0] = image->name;
        parts[1] = image->info && image->info->version ? image->info->version : "?";
        parts[2] = state;
        for (int part = 0; part < 3; part++) {
            const char *text = parts[part];
            if (part && position + 1 < size) buffer[position++] = ' ';
            while (*text && position + 1 < size) buffer[position++] = *text++;
        }
    }
    buffer[position] = '\0';
}
