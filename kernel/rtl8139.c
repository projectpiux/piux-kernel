#include <stdint.h>
#include "rtl8139.h"
#include "pci.h"
#include "io.h"

#define RTL8139_VENDOR_ID 0x10EC
#define RTL8139_DEVICE_ID 0x8139

#define REG_MAC0    0x00
#define REG_CONFIG1 0x52
#define REG_CMD     0x37
#define REG_RBSTART 0x30
#define REG_CAPR    0x38
#define REG_IMR     0x3C
#define REG_RCR     0x44
#define REG_TSAD0   0x20
#define REG_TSD0    0x10

#define CMD_RESET      0x10
#define CMD_RX_ENABLE  0x08
#define CMD_TX_ENABLE  0x04
#define CMD_BUFE       0x01
#define TSD_TOK        0x8000

#define RX_BUFFER_SIZE (8192 + 16 + 1500)
#define TX_DESCRIPTOR_COUNT 4
#define TX_BUFFER_SIZE 1536

static uint16_t io_base;
static int ready;
static uint8_t mac_address[6];
static uint32_t rx_read_offset;
static int current_tx_descriptor;

static uint8_t rx_buffer[RX_BUFFER_SIZE] __attribute__((aligned(4)));
static uint8_t tx_buffers[TX_DESCRIPTOR_COUNT][TX_BUFFER_SIZE] __attribute__((aligned(4)));

int rtl8139_init(void) {
    pci_device_t device;
    uint32_t bar0;

    ready = 0;
    if (pci_find_device(RTL8139_VENDOR_ID, RTL8139_DEVICE_ID, &device) < 0) return -1;
    pci_enable_bus_mastering(&device);

    bar0 = pci_bar_address(&device, 0);
    io_base = (uint16_t)bar0;

    outb(io_base + REG_CONFIG1, 0x00);
    outb(io_base + REG_CMD, CMD_RESET);
    for (int timeout = 1000000; timeout > 0; timeout--) {
        if (!(inb(io_base + REG_CMD) & CMD_RESET)) break;
    }

    for (int index = 0; index < 6; index++) mac_address[index] = inb(io_base + REG_MAC0 + index);

    outl(io_base + REG_RBSTART, (uint32_t)(uintptr_t)rx_buffer);
    outw(io_base + REG_IMR, 0x0000); 
    outl(io_base + REG_RCR, 0x0000000F);
    outb(io_base + REG_CMD, CMD_RX_ENABLE | CMD_TX_ENABLE);

    rx_read_offset = 0;
    current_tx_descriptor = 0;
    ready = 1;
    return 0;
}

int rtl8139_is_ready(void) { return ready; }

void rtl8139_get_mac(uint8_t mac[6]) {
    for (int index = 0; index < 6; index++) mac[index] = mac_address[index];
}

int rtl8139_send(const void *data, uint16_t length) {
    uint8_t *destination;
    if (!ready || length == 0 || length > TX_BUFFER_SIZE) return -1;

    destination = tx_buffers[current_tx_descriptor];
    for (uint16_t index = 0; index < length; index++) destination[index] = ((const uint8_t *)data)[index];

    outl(io_base + REG_TSAD0 + (uint32_t)current_tx_descriptor * 4, (uint32_t)(uintptr_t)destination);
    outl(io_base + REG_TSD0 + (uint32_t)current_tx_descriptor * 4, length);

    for (int timeout = 1000000; timeout > 0; timeout--) {
        if (inl(io_base + REG_TSD0 + (uint32_t)current_tx_descriptor * 4) & TSD_TOK) break;
    }

    current_tx_descriptor = (current_tx_descriptor + 1) % TX_DESCRIPTOR_COUNT;
    return 0;
}

int rtl8139_receive(void *buffer, uint16_t max_length) {
    uint8_t *packet;
    uint16_t length;
    uint16_t copy_length;

    if (!ready) return -1;
    if (inb(io_base + REG_CMD) & CMD_BUFE) return 0;

    packet = rx_buffer + rx_read_offset;
    length = (uint16_t)(packet[2] | (packet[3] << 8));

    copy_length = (uint16_t)(length - 4);
    if (copy_length > max_length) copy_length = max_length;
    for (uint16_t index = 0; index < copy_length; index++) ((uint8_t *)buffer)[index] = packet[4 + index];

    rx_read_offset = (rx_read_offset + length + 4 + 3) & ~3U;
    if (rx_read_offset >= RX_BUFFER_SIZE) rx_read_offset -= RX_BUFFER_SIZE;
    outw(io_base + REG_CAPR, (uint16_t)(rx_read_offset - 16));

    return copy_length;
}
