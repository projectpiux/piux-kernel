#include "ata.h"
#include "../core/io.h"

void ata_init(void) {
    outb(ATA_PRIMARY_CTRL, 0x02);
}

static int ata_wait_bsy(void) {
    for (int timeout = 1000000; timeout > 0; timeout--) {
        uint8_t status = inb(ATA_PRIMARY_IO + ATA_REG_STATUS);
        if (status == 0xFF) return -1;
        if (!(status & ATA_SR_BSY)) return 0;
    }
    return -1;
}

static int ata_wait_drq(void) {
    for (int timeout = 1000000; timeout > 0; timeout--) {
        uint8_t status = inb(ATA_PRIMARY_IO + ATA_REG_STATUS);
        if (status == 0xFF) return -1;
        if (status & ATA_SR_ERR) return -1;
        if (status & ATA_SR_DRQ) return 0;
        if (!(status & ATA_SR_BSY) && !(status & ATA_SR_DRQ)) return -1;
    }
    return -1;
}

int ata_read_sectors(uint32_t lba, uint32_t count, uint8_t *buffer) {
    if (count == 0 || count > 255) return -1;
    if (ata_wait_bsy() < 0) return -1;

    outb(ATA_PRIMARY_IO + ATA_REG_HDDEVSEL, 0xE0 | ((lba >> 24) & 0x0F));
    ata_wait_bsy();

    outb(ATA_PRIMARY_IO + ATA_REG_NSECT, count & 0xFF);
    outb(ATA_PRIMARY_IO + ATA_REG_LBAL, (lba & 0xFF));
    outb(ATA_PRIMARY_IO + ATA_REG_LBAM, ((lba >> 8) & 0xFF));
    outb(ATA_PRIMARY_IO + ATA_REG_LBAH, ((lba >> 16) & 0xFF));
    outb(ATA_PRIMARY_IO + ATA_REG_COMMAND, ATA_CMD_READ_PIO);

    for (uint32_t i = 0; i < count; i++) {
        if (ata_wait_drq() < 0) return -1;

        for (int j = 0; j < 256; j++) {
            uint16_t data = inw(ATA_PRIMARY_IO + ATA_REG_DATA);
            buffer[i * 512 + j * 2] = data & 0xFF;
            buffer[i * 512 + j * 2 + 1] = (data >> 8) & 0xFF;
        }

        if (ata_wait_bsy() < 0) return -1;
    }
    return 0;
}

int ata_write_sectors(uint32_t lba, uint32_t count, uint8_t *buffer) {
    if (ata_wait_bsy() < 0) return -1;

    outb(ATA_PRIMARY_IO + ATA_REG_HDDEVSEL, 0xE0 | ((lba >> 24) & 0x0F));
    ata_wait_bsy();

    outb(ATA_PRIMARY_IO + ATA_REG_NSECT, count & 0xFF);
    outb(ATA_PRIMARY_IO + ATA_REG_LBAL, (lba & 0xFF));
    outb(ATA_PRIMARY_IO + ATA_REG_LBAM, ((lba >> 8) & 0xFF));
    outb(ATA_PRIMARY_IO + ATA_REG_LBAH, ((lba >> 16) & 0xFF));
    outb(ATA_PRIMARY_IO + ATA_REG_COMMAND, ATA_CMD_WRITE_PIO);

    for (uint32_t i = 0; i < count; i++) {
        if (ata_wait_drq() < 0) return -1;

        for (int j = 0; j < 256; j++) {
            uint16_t data = (buffer[i * 512 + j * 2 + 1] << 8) | buffer[i * 512 + j * 2];
            outw(ATA_PRIMARY_IO + ATA_REG_DATA, data);
        }

        if (ata_wait_bsy() < 0) return -1;
    }

    outb(ATA_PRIMARY_IO + ATA_REG_COMMAND, ATA_CMD_CACHE_FLUSH);
    ata_wait_bsy();
    return 0;
}