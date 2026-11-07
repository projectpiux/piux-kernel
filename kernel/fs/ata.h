#ifndef ATA_H
#define ATA_H

#include <stdint.h>

#define ATA_PRIMARY_IO      0x1F0
#define ATA_PRIMARY_CTRL    0x3F6

#define ATA_REG_DATA        0x00
#define ATA_REG_ERR         0x01
#define ATA_REG_NSECT       0x02
#define ATA_REG_LBAL        0x03
#define ATA_REG_LBAM        0x04
#define ATA_REG_LBAH        0x05
#define ATA_REG_HDDEVSEL    0x06
#define ATA_REG_COMMAND     0x07
#define ATA_REG_STATUS      0x07

#define ATA_SR_BSY          0x80
#define ATA_SR_DRDY         0x40
#define ATA_SR_DF           0x20
#define ATA_SR_DSC          0x10
#define ATA_SR_SEEK         0x10
#define ATA_SR_DRQ          0x08
#define ATA_SR_CORR         0x04
#define ATA_SR_IDX          0x04
#define ATA_SR_ERR          0x01

#define ATA_CMD_READ_PIO    0x20
#define ATA_CMD_WRITE_PIO   0x30
#define ATA_CMD_CACHE_FLUSH 0xE7

void ata_init(void);
int  ata_read_sectors(uint32_t lba, uint32_t count, uint8_t *buffer);
int  ata_write_sectors(uint32_t lba, uint32_t count, uint8_t *buffer);

#endif