#ifndef EXT2_H
#define EXT2_H

#include <stdint.h>

#define EXT2_BLOCK_SIZE 1024
#define EXT2_MAX_BLOCK_SIZE 4096
#define EXT2_INODE_SIZE 128

#define EXT2_MAGIC 0xEF53
#define EXT2_STATE_CLEAN 1
#define EXT2_STATE_ERROR 2

#define EXT2_FT_REG_FILE 1
#define EXT2_FT_DIR 2
#define EXT2_S_IFMT 0xF000
#define EXT2_S_IFREG 0x8000
#define EXT2_S_IFDIR 0x4000

#define EXT2_FEATURE_INCOMPAT_FILETYPE 0x0002
#define EXT2_FEATURE_INCOMPAT_UNSUPPORTED 0xFFFFFFFD
#define EXT2_MAX_NAME_LEN 255

typedef struct {
    uint32_t inodes_count;
    uint32_t blocks_count;
    uint32_t reserved_blocks_count;
    uint32_t free_blocks_count;
    uint32_t free_inodes_count;
    uint32_t first_data_block;
    uint32_t log_block_size;
    uint32_t log_frag_size;
    uint32_t blocks_per_group;
    uint32_t frags_per_group;
    uint32_t inodes_per_group;
    uint32_t mtime;
    uint32_t wtime;
    uint16_t mnt_count;
    uint16_t max_mnt_count;
    uint16_t magic;
    uint16_t state;
    uint16_t errors;
    uint16_t minor_rev_level;
    uint32_t lastcheck;
    uint32_t checkinterval;
    uint32_t creator_os;
    uint32_t rev_level;
    uint16_t def_resuid;
    uint16_t def_resgid;
    uint32_t first_ino;
    uint16_t inode_size;
    uint16_t block_group_nr;
    uint32_t feature_compat;
    uint32_t feature_incompat;
    uint32_t feature_ro_compat;
    uint8_t uuid[16];
    uint8_t volume_name[16];
    uint8_t last_mounted[64];
    uint32_t algorithm_usage_bitmap;
    uint8_t prealloc_blocks;
    uint8_t prealloc_dir_blocks;
    uint16_t reserved_pad;
    uint32_t reserved[204];
} ext2_superblock_t;

typedef struct {
    uint32_t block_bitmap;
    uint32_t inode_bitmap;
    uint32_t inode_table;
    uint16_t free_blocks_count;
    uint16_t free_inodes_count;
    uint16_t used_dirs_count;
    uint16_t reserved;
    uint32_t reserved2[3];
} ext2_group_descriptor_t;

typedef struct {
    uint16_t mode;
    uint16_t uid;
    uint32_t size;
    uint32_t atime;
    uint32_t ctime;
    uint32_t mtime;
    uint32_t dtime;
    uint16_t gid;
    uint16_t links_count;
    uint32_t blocks;
    uint32_t flags;
    uint32_t osd1;
    uint32_t block[12];
    uint32_t indirect_block;
    uint32_t doubly_indirect_block;
    uint32_t triply_indirect_block;
    uint32_t generation;
    uint32_t file_acl;
    uint32_t dir_acl;
    uint32_t faddr;
    uint32_t osd2[3];
} ext2_inode_t;

typedef struct {
    uint32_t inode;
    uint16_t rec_len;
    uint8_t name_len;
    uint8_t file_type;
    char name[256];
} ext2_dir_entry_t;

typedef struct {
    ext2_superblock_t superblock;
    ext2_group_descriptor_t *group_descriptors;
    uint32_t block_size;
    uint32_t inode_size;
    uint32_t current_dir_inode;
    uint32_t group_count;
    uint32_t first_data_block;
    char current_path[256];
} ext2_filesystem_t;

int ext2_mount(void);
int ext2_is_mounted(void);
int ext2_read_block(uint32_t block_num, uint8_t *buffer);
int ext2_write_block(uint32_t block_num, uint8_t *buffer);
int ext2_read_inode(uint32_t inode_num, ext2_inode_t *inode);
int ext2_find_inode(const char *filename);
int ext2_find_inode_in_dir(uint32_t dir_inode, const char *filename);
int ext2_find_inode_by_path(const char *path);
int ext2_read_file(uint32_t inode_num, char *buffer, uint32_t size);
int ext2_read_file_range(uint32_t inode_num, uint32_t offset, char *buffer, uint32_t size);
int ext2_read_file_by_name(const char *filename, char *buffer, uint32_t size);
int ext2_read_file_range_by_path(const char *path, uint32_t offset, char *buffer, uint32_t size);
int ext2_read_file_by_path(const char *path, char *buffer, uint32_t size);
void ext2_print_dir_contents(uint32_t dir_inode, void (*vga_puts)(const char*));
int ext2_print_dir_by_path(const char *path, void (*vga_puts)(const char*));
int ext2_create_file(const char *filename);
int ext2_create_file_by_path(const char *path);
int ext2_create_directory(const char *dirname);
int ext2_create_directory_by_path(const char *path);
int ext2_write_file(uint32_t inode_num, const char *buffer, uint32_t size);
int ext2_write_file_by_name(const char *filename, const char *buffer, uint32_t size);
int ext2_write_file_by_path(const char *path, const char *buffer, uint32_t size);
int ext2_delete_file_by_path(const char *path);

#endif
