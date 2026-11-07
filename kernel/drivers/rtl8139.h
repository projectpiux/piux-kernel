#ifndef RTL8139_H
#define RTL8139_H

#include <stdint.h>

int rtl8139_init(void);
int rtl8139_is_ready(void);
void rtl8139_get_mac(uint8_t mac[6]);
int rtl8139_send(const void *data, uint16_t length);
int rtl8139_receive(void *buffer, uint16_t max_length);

#endif
