#ifndef ARP_H
#define ARP_H

#include <stdint.h>

void arp_handle_packet(const uint8_t *data, uint16_t length);

int arp_resolve(uint32_t ip_address, uint8_t mac_out[6]);
void arp_cache_insert(uint32_t ip_address, const uint8_t mac[6]);
void arp_send_request(uint32_t target_ip);

#endif
