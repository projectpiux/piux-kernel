#ifndef IP_H
#define IP_H

#include <stdint.h>

#define IP_PROTO_ICMP 1
#define IP_PROTO_TCP  6
#define IP_PROTO_UDP  17

void ip_set_config(uint32_t local_ip, uint32_t netmask, uint32_t gateway);
void ip_set_dns(uint32_t dns_server);
uint32_t ip_get_local_ip(void);
uint32_t ip_get_netmask(void);
uint32_t ip_get_gateway(void);
uint32_t ip_get_dns(void);
int ip_send(uint32_t destination_ip, uint8_t protocol, const void *payload, uint16_t length);
void ip_handle_packet(const uint8_t *data, uint16_t length);

#endif
