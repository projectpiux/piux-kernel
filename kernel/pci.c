#include <stdint.h>
#include "pci.h"
#include "io.h"

#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA    0xCFC

static uint32_t pci_address(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset) {
    return 0x80000000U | ((uint32_t)bus << 16) | ((uint32_t)slot << 11) |
           ((uint32_t)function << 8) | (offset & 0xFC);
}

static uint32_t pci_read_raw(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset) {
    outl(PCI_CONFIG_ADDRESS, pci_address(bus, slot, function, offset));
    return inl(PCI_CONFIG_DATA);
}

static void pci_write_raw(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset, uint32_t value) {
    outl(PCI_CONFIG_ADDRESS, pci_address(bus, slot, function, offset));
    outl(PCI_CONFIG_DATA, value);
}

int pci_find_device(uint16_t vendor_id, uint16_t device_id, pci_device_t *device) {
    for (uint32_t bus = 0; bus < 256; bus++) {
        for (uint32_t slot = 0; slot < 32; slot++) {
            for (uint32_t function = 0; function < 8; function++) {
                uint32_t id = pci_read_raw((uint8_t)bus, (uint8_t)slot, (uint8_t)function, 0x00);
                uint16_t vendor = (uint16_t)(id & 0xFFFF);
                uint16_t dev = (uint16_t)((id >> 16) & 0xFFFF);
                if (vendor == 0xFFFF) continue;
                if (vendor == vendor_id && dev == device_id) {
                    device->bus = (uint8_t)bus;
                    device->slot = (uint8_t)slot;
                    device->function = (uint8_t)function;
                    return 0;
                }
            }
        }
    }
    return -1;
}

uint32_t pci_config_read32(const pci_device_t *device, uint8_t offset) {
    return pci_read_raw(device->bus, device->slot, device->function, offset);
}

void pci_config_write32(const pci_device_t *device, uint8_t offset, uint32_t value) {
    pci_write_raw(device->bus, device->slot, device->function, offset, value);
}

uint32_t pci_bar_address(const pci_device_t *device, int bar_index) {
    uint32_t bar = pci_config_read32(device, (uint8_t)(0x10 + bar_index * 4));
    if (bar & 0x01) return bar & 0xFFFFFFFC;
    return bar & 0xFFFFFFF0;
}

void pci_enable_bus_mastering(const pci_device_t *device) {
    uint32_t command = pci_config_read32(device, 0x04);
    command |= 0x0005; 
    pci_config_write32(device, 0x04, command);
}
