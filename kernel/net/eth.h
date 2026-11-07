#ifndef ETH_H
#define ETH_H

#include <stdint.h>

#define ETH_TYPE_ARP  0x0806
#define ETH_TYPE_IPV4 0x0800

typedef struct __attribute__((packed)) {
    uint8_t destination[6];
    uint8_t source[6];
    uint16_t ethertype;
} eth_header_t;

void eth_get_local_mac(uint8_t mac[6]);
int eth_send(const uint8_t destination_mac[6], uint16_t ethertype, const void *payload, uint16_t length);

void net_poll(void);

#endif
