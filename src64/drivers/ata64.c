/*
 * ata64.c -- ATA PIO 전송 계층
 *
 * 1차 채널, 마스터 드라이브, LBA28, 명령 하나에 섹터 하나. AHCI 컨트롤러가
 * 없을 때 쓰는 대비책이다. 느리지만 어느 기기에서나 뜬다.
 */
#include <asmfunc64.h>
#include <block64.h>
#include <stddef.h>
#include <stdint.h>

#define ATA_DATA      0x1f0
#define ATA_SECCOUNT  0x1f2
#define ATA_LBA_LOW   0x1f3
#define ATA_LBA_MID   0x1f4
#define ATA_LBA_HIGH  0x1f5
#define ATA_DRIVE     0x1f6
#define ATA_STATUS    0x1f7
#define ATA_COMMAND   0x1f7

#define ATA_STATUS_ERR 0x01
#define ATA_STATUS_DRQ 0x08
#define ATA_STATUS_BSY 0x80
#define ATA_CMD_READ     0x20
#define ATA_CMD_WRITE    0x30
#define ATA_CMD_IDENTIFY 0xec
#define ATA_CMD_FLUSH    0xe7

static uint64_t sector_count;
static int identified;

static int ata_wait_not_busy(void)
{
	uint32_t timeout;

	for (timeout = 0; timeout < 1000000; timeout++) {
		if ((io_in8(ATA_STATUS) & ATA_STATUS_BSY) == 0) {
			return 0;
		}
	}
	return -1;
}

static int ata_wait_drq(void)
{
	uint32_t timeout;
	uint8_t status;

	for (timeout = 0; timeout < 1000000; timeout++) {
		status = io_in8(ATA_STATUS);
		if ((status & ATA_STATUS_ERR) != 0) {
			return -1;
		}
		if ((status & ATA_STATUS_DRQ) != 0) {
			return 0;
		}
	}
	return -1;
}

static uint16_t ata_in16(void)
{
	uint16_t value;

	__asm__ volatile ("inw %1, %0" : "=a" (value) : "Nd" ((uint16_t) ATA_DATA));
	return value;
}

static void ata_out16(uint16_t value)
{
	__asm__ volatile ("outw %0, %1" : : "a" (value), "Nd" ((uint16_t) ATA_DATA));
}

static void ata_select(uint32_t lba, uint8_t command)
{
	io_out8(ATA_DRIVE, (uint8_t) (0xe0 | ((lba >> 24) & 0x0f)));
	io_out8(ATA_SECCOUNT, 1);
	io_out8(ATA_LBA_LOW, (uint8_t) lba);
	io_out8(ATA_LBA_MID, (uint8_t) (lba >> 8));
	io_out8(ATA_LBA_HIGH, (uint8_t) (lba >> 16));
	io_out8(ATA_COMMAND, command);
}

static int ata_read_sector(uint32_t lba, uint8_t *dst)
{
	uint16_t i;
	uint16_t word;

	if (ata_wait_not_busy() != 0) {
		return -1;
	}
	ata_select(lba, ATA_CMD_READ);
	if (ata_wait_drq() != 0) {
		return -1;
	}
	for (i = 0; i < BLOCK64_SECTOR_SIZE / 2; i++) {
		word = ata_in16();
		dst[i * 2] = (uint8_t) word;
		dst[i * 2 + 1] = (uint8_t) (word >> 8);
	}
	return 0;
}

static int ata_write_sector(uint32_t lba, const uint8_t *src)
{
	uint16_t i;

	if (ata_wait_not_busy() != 0) {
		return -1;
	}
	ata_select(lba, ATA_CMD_WRITE);
	if (ata_wait_drq() != 0) {
		return -1;
	}
	for (i = 0; i < BLOCK64_SECTOR_SIZE / 2; i++) {
		ata_out16((uint16_t) src[i * 2] | ((uint16_t) src[i * 2 + 1] << 8));
	}
	if (ata_wait_not_busy() != 0) {
		return -1;
	}
	io_out8(ATA_COMMAND, ATA_CMD_FLUSH);
	return ata_wait_not_busy();
}

static uint64_t ata64_sector_count(void)
{
	uint16_t id[256];
	uint16_t i;

	if (identified != 0) {
		return sector_count;
	}
	identified = 1;
	if (ata_wait_not_busy() != 0) {
		return 0;
	}
	ata_select(0, ATA_CMD_IDENTIFY);
	if (io_in8(ATA_STATUS) == 0 || ata_wait_drq() != 0) {
		return 0;
	}
	for (i = 0; i < 256; i++) {
		id[i] = ata_in16();
	}
	/* ponytail: 용량을 LBA28(워드 60-61)로만 읽는다. 읽기/쓰기도 LBA28 명령을
	   쓰므로 48비트 항목은 어차피 닿지 않는다. */
	sector_count = (uint64_t) id[60] | ((uint64_t) id[61] << 16);
	return sector_count;
}

static int ata64_read(uint64_t lba, uint32_t count, void *dst)
{
	uint8_t *out;
	uint32_t i;

	if (dst == NULL) {
		return -1;
	}
	out = (uint8_t *) dst;
	for (i = 0; i < count; i++) {
		if (ata_read_sector((uint32_t) (lba + i),
				out + i * BLOCK64_SECTOR_SIZE) != 0) {
			return -1;
		}
	}
	return 0;
}

static int ata64_write(uint64_t lba, uint32_t count, const void *src)
{
	const uint8_t *in;
	uint32_t i;

	if (src == NULL) {
		return -1;
	}
	in = (const uint8_t *) src;
	for (i = 0; i < count; i++) {
		if (ata_write_sector((uint32_t) (lba + i),
				in + i * BLOCK64_SECTOR_SIZE) != 0) {
			return -1;
		}
	}
	return 0;
}

const struct BLOCK64_OPS ata64_ops = {
	"ata",
	ata64_read,
	ata64_write,
	ata64_sector_count
};
