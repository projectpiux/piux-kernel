#include <stdint.h>
#include "arp.h"
#include "eth.h"
#include "ip.h"
#include "net.h"

#define ARP_HTYPE_ETHERNET 1
#define ARP_PTYPE_IPV4 0x0800
#define ARP_OP_REQUEST 1
#define ARP_OP_REPLY 2
#define ARP_CACHE_SIZE 8
#define ARP_RESOLVE_ATTEMPTS 200000

typedef struct __attribute__((packed)) {
    uint16_t hardware_type;
    uint16_t protocol_type;
    uint8_t hardware_length;
    uint8_t protocol_length;
    uint16_t operation;
    uint8_t sender_mac[6];
    uint32_t sender_ip;
    uint8_t target_mac[6];
    uint32_t target_ip;
} arp_packet_t;

typedef struct {
    uint32_t ip_address;
    uint8_t mac[6];
    int used;
} arp_cache_entry_t;

static arp_cache_entry_t cache[ARP_CACHE_SIZE];
static int next_cache_slot;

void arp_cache_insert(uint32_t ip_address, const uint8_t mac[6]) {
    for (int index = 0; index < ARP_CACHE_SIZE; index++) {
        if (cache[index].used && cache[index].ip_address == ip_address) {
            for (int byte = 0; byte < 6; byte++) cache[index].mac[byte] = mac[byte];
            return;
        }
    }
    cache[next_cache_slot].used = 1;
    cache[next_cache_slot].ip_address = ip_address;
    for (int byte = 0; byte < 6; byte++) cache[next_cache_slot].mac[byte] = mac[byte];
    next_cache_slot = (next_cache_slot + 1) % ARP_CACHE_SIZE;
}

static int arp_cache_lookup(uint32_t ip_address, uint8_t mac_out[6]) {
    for (int index = 0; index < ARP_CACHE_SIZE; index++) {
        if (cache[index].used && cache[index].ip_address == ip_address) {
            for (int byte = 0; byte < 6; byte++) mac_out[byte] = cache[index].mac[byte];
            return 0;
        }
    }
    return -1;
}

void arp_send_request(uint32_t target_ip) {
    arp_packet_t packet;
    uint8_t broadcast[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
    uint8_t local_mac[6];

    eth_get_local_mac(local_mac);
    packet.hardware_type = net_htons(ARP_HTYPE_ETHERNET);
    packet.protocol_type = net_htons(ARP_PTYPE_IPV4);
    packet.hardware_length = 6;
    packet.protocol_length = 4;
    packet.operation = net_htons(ARP_OP_REQUEST);
    for (int byte = 0; byte < 6; byte++) packet.sender_mac[byte] = local_mac[byte];
    packet.sender_ip = net_htonl(ip_get_local_ip());
    for (int byte = 0; byte < 6; byte++) packet.target_mac[byte] = 0;
    packet.target_ip = net_htonl(target_ip);

    eth_send(broadcast, ETH_TYPE_ARP, &packet, sizeof(packet));
}

static void arp_send_reply(const arp_packet_t *request) {
    arp_packet_t reply;
    uint8_t local_mac[6];

    eth_get_local_mac(local_mac);
    reply.hardware_type = net_htons(ARP_HTYPE_ETHERNET);
    reply.protocol_type = net_htons(ARP_PTYPE_IPV4);
    reply.hardware_length = 6;
    reply.protocol_length = 4;
    reply.operation = net_htons(ARP_OP_REPLY);
    for (int byte = 0; byte < 6; byte++) reply.sender_mac[byte] = local_mac[byte];
    reply.sender_ip = net_htonl(ip_get_local_ip());
    for (int byte = 0; byte < 6; byte++) reply.target_mac[byte] = request->sender_mac[byte];
    reply.target_ip = request->sender_ip;

    eth_send(request->sender_mac, ETH_TYPE_ARP, &reply, sizeof(reply));
}

void arp_handle_packet(const uint8_t *data, uint16_t length) {
    const arp_packet_t *packet;
    uint16_t operation;
    uint32_t sender_ip;

    if (length < sizeof(arp_packet_t)) return;
    packet = (const arp_packet_t *)data;
    operation = net_htons(packet->operation);
    sender_ip = net_htonl(packet->sender_ip);

    arp_cache_insert(sender_ip, packet->sender_mac);

    if (operation == ARP_OP_REQUEST && net_htonl(packet->target_ip) == ip_get_local_ip()) {
        arp_send_reply(packet);
    }
}

int arp_resolve(uint32_t ip_address, uint8_t mac_out[6]) {
    if (arp_cache_lookup(ip_address, mac_out) == 0) return 0;
    arp_send_request(ip_address);
    for (int attempt = 0; attempt < ARP_RESOLVE_ATTEMPTS; attempt++) {
        net_poll();
        if (arp_cache_lookup(ip_address, mac_out) == 0) return 0;
    }
    return -1;
}
