#include <stdint.h>
#include "../core/io.h"
#include "ata.h"
#include "ext2.h"
#include "../core/auth.h"

ext2_filesystem_t fs;
static int fs_initialized;
static uint8_t block_buffer[EXT2_MAX_BLOCK_SIZE];
static uint8_t inode_buffer[EXT2_MAX_BLOCK_SIZE];

static uint32_t min_u32(uint32_t a, uint32_t b) { return a < b ? a : b; }

static int name_length(const char *name) {
    int length = 0;
    while (name[length] != '\0' && length <= EXT2_MAX_NAME_LEN) length++;
    return length;
}

static int name_equal(const char *name, uint32_t length, const char *wanted) {
    int wanted_length = name_length(wanted);
    if (wanted_length != (int)length) return 0;
    for (uint32_t i = 0; i < length; i++) if (name[i] != wanted[i]) return 0;
    return 1;
}

static uint32_t block_count_for_size(uint32_t size) {
    return (size + fs.block_size - 1) / fs.block_size;
}

static uint32_t metadata_block_count(uint32_t data_blocks) {
    uint32_t pointers = fs.block_size / sizeof(uint32_t);
    uint32_t metadata = 0;
    if (data_blocks > 12) {
        metadata++;
        if (data_blocks > 12 + pointers) {
            metadata++;
            metadata += (data_blocks - 12 - pointers + pointers - 1) / pointers;
        }
    }
    return metadata;
}

static uint32_t descriptor_block(void) { return fs.block_size == 1024 ? 2 : 1; }

static int read_group_descriptor(uint32_t group, ext2_group_descriptor_t *descriptor) {
    uint32_t byte_offset;
    uint32_t block;
    uint32_t offset;
    if (group >= fs.group_count) return -1;
    byte_offset = group * sizeof(ext2_group_descriptor_t);
    block = descriptor_block() + byte_offset / fs.block_size;
    offset = byte_offset % fs.block_size;
    if (ext2_read_block(block, block_buffer) < 0) return -1;
    *descriptor = *(ext2_group_descriptor_t *)(block_buffer + offset);
    return 0;
}

static int write_group_descriptor(uint32_t group, const ext2_group_descriptor_t *descriptor) {
    uint32_t byte_offset;
    uint32_t block;
    uint32_t offset;
    if (group >= fs.group_count) return -1;
    byte_offset = group * sizeof(ext2_group_descriptor_t);
    block = descriptor_block() + byte_offset / fs.block_size;
    offset = byte_offset % fs.block_size;
    if (ext2_read_block(block, block_buffer) < 0) return -1;
    *(ext2_group_descriptor_t *)(block_buffer + offset) = *descriptor;
    return ext2_write_block(block, block_buffer);
}

static int write_superblock(void) {
    uint8_t buffer[1024];
    if (ata_read_sectors(2, 2, buffer) < 0) return -1;
    *(ext2_superblock_t *)buffer = fs.superblock;
    return ata_write_sectors(2, 2, buffer);
}

int ext2_mount(void) {
    uint8_t buffer[1024];
    uint32_t groups_by_blocks;
    uint32_t groups_by_inodes;
    uint32_t unsupported;
    fs_initialized = 0;
    ata_init();
    if (ata_read_sectors(2, 2, buffer) < 0) return -1;
    fs.superblock = *(ext2_superblock_t *)buffer;
    if (fs.superblock.magic != EXT2_MAGIC) return -1;
    if (fs.superblock.log_block_size > 2 || fs.superblock.blocks_per_group == 0 ||
        fs.superblock.inodes_per_group == 0 ||
        fs.superblock.blocks_count <= fs.superblock.first_data_block) return -1;
    unsupported = fs.superblock.feature_incompat & EXT2_FEATURE_INCOMPAT_UNSUPPORTED;
    if (unsupported != 0) return -1;
    fs.block_size = 1024U << fs.superblock.log_block_size;
    fs.inode_size = fs.superblock.inode_size == 0 ? EXT2_INODE_SIZE : fs.superblock.inode_size;
    if (fs.inode_size < EXT2_INODE_SIZE || fs.inode_size > fs.block_size ||
        (fs.block_size % fs.inode_size) != 0) return -1;
    groups_by_blocks = (fs.superblock.blocks_count - fs.superblock.first_data_block +
                        fs.superblock.blocks_per_group - 1) / fs.superblock.blocks_per_group;
    groups_by_inodes = (fs.superblock.inodes_count + fs.superblock.inodes_per_group - 1) /
                       fs.superblock.inodes_per_group;
    fs.group_count = groups_by_blocks > groups_by_inodes ? groups_by_blocks : groups_by_inodes;
    if (fs.group_count == 0) return -1;
    fs.first_data_block = fs.superblock.first_data_block;
    fs.current_dir_inode = 2;
    fs.current_path[0] = '~';
    fs.current_path[1] = '\0';
    fs_initialized = 1;
    return 0;
}

int ext2_is_mounted(void) { return fs_initialized; }

int ext2_read_block(uint32_t block_num, uint8_t *buffer) {
    uint32_t sector;
    uint32_t sectors;
    if (!fs_initialized || block_num >= fs.superblock.blocks_count) return -1;
    sector = block_num * (fs.block_size / 512);
    sectors = fs.block_size / 512;
    return ata_read_sectors(sector, sectors, buffer);
}

int ext2_write_block(uint32_t block_num, uint8_t *buffer) {
    uint32_t sector;
    uint32_t sectors;
    if (!fs_initialized || block_num >= fs.superblock.blocks_count) return -1;
    sector = block_num * (fs.block_size / 512);
    sectors = fs.block_size / 512;
    return ata_write_sectors(sector, sectors, buffer);
}

int ext2_read_inode(uint32_t inode_num, ext2_inode_t *inode) {
    ext2_group_descriptor_t descriptor;
    uint32_t zero_based;
    uint32_t group;
    uint32_t index;
    uint32_t byte_offset;
    uint32_t block;
    uint32_t offset;
    uint32_t first_part;
    if (!fs_initialized || inode == 0 || inode_num == 0 || inode_num > fs.superblock.inodes_count) return -1;
    zero_based = inode_num - 1;
    group = zero_based / fs.superblock.inodes_per_group;
    index = zero_based % fs.superblock.inodes_per_group;
    if (read_group_descriptor(group, &descriptor) < 0) return -1;
    byte_offset = index * fs.inode_size;
    block = descriptor.inode_table + byte_offset / fs.block_size;
    offset = byte_offset % fs.block_size;
    if (ext2_read_block(block, block_buffer) < 0) return -1;
    first_part = min_u32(fs.inode_size, fs.block_size - offset);
    memcpy_safe(inode_buffer, block_buffer + offset, first_part);
    if (first_part < fs.inode_size) {
        if (ext2_read_block(block + 1, block_buffer) < 0) return -1;
        memcpy_safe(inode_buffer + first_part, block_buffer, fs.inode_size - first_part);
    }
    memset_safe(inode, 0, sizeof(*inode));
    memcpy_safe(inode, inode_buffer, min_u32(sizeof(*inode), fs.inode_size));
    return 0;
}

int ext2_write_inode(uint32_t inode_num, ext2_inode_t *inode) {
    ext2_group_descriptor_t descriptor;
    uint32_t zero_based;
    uint32_t group;
    uint32_t index;
    uint32_t byte_offset;
    uint32_t block;
    uint32_t offset;
    uint32_t first_part;
    if (!fs_initialized || inode == 0 || inode_num == 0 || inode_num > fs.superblock.inodes_count) return -1;
    zero_based = inode_num - 1;
    group = zero_based / fs.superblock.inodes_per_group;
    index = zero_based % fs.superblock.inodes_per_group;
    if (read_group_descriptor(group, &descriptor) < 0) return -1;
    byte_offset = index * fs.inode_size;
    block = descriptor.inode_table + byte_offset / fs.block_size;
    offset = byte_offset % fs.block_size;
    if (ext2_read_block(block, block_buffer) < 0) return -1;
    first_part = min_u32(fs.inode_size, fs.block_size - offset);
    memcpy_safe(inode_buffer, block_buffer + offset, first_part);
    if (first_part < fs.inode_size) {
        if (ext2_read_block(block + 1, block_buffer) < 0) return -1;
        memcpy_safe(inode_buffer + first_part, block_buffer, fs.inode_size - first_part);
    }
    memcpy_safe(inode_buffer, inode, min_u32(sizeof(*inode), fs.inode_size));
    if (ext2_read_block(block, block_buffer) < 0) return -1;
    memcpy_safe(block_buffer + offset, inode_buffer, first_part);
    if (ext2_write_block(block, block_buffer) < 0) return -1;
    if (first_part < fs.inode_size) {
        if (ext2_read_block(block + 1, block_buffer) < 0) return -1;
        memcpy_safe(block_buffer, inode_buffer + first_part, fs.inode_size - first_part);
        if (ext2_write_block(block + 1, block_buffer) < 0) return -1;
    }
    return 0;
}

static int read_pointer(uint32_t block, uint32_t index, uint32_t *value) {
    if (block == 0 || index >= fs.block_size / sizeof(uint32_t) || ext2_read_block(block, block_buffer) < 0) return -1;
    *value = ((uint32_t *)block_buffer)[index];
    return 0;
}

static int inode_block(const ext2_inode_t *inode, uint32_t logical, uint32_t *physical) {
    uint32_t pointers = fs.block_size / sizeof(uint32_t);
    uint32_t first;
    uint32_t second;
    if (logical < 12) {
        *physical = inode->block[logical];
        return 0;
    }
    logical -= 12;
    if (logical < pointers) return read_pointer(inode->indirect_block, logical, physical);
    logical -= pointers;
    if (logical < pointers * pointers) {
        if (read_pointer(inode->doubly_indirect_block, logical / pointers, &first) < 0) return -1;
        return read_pointer(first, logical % pointers, physical);
    }
    logical -= pointers * pointers;
    if (logical >= pointers * pointers * pointers) return -1;
    if (read_pointer(inode->triply_indirect_block, logical / (pointers * pointers), &first) < 0) return -1;
    if (read_pointer(first, (logical / pointers) % pointers, &second) < 0) return -1;
    return read_pointer(second, logical % pointers, physical);
}

static int allocate_block(uint32_t *result) {
    for (uint32_t group = 0; group < fs.group_count; group++) {
        ext2_group_descriptor_t descriptor;
        uint32_t group_blocks = fs.superblock.blocks_per_group;
        uint32_t first = fs.first_data_block + group * fs.superblock.blocks_per_group;
        if (first >= fs.superblock.blocks_count) continue;
        if (first + group_blocks > fs.superblock.blocks_count) group_blocks = fs.superblock.blocks_count - first;
        if (read_group_descriptor(group, &descriptor) < 0 || descriptor.free_blocks_count == 0) continue;
        if (ext2_read_block(descriptor.block_bitmap, block_buffer) < 0) return -1;
        for (uint32_t bit = 0; bit < group_blocks; bit++) {
            if ((block_buffer[bit / 8] & (1U << (bit % 8))) == 0) {
                block_buffer[bit / 8] |= (uint8_t)(1U << (bit % 8));
                if (ext2_write_block(descriptor.block_bitmap, block_buffer) < 0) return -1;
                descriptor.free_blocks_count--;
                fs.superblock.free_blocks_count--;
                if (write_group_descriptor(group, &descriptor) < 0 || write_superblock() < 0) return -1;
                *result = first + bit;
                memset_safe(block_buffer, 0, fs.block_size);
                return ext2_write_block(*result, block_buffer);
            }
        }
    }
    return -1;
}

static int allocate_inode(uint32_t *result) {
    for (uint32_t group = 0; group < fs.group_count; group++) {
        ext2_group_descriptor_t descriptor;
        uint32_t group_inodes = fs.superblock.inodes_per_group;
        uint32_t first = group * fs.superblock.inodes_per_group;
        uint32_t first_free = fs.superblock.first_ino > first ? fs.superblock.first_ino - first - 1 : 0;
        if (first >= fs.superblock.inodes_count) continue;
        if (first + group_inodes > fs.superblock.inodes_count) group_inodes = fs.superblock.inodes_count - first;
        if (read_group_descriptor(group, &descriptor) < 0 || descriptor.free_inodes_count == 0) continue;
        if (ext2_read_block(descriptor.inode_bitmap, block_buffer) < 0) return -1;
        for (uint32_t bit = first_free; bit < group_inodes; bit++) {
            if ((block_buffer[bit / 8] & (1U << (bit % 8))) == 0) {
                block_buffer[bit / 8] |= (uint8_t)(1U << (bit % 8));
                if (ext2_write_block(descriptor.inode_bitmap, block_buffer) < 0) return -1;
                descriptor.free_inodes_count--;
                fs.superblock.free_inodes_count--;
                if (write_group_descriptor(group, &descriptor) < 0 || write_superblock() < 0) return -1;
                *result = first + bit + 1;
                return 0;
            }
        }
    }
    return -1;
}

int ext2_find_inode(const char *filename) { return ext2_find_inode_in_dir(fs.current_dir_inode, filename); }

int ext2_find_inode_in_dir(uint32_t dir_inode, const char *filename);

static int path_next_component(const char **path, char *component) {
    uint32_t length = 0;
    while (**path == '/') (*path)++;
    if (**path == '\0') return 0;
    while (**path != '\0' && **path != '/') {
        if (length >= EXT2_MAX_NAME_LEN) return -1;
        component[length++] = *(*path)++;
    }
    component[length] = '\0';
    return 1;
}

int ext2_find_inode_by_path(const char *path) {
    uint32_t inode = fs.current_dir_inode;
    char component[EXT2_MAX_NAME_LEN + 1];
    int component_result;
    if (!fs_initialized || path == 0 || *path == '\0') return -1;
    if (path[0] == '/' || (path[0] == '~' && (path[1] == '\0' || path[1] == '/'))) {
        inode = 2;
        if (path[0] == '~') path++;
    }
    while ((component_result = path_next_component(&path, component)) > 0) {
        if (component[0] == '.' && component[1] == '\0') continue;
        if (component[0] == '.' && component[1] == '.' && component[2] == '\0') {
            int parent = ext2_find_inode_in_dir(inode, "..");
            if (parent < 0) return -1;
            inode = (uint32_t)parent;
        } else {
            int child = ext2_find_inode_in_dir(inode, component);
            if (child < 0) return -1;
            inode = (uint32_t)child;
        }
    }
    if (component_result < 0) return -1;
    return (int)inode;
}

static int split_parent_path(const char *path, char *parent_path, char *name) {
    int length = 0;
    int last_separator = -1;
    if (path == 0 || *path == '\0') return -1;
    while (path[length]) {
        if (path[length] == '/') last_separator = length;
        length++;
    }
    if (length == 0 || length > 255) return -1;
    int name_start = last_separator + 1;
    int name_length_value = length - name_start;
    if (name_length_value == 0 || name_length_value > EXT2_MAX_NAME_LEN) return -1;
    for (int i = 0; i < name_length_value; i++) name[i] = path[name_start + i];
    name[name_length_value] = '\0';
    if (last_separator < 0) {
        parent_path[0] = '\0';
    } else if (last_separator == 0) {
        parent_path[0] = '/';
        parent_path[1] = '\0';
    } else {
        for (int i = 0; i < last_separator; i++) parent_path[i] = path[i];
        parent_path[last_separator] = '\0';
    }
    return 0;
}

int ext2_find_inode_in_dir(uint32_t dir_inode, const char *filename) {
    ext2_inode_t inode;
    uint32_t blocks;
    int wanted_length = name_length(filename);
    if (wanted_length == 0 || wanted_length > EXT2_MAX_NAME_LEN ||
        ext2_read_inode(dir_inode, &inode) < 0 || (inode.mode & EXT2_S_IFMT) != EXT2_S_IFDIR) return -1;
    blocks = block_count_for_size(inode.size);
    for (uint32_t logical = 0; logical < blocks; logical++) {
        uint32_t physical;
        if (inode_block(&inode, logical, &physical) < 0 || physical == 0 || ext2_read_block(physical, block_buffer) < 0) return -1;
        uint32_t offset = 0;
        uint32_t limit = min_u32(fs.block_size, inode.size - logical * fs.block_size);
        while (offset + 8 <= limit) {
            ext2_dir_entry_t *entry = (ext2_dir_entry_t *)(block_buffer + offset);
            if (entry->rec_len < 8 || entry->rec_len > limit - offset || entry->name_len > entry->rec_len - 8) break;
            if (entry->inode != 0 && name_equal(entry->name, entry->name_len, filename)) return entry->inode;
            offset += entry->rec_len;
        }
    }
    return -1;
}

int ext2_read_file(uint32_t inode_num, char *buffer, uint32_t size) {
    return ext2_read_file_range(inode_num, 0, buffer, size);
}

int ext2_read_file_range(uint32_t inode_num, uint32_t offset, char *buffer, uint32_t size) {
    ext2_inode_t inode;
    uint32_t wanted;
    uint32_t bytes_read = 0;
    if (buffer == 0 || ext2_read_inode(inode_num, &inode) < 0) return -1;
    if (offset >= inode.size) return 0;
    wanted = min_u32(size, inode.size - offset);
    while (bytes_read < wanted) {
        uint32_t physical;
        uint32_t file_offset = offset + bytes_read;
        uint32_t block_offset = file_offset % fs.block_size;
        uint32_t amount = min_u32(wanted - bytes_read, fs.block_size - block_offset);
        if (inode_block(&inode, file_offset / fs.block_size, &physical) < 0) return -1;
        if (physical == 0) {
            memset_safe(buffer + bytes_read, 0, amount);
        } else {
            if (ext2_read_block(physical, block_buffer) < 0) return -1;
            memcpy_safe(buffer + bytes_read, block_buffer + block_offset, amount);
        }
        bytes_read += amount;
    }
    return (int)bytes_read;
}

int ext2_read_file_by_name(const char *filename, char *buffer, uint32_t size) {
    int inode = ext2_find_inode(filename);
    return inode < 0 ? -1 : ext2_read_file((uint32_t)inode, buffer, size);
}

static int set_inode_block(ext2_inode_t *inode, uint32_t logical, uint32_t physical) {
    uint32_t pointers = fs.block_size / sizeof(uint32_t);
    uint32_t index;
    uint32_t table;
    uint32_t child;
    if (logical < 12) {
        inode->block[logical] = physical;
        return 0;
    }
    logical -= 12;
    if (logical < pointers) {
        if (inode->indirect_block == 0 && allocate_block(&inode->indirect_block) < 0) return -1;
        if (ext2_read_block(inode->indirect_block, block_buffer) < 0) return -1;
        ((uint32_t *)block_buffer)[logical] = physical;
        return ext2_write_block(inode->indirect_block, block_buffer);
    }
    logical -= pointers;
    if (logical < pointers * pointers) {
        if (inode->doubly_indirect_block == 0 && allocate_block(&inode->doubly_indirect_block) < 0) return -1;
        table = logical / pointers;
        index = logical % pointers;
        if (read_pointer(inode->doubly_indirect_block, table, &child) < 0) return -1;
        if (child == 0) {
            if (allocate_block(&child) < 0) return -1;
            if (ext2_read_block(inode->doubly_indirect_block, block_buffer) < 0) return -1;
            ((uint32_t *)block_buffer)[table] = child;
            if (ext2_write_block(inode->doubly_indirect_block, block_buffer) < 0) return -1;
        }
        if (ext2_read_block(child, block_buffer) < 0) return -1;
        ((uint32_t *)block_buffer)[index] = physical;
        return ext2_write_block(child, block_buffer);
    }
    return -1;
}

int ext2_write_file(uint32_t inode_num, const char *buffer, uint32_t size) {
    ext2_inode_t inode;
    uint32_t old_blocks;
    uint32_t new_blocks;
    if (buffer == 0 || ext2_read_inode(inode_num, &inode) < 0 || (inode.mode & EXT2_S_IFMT) != EXT2_S_IFREG) return -1;
    if (size < inode.size) return -1;
    old_blocks = block_count_for_size(inode.size);
    new_blocks = block_count_for_size(size);
    if (new_blocks > 12 + fs.block_size / 4 + (fs.block_size / 4) * (fs.block_size / 4)) return -1;
    for (uint32_t logical = old_blocks; logical < new_blocks; logical++) {
        uint32_t physical;
        if (allocate_block(&physical) < 0 || set_inode_block(&inode, logical, physical) < 0) return -1;
    }
    for (uint32_t logical = 0; logical < new_blocks; logical++) {
        uint32_t physical;
        uint32_t amount = min_u32(size - logical * fs.block_size, fs.block_size);
        if (inode_block(&inode, logical, &physical) < 0 || physical == 0) return -1;
        memset_safe(block_buffer, 0, fs.block_size);
        memcpy_safe(block_buffer, buffer + logical * fs.block_size, amount);
        if (ext2_write_block(physical, block_buffer) < 0) return -1;
    }
    inode.size = size;
    inode.blocks = (new_blocks + metadata_block_count(new_blocks)) * (fs.block_size / 512);
    return ext2_write_inode(inode_num, &inode) < 0 ? -1 : (int)size;
}

static int add_directory_entry(ext2_inode_t *directory, uint32_t inode_num, const char *name, uint8_t type) {
    uint32_t length = name_length(name);
    uint32_t required = (length + 8 + 3) & ~3U;
    uint32_t blocks = block_count_for_size(directory->size);
    for (uint32_t logical = 0; logical < blocks; logical++) {
        uint32_t physical;
        if (inode_block(directory, logical, &physical) < 0 || physical == 0 || ext2_read_block(physical, block_buffer) < 0) return -1;
        uint32_t offset = 0;
        while (offset + 8 <= fs.block_size) {
            ext2_dir_entry_t *entry = (ext2_dir_entry_t *)(block_buffer + offset);
            uint32_t actual = (entry->inode == 0) ? 8 : ((entry->name_len + 8 + 3) & ~3U);
            if (entry->rec_len < 8 || entry->rec_len > fs.block_size - offset || entry->name_len > entry->rec_len - 8) break;
            if (entry->inode == 0 && entry->rec_len >= required) {
                entry->inode = inode_num;
                entry->name_len = (uint8_t)length;
                entry->file_type = type;
                for (uint32_t i = 0; i < length; i++) entry->name[i] = name[i];
                return ext2_write_block(physical, block_buffer);
            }
            if (entry->inode != 0 && entry->rec_len >= actual + required) {
                uint16_t old_length = entry->rec_len;
                entry->rec_len = (uint16_t)actual;
                ext2_dir_entry_t *new_entry = (ext2_dir_entry_t *)(block_buffer + offset + actual);
                new_entry->inode = inode_num;
                new_entry->rec_len = old_length - actual;
                new_entry->name_len = (uint8_t)length;
                new_entry->file_type = type;
                for (uint32_t i = 0; i < length; i++) new_entry->name[i] = name[i];
                return ext2_write_block(physical, block_buffer);
            }
            offset += entry->rec_len;
        }
    }
    if (blocks >= 12) return -1;
    uint32_t physical;
    if (allocate_block(&physical) < 0 || set_inode_block(directory, blocks, physical) < 0) return -1;
    memset_safe(block_buffer, 0, fs.block_size);
    ext2_dir_entry_t *entry = (ext2_dir_entry_t *)block_buffer;
    entry->inode = inode_num;
    entry->rec_len = (uint16_t)fs.block_size;
    entry->name_len = (uint8_t)length;
    entry->file_type = type;
    for (uint32_t i = 0; i < length; i++) entry->name[i] = name[i];
    if (ext2_write_block(physical, block_buffer) < 0) return -1;
    directory->size += fs.block_size;
    directory->blocks += fs.block_size / 512;
    return 0;
}

void ext2_print_dir_contents(uint32_t dir_inode, void (*vga_puts)(const char*)) {
    ext2_inode_t inode;
    char name[EXT2_MAX_NAME_LEN + 1];
    if (ext2_read_inode(dir_inode, &inode) < 0 || (inode.mode & EXT2_S_IFMT) != EXT2_S_IFDIR) {
        vga_puts("Error reading directory\n");
        return;
    }
    uint32_t blocks = block_count_for_size(inode.size);
    for (uint32_t logical = 0; logical < blocks; logical++) {
        uint32_t physical;
        if (inode_block(&inode, logical, &physical) < 0 || physical == 0 || ext2_read_block(physical, block_buffer) < 0) return;
        uint32_t offset = 0;
        while (offset + 8 <= fs.block_size) {
            ext2_dir_entry_t *entry = (ext2_dir_entry_t *)(block_buffer + offset);
            if (entry->rec_len < 8 || entry->rec_len > fs.block_size - offset || entry->name_len > entry->rec_len - 8) break;
            if (entry->inode != 0) {
                uint32_t length = min_u32(entry->name_len, EXT2_MAX_NAME_LEN);
                for (uint32_t i = 0; i < length; i++) name[i] = entry->name[i];
                name[length] = '\0';
                vga_puts(entry->file_type == EXT2_FT_DIR ? "[DIR]  " : "[FILE] ");
                vga_puts(name);
                vga_puts("\n");
            }
            offset += entry->rec_len;
        }
    }
}

int ext2_create_file(const char *filename) {
    ext2_inode_t directory;
    ext2_inode_t inode;
    uint32_t inode_num;
    int length = name_length(filename);
    if (!fs_initialized || length == 0 || length > EXT2_MAX_NAME_LEN || ext2_find_inode(filename) >= 0 ||
        ext2_read_inode(fs.current_dir_inode, &directory) < 0 ||
        (directory.mode & EXT2_S_IFMT) != EXT2_S_IFDIR) return -1;
    if (allocate_inode(&inode_num) < 0) return -1;
    memset_safe(&inode, 0, sizeof(inode));
    inode.mode = EXT2_S_IFREG | 0644;
    inode.uid = (uint16_t)auth_current_uid();
    inode.links_count = 1;
    if (ext2_write_inode(inode_num, &inode) < 0 ||
        add_directory_entry(&directory, inode_num, filename, EXT2_FT_REG_FILE) < 0 ||
        ext2_write_inode(fs.current_dir_inode, &directory) < 0) return -1;
    return (int)inode_num;
}

int ext2_create_directory(const char *dirname) {
    ext2_inode_t parent;
    ext2_inode_t inode;
    uint32_t inode_num;
    uint32_t data_block;
    int length = name_length(dirname);
    if (!fs_initialized || length == 0 || length > EXT2_MAX_NAME_LEN ||
        ext2_find_inode(dirname) >= 0 || ext2_read_inode(fs.current_dir_inode, &parent) < 0 ||
        (parent.mode & EXT2_S_IFMT) != EXT2_S_IFDIR) return -1;
    if (allocate_inode(&inode_num) < 0 || allocate_block(&data_block) < 0) return -1;

    memset_safe(&inode, 0, sizeof(inode));
    inode.mode = EXT2_S_IFDIR | 0755;
    inode.uid = (uint16_t)auth_current_uid();
    inode.links_count = 2;
    inode.size = fs.block_size;
    inode.blocks = fs.block_size / 512;
    inode.block[0] = data_block;

    memset_safe(block_buffer, 0, fs.block_size);
    ext2_dir_entry_t *self = (ext2_dir_entry_t *)block_buffer;
    self->inode = inode_num;
    self->rec_len = 12;
    self->name_len = 1;
    self->file_type = EXT2_FT_DIR;
    self->name[0] = '.';

    ext2_dir_entry_t *parent_entry = (ext2_dir_entry_t *)(block_buffer + 12);
    parent_entry->inode = fs.current_dir_inode;
    parent_entry->rec_len = (uint16_t)(fs.block_size - 12);
    parent_entry->name_len = 2;
    parent_entry->file_type = EXT2_FT_DIR;
    parent_entry->name[0] = '.';
    parent_entry->name[1] = '.';

    if (ext2_write_block(data_block, block_buffer) < 0 ||
        ext2_write_inode(inode_num, &inode) < 0 ||
        add_directory_entry(&parent, inode_num, dirname, EXT2_FT_DIR) < 0) return -1;
    parent.links_count++;
    if (ext2_write_inode(fs.current_dir_inode, &parent) < 0) return -1;
    return (int)inode_num;
}

int ext2_write_file_by_name(const char *filename, const char *buffer, uint32_t size) {
    int inode = ext2_find_inode(filename);
    if (inode < 0) inode = ext2_create_file(filename);
    return inode < 0 ? -1 : ext2_write_file((uint32_t)inode, buffer, size);
}

int ext2_read_file_by_path(const char *path, char *buffer, uint32_t size) {
    int inode = ext2_find_inode_by_path(path);
    return inode < 0 ? -1 : ext2_read_file((uint32_t)inode, buffer, size);
}

int ext2_read_file_range_by_path(const char *path, uint32_t offset, char *buffer, uint32_t size) {
    int inode = ext2_find_inode_by_path(path);
    return inode < 0 ? -1 : ext2_read_file_range((uint32_t)inode, offset, buffer, size);
}

int ext2_print_dir_by_path(const char *path, void (*vga_puts)(const char*)) {
    int inode = ext2_find_inode_by_path(path);
    if (inode < 0) return -1;
    ext2_print_dir_contents((uint32_t)inode, vga_puts);
    return 0;
}

int ext2_create_file_by_path(const char *path) {
    char parent_path[256];
    char name[EXT2_MAX_NAME_LEN + 1];
    uint32_t saved_dir;
    int parent;
    int result;
    if (split_parent_path(path, parent_path, name) < 0) return -1;
    parent = parent_path[0] == '\0' ? (int)fs.current_dir_inode : ext2_find_inode_by_path(parent_path);
    if (parent < 0) return -1;
    saved_dir = fs.current_dir_inode;
    fs.current_dir_inode = (uint32_t)parent;
    result = ext2_create_file(name);
    fs.current_dir_inode = saved_dir;
    return result;
}

int ext2_create_directory_by_path(const char *path) {
    char parent_path[256];
    char name[EXT2_MAX_NAME_LEN + 1];
    uint32_t saved_dir;
    int parent;
    int result;
    if (split_parent_path(path, parent_path, name) < 0) return -1;
    parent = parent_path[0] == '\0' ? (int)fs.current_dir_inode : ext2_find_inode_by_path(parent_path);
    if (parent < 0) return -1;
    saved_dir = fs.current_dir_inode;
    fs.current_dir_inode = (uint32_t)parent;
    result = ext2_create_directory(name);
    fs.current_dir_inode = saved_dir;
    return result;
}

int ext2_write_file_by_path(const char *path, const char *buffer, uint32_t size) {
    int inode = ext2_find_inode_by_path(path);
    if (inode < 0) inode = ext2_create_file_by_path(path);
    return inode < 0 ? -1 : ext2_write_file((uint32_t)inode, buffer, size);
}

static int free_block(uint32_t block_num) {
    for (uint32_t group = 0; group < fs.group_count; group++) {
        uint32_t first = fs.first_data_block + group * fs.superblock.blocks_per_group;
        uint32_t group_blocks = fs.superblock.blocks_per_group;
        ext2_group_descriptor_t descriptor;
        uint32_t bit;
        if (first >= fs.superblock.blocks_count) continue;
        if (first + group_blocks > fs.superblock.blocks_count) group_blocks = fs.superblock.blocks_count - first;
        if (block_num < first || block_num >= first + group_blocks) continue;
        if (read_group_descriptor(group, &descriptor) < 0 || ext2_read_block(descriptor.block_bitmap, block_buffer) < 0) return -1;
        bit = block_num - first;
        block_buffer[bit / 8] &= (uint8_t)~(1U << (bit % 8));
        if (ext2_write_block(descriptor.block_bitmap, block_buffer) < 0) return -1;
        descriptor.free_blocks_count++;
        fs.superblock.free_blocks_count++;
        return (write_group_descriptor(group, &descriptor) < 0 || write_superblock() < 0) ? -1 : 0;
    }
    return -1;
}

static int free_inode(uint32_t inode_num) {
    uint32_t zero_based = inode_num - 1;
    uint32_t group = zero_based / fs.superblock.inodes_per_group;
    uint32_t index = zero_based % fs.superblock.inodes_per_group;
    ext2_group_descriptor_t descriptor;
    if (read_group_descriptor(group, &descriptor) < 0 || ext2_read_block(descriptor.inode_bitmap, block_buffer) < 0) return -1;
    block_buffer[index / 8] &= (uint8_t)~(1U << (index % 8));
    if (ext2_write_block(descriptor.inode_bitmap, block_buffer) < 0) return -1;
    descriptor.free_inodes_count++;
    fs.superblock.free_inodes_count++;
    return (write_group_descriptor(group, &descriptor) < 0 || write_superblock() < 0) ? -1 : 0;
}

static int remove_directory_entry(ext2_inode_t *directory, const char *name) {
    uint32_t blocks = block_count_for_size(directory->size);
    for (uint32_t logical = 0; logical < blocks; logical++) {
        uint32_t physical;
        uint32_t offset = 0;
        if (inode_block(directory, logical, &physical) < 0 || physical == 0 || ext2_read_block(physical, block_buffer) < 0) return -1;
        while (offset + 8 <= fs.block_size) {
            ext2_dir_entry_t *entry = (ext2_dir_entry_t *)(block_buffer + offset);
            if (entry->rec_len < 8 || entry->rec_len > fs.block_size - offset || entry->name_len > entry->rec_len - 8) break;
            if (entry->inode != 0 && name_equal(entry->name, entry->name_len, name)) {
                entry->inode = 0;
                return ext2_write_block(physical, block_buffer);
            }
            offset += entry->rec_len;
        }
    }
    return -1;
}

int ext2_delete_file_by_path(const char *path) {
    char parent_path[256];
    char name[EXT2_MAX_NAME_LEN + 1];
    ext2_inode_t parent;
    ext2_inode_t file;
    int parent_inode_num;
    int file_inode_num;
    uint32_t blocks;

    if (!fs_initialized || split_parent_path(path, parent_path, name) < 0) return -1;
    parent_inode_num = parent_path[0] == '\0' ? (int)fs.current_dir_inode : ext2_find_inode_by_path(parent_path);
    if (parent_inode_num < 0 || ext2_read_inode((uint32_t)parent_inode_num, &parent) < 0) return -1;

    file_inode_num = ext2_find_inode_in_dir((uint32_t)parent_inode_num, name);
    if (file_inode_num < 0 || ext2_read_inode((uint32_t)file_inode_num, &file) < 0) return -1;
    if ((file.mode & EXT2_S_IFMT) != EXT2_S_IFREG) return -1;

    if (remove_directory_entry(&parent, name) < 0) return -1;

    blocks = block_count_for_size(file.size);
    for (uint32_t logical = 0; logical < blocks; logical++) {
        uint32_t physical;
        if (inode_block(&file, logical, &physical) == 0 && physical != 0) free_block(physical);
    }
    if (file.indirect_block) free_block(file.indirect_block);
    if (file.doubly_indirect_block) {
        uint32_t pointers = fs.block_size / (uint32_t)sizeof(uint32_t);
        uint32_t indirect_pointers[EXT2_MAX_BLOCK_SIZE / sizeof(uint32_t)];
        if (ext2_read_block(file.doubly_indirect_block, block_buffer) == 0) {
            memcpy_safe(indirect_pointers, block_buffer, fs.block_size);
            for (uint32_t index = 0; index < pointers; index++) {
                if (indirect_pointers[index]) free_block(indirect_pointers[index]);
            }
        }
        free_block(file.doubly_indirect_block);
    }

    memset_safe(&file, 0, sizeof(file));
    ext2_write_inode((uint32_t)file_inode_num, &file);
    free_inode((uint32_t)file_inode_num);

    return 0;
}

