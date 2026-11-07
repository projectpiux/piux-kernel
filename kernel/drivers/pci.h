#ifndef PCI_H
#define PCI_H

#include <stdint.h>

typedef struct {
    uint8_t bus;
    uint8_t slot;
    uint8_t function;
} pci_device_t;

int pci_find_device(uint16_t vendor_id, uint16_t device_id, pci_device_t *device);
uint32_t pci_config_read32(const pci_device_t *device, uint8_t offset);
void pci_config_write32(const pci_device_t *device, uint8_t offset, uint32_t value);
uint32_t pci_bar_address(const pci_device_t *device, int bar_index);
void pci_enable_bus_mastering(const pci_device_t *device);

#endif
