#include <stdint.h>
#include "module.h"
#include "piux_module.h"
#include "memory.h"
#include "ext2.h"
extern ext2_filesystem_t fs;
#include "ramfs.h"
#include "ramfs.h"
#include "ata.h"
#include "keyboard.h"
#include "auth.h"
#include "video.h"
#include "system.h"
#include "shell.h"
#include "timer.h"
#include "process.h"
#include "elf.h"
#include "http.h"
#include "net.h"
#include "ip.h"
#include "arp.h"
#include "icmp.h"
#include "udp.h"
#include "rtl8139.h"
#include "vfs_core.h"
#include "vfs_module.h"

extern char _os_infos_start[];
extern char _os_infos_end[];

static const char *const core_symbol_names[] = {

    "kmalloc", "kfree", "memory_range_valid", "memory_user_range_valid",
    "memory_address_space_create", "memory_address_space_activate",
    "memory_address_space_destroy", "memory_user_map", "memory_user_protect",
    "paging_enabled",

    "timer_ticks",


    "ext2_mount", "ext2_is_mounted", "ext2_read_block", "ext2_write_block",
    "ext2_read_inode", "ext2_find_inode", "ext2_find_inode_in_dir",
    "ext2_find_inode_by_path", "ext2_read_file", "ext2_read_file_range",
    "ext2_read_file_by_name", "ext2_read_file_range_by_path",
    "ext2_read_file_by_path", "ext2_print_dir_contents", "ext2_print_dir_by_path",
    "ext2_create_file", "ext2_create_file_by_path", "ext2_create_directory",
    "ext2_create_directory_by_path", "ext2_write_file", "ext2_write_file_by_name",
    "ext2_write_file_by_path", "ext2_delete_file_by_path",

    "ramfs_init", "ramfs_create_file", "ramfs_write_file", "ramfs_read_file",
    "ramfs_find_file", "ramfs_list_files",

    "ata_init", "ata_read_sectors", "ata_write_sectors",

    "keyboard_read_char", "keyboard_try_read_char", "keyboard_is_shift_pressed",
    "keyboard_is_ctrl_pressed", "keyboard_is_alt_pressed",
    "keyboard_get_last_scancode", "keyboard_get_layout", "keyboard_set_layout",

    "auth_create_user", "auth_login", "auth_current_username",
    "auth_current_uid", "auth_current_user_is_sudoer",

    "video_clear", "video_scroll", "video_put_cell", "video_put_pixel",
    "video_capture_region", "video_restore_region", "video_begin_frame",
    "video_begin_region_frame", "video_present", "video_present_region",
    "video_fill_rect", "video_copy_rect", "video_blit", "video_framebuffer_ready",
    "video_columns", "video_rows", "video_pixel_width", "video_pixel_height",

    "vga_puts", "vga_putc", "vga_clear",

    "process_exec", "process_getpid", "process_getuid", "process_yield",
    "process_create", "process_schedule_once",

    "net_htons", "net_htonl", "net_checksum",
    "net_parse_ip", "net_resolve_host", "net_format_ip",
    "rtl8139_init", "rtl8139_send", "rtl8139_receive", "rtl8139_get_mac",
    "rtl8139_is_ready",
    "ip_get_local_ip", "ip_get_netmask", "ip_get_gateway", "ip_get_dns",
    "ip_set_config", "ip_set_dns", "ip_send", "ip_handle_packet",
    "arp_resolve", "icmp_send_echo_request", "icmp_handle_packet",
    "icmp_wait_echo_reply",
    "udp_send", "udp_last_received",

    "vfs_open", "vfs_read", "vfs_write", "vfs_close", "vfs_seek",
    "vfs_node_type", "vfs_set_console_output",
    "vfs_open_dispatch", "vfs_read_dispatch", "vfs_write_dispatch",
    "vfs_close_dispatch", "vfs_seek_dispatch", "vfs_node_type_dispatch",
    "vfs_console_output_dispatch", "vfs_module_ready",

    "fs",

    "_os_infos_start", "_os_infos_end",

    "http_get_to_fd", "https_get_to_fd", "elf_load",
    "system_memory_total_kb", "system_memory_used_kb", "system_memory_free_kb"
};

static const void *const core_symbol_addresses[] = {
    PIUX_EXPORT(kmalloc), PIUX_EXPORT(kfree),
    PIUX_EXPORT(memory_range_valid), PIUX_EXPORT(memory_user_range_valid),
    PIUX_EXPORT(memory_address_space_create), PIUX_EXPORT(memory_address_space_activate),
    PIUX_EXPORT(memory_address_space_destroy), PIUX_EXPORT(memory_user_map),
    PIUX_EXPORT(memory_user_protect), PIUX_EXPORT(paging_enabled),
    PIUX_EXPORT(timer_ticks),

    PIUX_EXPORT(ext2_mount), PIUX_EXPORT(ext2_is_mounted),
    PIUX_EXPORT(ext2_read_block), PIUX_EXPORT(ext2_write_block),
    PIUX_EXPORT(ext2_read_inode), PIUX_EXPORT(ext2_find_inode),
    PIUX_EXPORT(ext2_find_inode_in_dir), PIUX_EXPORT(ext2_find_inode_by_path),
    PIUX_EXPORT(ext2_read_file), PIUX_EXPORT(ext2_read_file_range),
    PIUX_EXPORT(ext2_read_file_by_name), PIUX_EXPORT(ext2_read_file_range_by_path),
    PIUX_EXPORT(ext2_read_file_by_path), PIUX_EXPORT(ext2_print_dir_contents),
    PIUX_EXPORT(ext2_print_dir_by_path), PIUX_EXPORT(ext2_create_file),
    PIUX_EXPORT(ext2_create_file_by_path), PIUX_EXPORT(ext2_create_directory),
    PIUX_EXPORT(ext2_create_directory_by_path), PIUX_EXPORT(ext2_write_file),
    PIUX_EXPORT(ext2_write_file_by_name), PIUX_EXPORT(ext2_write_file_by_path),
    PIUX_EXPORT(ext2_delete_file_by_path),
    PIUX_EXPORT(ramfs_init), PIUX_EXPORT(ramfs_create_file),
    PIUX_EXPORT(ramfs_write_file), PIUX_EXPORT(ramfs_read_file),
    PIUX_EXPORT(ramfs_find_file), PIUX_EXPORT(ramfs_list_files),
    PIUX_EXPORT(ata_init), PIUX_EXPORT(ata_read_sectors), PIUX_EXPORT(ata_write_sectors),
    PIUX_EXPORT(keyboard_read_char), PIUX_EXPORT(keyboard_try_read_char),
    PIUX_EXPORT(keyboard_is_shift_pressed), PIUX_EXPORT(keyboard_is_ctrl_pressed),
    PIUX_EXPORT(keyboard_is_alt_pressed), PIUX_EXPORT(keyboard_get_last_scancode),
    PIUX_EXPORT(keyboard_get_layout), PIUX_EXPORT(keyboard_set_layout),
    PIUX_EXPORT(auth_create_user), PIUX_EXPORT(auth_login),
    PIUX_EXPORT(auth_current_username), PIUX_EXPORT(auth_current_uid),
    PIUX_EXPORT(auth_current_user_is_sudoer),
    PIUX_EXPORT(video_clear), PIUX_EXPORT(video_scroll), PIUX_EXPORT(video_put_cell),
    PIUX_EXPORT(video_put_pixel), PIUX_EXPORT(video_capture_region),
    PIUX_EXPORT(video_restore_region), PIUX_EXPORT(video_begin_frame),
    PIUX_EXPORT(video_begin_region_frame), PIUX_EXPORT(video_present),
    PIUX_EXPORT(video_present_region), PIUX_EXPORT(video_fill_rect),
    PIUX_EXPORT(video_copy_rect), PIUX_EXPORT(video_blit),
    PIUX_EXPORT(&video_framebuffer_ready), PIUX_EXPORT(&video_columns),
    PIUX_EXPORT(&video_rows), PIUX_EXPORT(&video_pixel_width),
    PIUX_EXPORT(&video_pixel_height),
    PIUX_EXPORT(vga_puts), PIUX_EXPORT(vga_putc), PIUX_EXPORT(vga_clear),
    PIUX_EXPORT(process_exec), PIUX_EXPORT(process_getpid),
    PIUX_EXPORT(process_getuid), PIUX_EXPORT(process_yield),
    PIUX_EXPORT(process_create), PIUX_EXPORT(process_schedule_once),
    PIUX_EXPORT(net_htons), PIUX_EXPORT(net_htonl), PIUX_EXPORT(net_checksum),
    PIUX_EXPORT(net_parse_ip), PIUX_EXPORT(net_resolve_host),
    PIUX_EXPORT(net_format_ip),
    PIUX_EXPORT(rtl8139_init), PIUX_EXPORT(rtl8139_send),
    PIUX_EXPORT(rtl8139_receive), PIUX_EXPORT(rtl8139_get_mac),
    PIUX_EXPORT(rtl8139_is_ready),
    PIUX_EXPORT(ip_get_local_ip), PIUX_EXPORT(ip_get_netmask),
    PIUX_EXPORT(ip_get_gateway), PIUX_EXPORT(ip_get_dns),
    PIUX_EXPORT(ip_set_config), PIUX_EXPORT(ip_set_dns),
    PIUX_EXPORT(ip_send), PIUX_EXPORT(ip_handle_packet),
    PIUX_EXPORT(arp_resolve),
    PIUX_EXPORT(icmp_send_echo_request), PIUX_EXPORT(icmp_handle_packet),
    PIUX_EXPORT(icmp_wait_echo_reply),
    PIUX_EXPORT(udp_send), PIUX_EXPORT(udp_last_received),
    PIUX_EXPORT(vfs_open), PIUX_EXPORT(vfs_read), PIUX_EXPORT(vfs_write),
    PIUX_EXPORT(vfs_close), PIUX_EXPORT(vfs_seek), PIUX_EXPORT(vfs_node_type),
    PIUX_EXPORT(vfs_set_console_output),
    PIUX_EXPORT(&vfs_open_dispatch), PIUX_EXPORT(&vfs_read_dispatch),
    PIUX_EXPORT(&vfs_write_dispatch), PIUX_EXPORT(&vfs_close_dispatch),
    PIUX_EXPORT(&vfs_seek_dispatch), PIUX_EXPORT(&vfs_node_type_dispatch),
    PIUX_EXPORT(&vfs_console_output_dispatch), PIUX_EXPORT(&vfs_module_ready),
    PIUX_EXPORT(&fs),
    PIUX_EXPORT(&_os_infos_start), PIUX_EXPORT(&_os_infos_end),
    PIUX_EXPORT(http_get_to_fd), PIUX_EXPORT(https_get_to_fd),
    PIUX_EXPORT(elf_load),
    PIUX_EXPORT(&system_memory_total_kb), PIUX_EXPORT(&system_memory_used_kb),
    PIUX_EXPORT(&system_memory_free_kb)
};

void core_symbols_init(void) {
    module_register_core_symbols(core_symbol_names, core_symbol_addresses,
                                 sizeof(core_symbol_names) / sizeof(core_symbol_names[0]));
}
