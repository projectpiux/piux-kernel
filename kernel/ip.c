#include <stdint.h>
#include "ip.h"
#include "eth.h"
#include "arp.h"
#include "net.h"
#include "icmp.h"
#include "tcp.h"
#include "udp.h"

#define IP_VERSION_IHL 0x45
#define IP_DEFAULT_TTL 64
#define IP_MAX_PACKET 1500

typedef struct __attribute__((packed)) {
    uint8_t version_ihl;
    uint8_t type_of_service;
    uint16_t total_length;
    uint16_t identification;
    uint16_t flags_fragment;
    uint8_t ttl;
    uint8_t protocol;
    uint16_t header_checksum;
    uint32_t source_ip;
    uint32_t destination_ip;
} ip_header_t;

static uint32_t local_ip = 0x0A00020FU;      
static uint32_t local_netmask = 0xFFFFFF00U; 
static uint32_t local_gateway = 0x0A000202U; 
static uint32_t local_dns = 0x0A000203U;     
static uint16_t next_identification = 1;

void ip_set_config(uint32_t new_ip, uint32_t new_netmask, uint32_t new_gateway) {
    local_ip = new_ip;
    local_netmask = new_netmask;
    local_gateway = new_gateway;
}

void ip_set_dns(uint32_t dns_server) { local_dns = dns_server; }

uint32_t ip_get_local_ip(void) { return local_ip; }
uint32_t ip_get_netmask(void) { return local_netmask; }
uint32_t ip_get_gateway(void) { return local_gateway; }
uint32_t ip_get_dns(void) { return local_dns; }

int ip_send(uint32_t destination_ip, uint8_t protocol, const void *payload, uint16_t length) {
    uint8_t packet[IP_MAX_PACKET];
    ip_header_t *header = (ip_header_t *)packet;
    uint8_t destination_mac[6];
    uint32_t next_hop;

    if (length > sizeof(packet) - sizeof(ip_header_t)) return -1;

    header->version_ihl = IP_VERSION_IHL;
    header->type_of_service = 0;
    header->total_length = net_htons((uint16_t)(sizeof(ip_header_t) + length));
    header->identification = net_htons(next_identification++);
    header->flags_fragment = 0;
    header->ttl = IP_DEFAULT_TTL;
    header->protocol = protocol;
    header->header_checksum = 0;
    header->source_ip = net_htonl(local_ip);
    header->destination_ip = net_htonl(destination_ip);
    header->header_checksum = net_htons(net_checksum(header, sizeof(ip_header_t), 0));

    for (uint16_t index = 0; index < length; index++) packet[sizeof(ip_header_t) + index] = ((const uint8_t *)payload)[index];

    if (destination_ip == 0xFFFFFFFFU) {
        for (int index = 0; index < 6; index++) destination_mac[index] = 0xFF;
    } else {
        next_hop = ((destination_ip ^ local_ip) & local_netmask) == 0 ? destination_ip : local_gateway;
        if (arp_resolve(next_hop, destination_mac) < 0) return -1;
    }

    return eth_send(destination_mac, ETH_TYPE_IPV4, packet, (uint16_t)(sizeof(ip_header_t) + length));
}

void ip_handle_packet(const uint8_t *data, uint16_t length) {
    const ip_header_t *header;
    uint16_t header_length;
    uint16_t total_length;
    uint32_t destination_ip;
    uint32_t source_ip;
    const uint8_t *payload;
    uint16_t payload_length;

    if (length < sizeof(ip_header_t)) return;
    header = (const ip_header_t *)data;
    header_length = (uint16_t)((header->version_ihl & 0x0F) * 4);
    if (header_length < sizeof(ip_header_t) || header_length > length) return;
    total_length = net_htons(header->total_length);
    if (total_length > length) return;

    destination_ip = net_htonl(header->destination_ip);
    if (destination_ip != local_ip && destination_ip != 0xFFFFFFFFU) return;

    source_ip = net_htonl(header->source_ip);
    payload = data + header_length;
    payload_length = (uint16_t)(total_length - header_length);

    if (header->protocol == IP_PROTO_ICMP) icmp_handle_packet(payload, payload_length, source_ip);
    else if (header->protocol == IP_PROTO_TCP) tcp_handle_packet(payload, payload_length, source_ip);
    else if (header->protocol == IP_PROTO_UDP) udp_handle_packet(payload, payload_length, source_ip);
}
