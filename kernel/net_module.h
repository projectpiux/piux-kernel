#ifndef NET_MODULE_H
#define NET_MODULE_H

#include <stdint.h>

#if defined(PIUX_MODULE_NET)

#include "piux_module.h"

int net_module_init(void);
uint16_t net_htons(uint16_t value);
uint32_t net_htonl(uint32_t value);
uint16_t net_checksum(const void *data, uint32_t length, uint32_t initial_sum);
uint32_t net_parse_ip(const char *text);
int net_resolve_host(const char *text, uint32_t *out_ip);
void net_format_ip(uint32_t address, char *buffer);

#else

uint16_t net_htons(uint16_t value);
uint32_t net_htonl(uint32_t value);
uint16_t net_checksum(const void *data, uint32_t length, uint32_t initial_sum);
uint32_t net_parse_ip(const char *text);
int net_resolve_host(const char *text, uint32_t *out_ip);
void net_format_ip(uint32_t address, char *buffer);

#endif

#endif
