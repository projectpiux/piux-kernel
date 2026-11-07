#include <stdint.h>
#include "eth.h"
#include "arp.h"
#include "ip.h"
#include "net.h"
#include "../drivers/rtl8139.h"

#define ETH_MAX_FRAME 1514

void eth_get_local_mac(uint8_t mac[6]) {
    rtl8139_get_mac(mac);
}

int eth_send(const uint8_t destination_mac[6], uint16_t ethertype, const void *payload, uint16_t length) {
    uint8_t frame[ETH_MAX_FRAME];
    eth_header_t *header = (eth_header_t *)frame;

    if (length > sizeof(frame) - sizeof(eth_header_t)) return -1;
    for (int index = 0; index < 6; index++) header->destination[index] = destination_mac[index];
    eth_get_local_mac(header->source);
    header->ethertype = net_htons(ethertype);
    for (uint16_t index = 0; index < length; index++) frame[sizeof(eth_header_t) + index] = ((const uint8_t *)payload)[index];

    return rtl8139_send(frame, (uint16_t)(sizeof(eth_header_t) + length));
}

void net_poll(void) {
    static uint8_t buffer[ETH_MAX_FRAME];
    int length = rtl8139_receive(buffer, sizeof(buffer));
    eth_header_t *header;
    uint16_t ethertype;

    if (length <= (int)sizeof(eth_header_t)) return;
    header = (eth_header_t *)buffer;
    ethertype = net_htons(header->ethertype);

    if (ethertype == ETH_TYPE_ARP) {
        arp_handle_packet(buffer + sizeof(eth_header_t), (uint16_t)(length - sizeof(eth_header_t)));
    } else if (ethertype == ETH_TYPE_IPV4) {
        ip_handle_packet(buffer + sizeof(eth_header_t), (uint16_t)(length - sizeof(eth_header_t)));
    }
}
