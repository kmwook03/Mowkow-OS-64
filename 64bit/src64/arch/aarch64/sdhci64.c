/*
 * sdhci64.c -- Raspberry Pi 5 BCM2712 SD 카드 전송 계층
 *
 * SDHCI 표준 레지스터만 사용하고 명령과 데이터 전송을 polling한다.
 * cache64가 4 KiB 요청을 내더라도 여기서는 단일 블록 CMD17/CMD24로
 * 나눠 처리한다. DMA와 CQE는 필요해질 때 별도로 추가한다.
 */
#include <arch/arch64.h>
#include <block64.h>
#include <stddef.h>
#include <stdint.h>

#define SDHCI_BASE 0x1000fff000ULL
#define SDHCI_CFG_BASE 0x1000fff400ULL
#define SDHCI_INPUT_HZ 200000000U

#define SDHCI_BLOCK_SIZE 0x04
#define SDHCI_BLOCK_COUNT 0x06
#define SDHCI_ARGUMENT 0x08
#define SDHCI_TRANSFER_MODE 0x0c
#define SDHCI_COMMAND 0x0e
#define SDHCI_RESPONSE0 0x10
#define SDHCI_BUFFER 0x20
#define SDHCI_PRESENT_STATE 0x24
#define SDHCI_HOST_CONTROL 0x28
#define SDHCI_POWER_CONTROL 0x29
#define SDHCI_CLOCK_CONTROL 0x2c
#define SDHCI_TIMEOUT_CONTROL 0x2e
#define SDHCI_SOFTWARE_RESET 0x2f
#define SDHCI_INT_STATUS 0x30
#define SDHCI_INT_ENABLE 0x34
#define SDHCI_SIGNAL_ENABLE 0x38
#define SDHCI_HOST_VERSION 0xfe

#define SDHCI_CMD_INHIBIT (1U << 0)
#define SDHCI_DATA_INHIBIT (1U << 1)
#define SDHCI_CARD_INSERTED (1U << 16)

#define SDHCI_INT_CMD_COMPLETE (1U << 0)
#define SDHCI_INT_XFER_COMPLETE (1U << 1)
#define SDHCI_INT_BUF_WRITE_READY (1U << 4)
#define SDHCI_INT_BUF_READ_READY (1U << 5)
#define SDHCI_INT_ERROR (1U << 15)
#define SDHCI_INT_ERROR_MASK 0xffff0000U

#define SDHCI_CLOCK_INT_EN (1U << 0)
#define SDHCI_CLOCK_INT_STABLE (1U << 1)
#define SDHCI_CLOCK_CARD_EN (1U << 2)

#define SDHCI_RESET_ALL (1U << 0)
#define SDHCI_CTRL_4BIT (1U << 1)
#define SDHCI_CTRL_HISPD (1U << 2)

#define SDHCI_TRNS_BLK_CNT_EN (1U << 1)
#define SDHCI_TRNS_READ (1U << 4)

#define SDHCI_CMD_RESP_NONE 0x00
#define SDHCI_CMD_RESP_LONG 0x01
#define SDHCI_CMD_RESP_SHORT 0x02
#define SDHCI_CMD_RESP_SHORT_BUSY 0x03
#define SDHCI_CMD_CRC (1U << 3)
#define SDHCI_CMD_INDEX (1U << 4)
#define SDHCI_CMD_DATA (1U << 5)
#define SDHCI_MAKE_CMD(index, flags) ((uint16_t) (((index) << 8) | (flags)))

#define SDIO_CFG_SD_PIN_SEL 0x44
#define SDIO_CFG_SD_PIN_SEL_MASK 0x03U
#define SDIO_CFG_SD_PIN_SEL_SD 0x02U

#define SD_OCR_BUSY (1U << 31)
#define SD_OCR_CCS (1U << 30)
#define SD_OCR_VOLTAGE 0x00ff8000U

#define SDHCI_TIMEOUT 10000000U

static volatile uint8_t *sdhci;
static volatile uint8_t *sdhci_cfg;
static uint32_t card_rca;
static uint64_t card_sectors;
static int card_high_capacity;
static int card_ready;

static void delay_us(uint32_t microseconds)
{
	uint64_t frequency;
	uint64_t start;
	uint64_t now;
	uint64_t ticks;

	__asm__ volatile ("mrs %0, cntfrq_el0" : "=r" (frequency));
	__asm__ volatile ("isb\n\tmrs %0, cntpct_el0" : "=r" (start));
	ticks = (frequency * microseconds + 999999ULL) / 1000000ULL;
	do {
		__asm__ volatile ("isb\n\tmrs %0, cntpct_el0" : "=r" (now));
	} while ((int64_t) (now - start) < (int64_t) ticks);
}

static uint8_t read8(uint32_t offset)
{
	return *(volatile uint8_t *) (sdhci + offset);
}

static uint16_t read16(uint32_t offset)
{
	return *(volatile uint16_t *) (sdhci + offset);
}

static uint32_t read32(uint32_t offset)
{
	return *(volatile uint32_t *) (sdhci + offset);
}

static void write8(uint32_t offset, uint8_t value)
{
	*(volatile uint8_t *) (sdhci + offset) = value;
}

static void write16(uint32_t offset, uint16_t value)
{
	*(volatile uint16_t *) (sdhci + offset) = value;
}

static void write32(uint32_t offset, uint32_t value)
{
	*(volatile uint32_t *) (sdhci + offset) = value;
}

static int wait_reg32(uint32_t offset, uint32_t mask, uint32_t value)
{
	uint32_t timeout;

	for (timeout = 0; timeout < SDHCI_TIMEOUT; timeout++) {
		if ((read32(offset) & mask) == value) {
			return 0;
		}
		__asm__ volatile ("yield");
	}
	return -1;
}

static int wait_interrupt(uint32_t wanted)
{
	uint32_t status;
	uint32_t timeout;

	for (timeout = 0; timeout < SDHCI_TIMEOUT; timeout++) {
		status = read32(SDHCI_INT_STATUS);
		if ((status & (SDHCI_INT_ERROR | SDHCI_INT_ERROR_MASK)) != 0) {
			write32(SDHCI_INT_STATUS, status);
			return -1;
		}
		if ((status & wanted) != 0) {
			write32(SDHCI_INT_STATUS, wanted);
			return 0;
		}
		__asm__ volatile ("yield");
	}
	return -1;
}

static int set_clock(uint32_t target_hz)
{
	uint32_t divisor;
	uint16_t control;

	write16(SDHCI_CLOCK_CONTROL, 0);
	if (target_hz == 0) {
		return 0;
	}
	divisor = (SDHCI_INPUT_HZ + target_hz * 2U - 1U) / (target_hz * 2U);
	if (divisor == 0) {
		divisor = 1;
	}
	if (divisor > 0x3ffU) {
		divisor = 0x3ffU;
	}
	control = (uint16_t) (((divisor & 0xffU) << 8) |
		((divisor & 0x300U) >> 2) | SDHCI_CLOCK_INT_EN);
	write16(SDHCI_CLOCK_CONTROL, control);
	if (wait_reg32(SDHCI_CLOCK_CONTROL, SDHCI_CLOCK_INT_STABLE,
			SDHCI_CLOCK_INT_STABLE) != 0) {
		return -1;
	}
	write16(SDHCI_CLOCK_CONTROL, control | SDHCI_CLOCK_CARD_EN);
	return 0;
}

static int send_command(uint32_t index, uint32_t argument, uint16_t flags,
	uint32_t *response)
{
	uint32_t inhibit;

	inhibit = SDHCI_CMD_INHIBIT;
	if ((flags & SDHCI_CMD_DATA) != 0 ||
			(flags & 3U) == SDHCI_CMD_RESP_SHORT_BUSY) {
		inhibit |= SDHCI_DATA_INHIBIT;
	}
	if (wait_reg32(SDHCI_PRESENT_STATE, inhibit, 0) != 0) {
		return -1;
	}
	write32(SDHCI_INT_STATUS, 0xffffffffU);
	write32(SDHCI_ARGUMENT, argument);
	write16(SDHCI_COMMAND, SDHCI_MAKE_CMD(index, flags));
	if (wait_interrupt(SDHCI_INT_CMD_COMPLETE) != 0) {
		return -1;
	}
	if (response != NULL) {
		*response = read32(SDHCI_RESPONSE0);
	}
	if ((flags & 3U) == SDHCI_CMD_RESP_SHORT_BUSY &&
			wait_reg32(SDHCI_PRESENT_STATE, SDHCI_DATA_INHIBIT, 0) != 0) {
		return -1;
	}
	return 0;
}

static uint32_t response_bits(const uint32_t response[4], uint32_t start,
	uint32_t size)
{
	uint32_t offset;
	uint32_t shift;
	uint32_t value;

	offset = 3U - start / 32U;
	shift = start & 31U;
	value = response[offset] >> shift;
	if (size + shift > 32U && offset != 0) {
		value |= response[offset - 1U] << (32U - shift);
	}
	return value & ((1U << size) - 1U);
}

static int read_card_capacity(void)
{
	uint32_t response[4];
	uint32_t offset;
	uint32_t i;
	uint32_t c_size;
	uint32_t c_size_mult;
	uint32_t read_bl_len;

	if (send_command(9, card_rca << 16,
			SDHCI_CMD_RESP_LONG | SDHCI_CMD_CRC, NULL) != 0) {
		return -1;
	}
	/* SDHCI drops the CRC byte. Reconstruct the 128 response bits in the
	   same layout used by the SD/MMC bit-field definitions. */
	for (i = 0; i < 4; i++) {
		offset = (3U - i) * 4U;
		response[i] = read32(SDHCI_RESPONSE0 + offset) << 8;
		if (i != 3U) {
			response[i] |= read8(SDHCI_RESPONSE0 + offset - 1U);
		}
	}
	if (response_bits(response, 126, 2) == 1U) {
		c_size = response_bits(response, 48, 22);
		card_sectors = ((uint64_t) c_size + 1ULL) * 1024ULL;
	} else {
		read_bl_len = response_bits(response, 80, 4);
		c_size = response_bits(response, 62, 12);
		c_size_mult = response_bits(response, 47, 3);
		card_sectors = (((uint64_t) c_size + 1ULL) <<
			(c_size_mult + 2U + read_bl_len)) / BLOCK64_SECTOR_SIZE;
	}
	return card_sectors != 0 ? 0 : -1;
}

static int send_app_command(uint32_t index, uint32_t argument, uint16_t flags,
	uint32_t *response)
{
	if (send_command(55, card_rca << 16,
			SDHCI_CMD_RESP_SHORT | SDHCI_CMD_CRC | SDHCI_CMD_INDEX,
			NULL) != 0) {
		return -1;
	}
	return send_command(index, argument, flags, response);
}

static int read_data_command(uint32_t index, uint32_t argument, void *dst,
	uint16_t bytes)
{
	uint32_t *out;
	uint32_t words;
	uint32_t i;

	write16(SDHCI_BLOCK_SIZE, bytes);
	write16(SDHCI_BLOCK_COUNT, 1);
	write16(SDHCI_TRANSFER_MODE,
		SDHCI_TRNS_BLK_CNT_EN | SDHCI_TRNS_READ);
	if (send_command(index, argument, SDHCI_CMD_RESP_SHORT | SDHCI_CMD_CRC |
			SDHCI_CMD_INDEX | SDHCI_CMD_DATA, NULL) != 0 ||
			wait_interrupt(SDHCI_INT_BUF_READ_READY) != 0) {
		return -1;
	}
	out = (uint32_t *) dst;
	words = bytes / sizeof(uint32_t);
	for (i = 0; i < words; i++) {
		out[i] = read32(SDHCI_BUFFER);
	}
	return wait_interrupt(SDHCI_INT_XFER_COMPLETE);
}

static int write_data_command(uint32_t index, uint32_t argument,
	const void *src, uint16_t bytes)
{
	const uint32_t *in;
	uint32_t words;
	uint32_t i;

	write16(SDHCI_BLOCK_SIZE, bytes);
	write16(SDHCI_BLOCK_COUNT, 1);
	write16(SDHCI_TRANSFER_MODE, SDHCI_TRNS_BLK_CNT_EN);
	if (send_command(index, argument, SDHCI_CMD_RESP_SHORT | SDHCI_CMD_CRC |
			SDHCI_CMD_INDEX | SDHCI_CMD_DATA, NULL) != 0 ||
			wait_interrupt(SDHCI_INT_BUF_WRITE_READY) != 0) {
		return -1;
	}
	in = (const uint32_t *) src;
	words = bytes / sizeof(uint32_t);
	for (i = 0; i < words; i++) {
		write32(SDHCI_BUFFER, in[i]);
	}
	return wait_interrupt(SDHCI_INT_XFER_COMPLETE);
}

static void try_high_speed(void)
{
	uint32_t status[16] __attribute__((aligned(4)));
	uint8_t *bytes;

	/* CMD6 mode 1, group 1 function 1. Unsupported cards remain at 25 MHz. */
	if (read_data_command(6, 0x80fffff1U, status, sizeof(status)) != 0) {
		return;
	}
	bytes = (uint8_t *) status;
	if ((bytes[16] & 0x0fU) != 1U) {
		return;
	}
	write8(SDHCI_HOST_CONTROL,
		read8(SDHCI_HOST_CONTROL) | SDHCI_CTRL_HISPD);
	(void) set_clock(50000000U);
}

static int controller_init(void)
{
	volatile uint32_t *pin_select;
	uint32_t response;
	uint32_t retry;

	sdhci = (volatile uint8_t *) arch64_phys_to_virt(SDHCI_BASE);
	sdhci_cfg = (volatile uint8_t *) arch64_phys_to_virt(SDHCI_CFG_BASE);
	if (read16(SDHCI_HOST_VERSION) == 0xffffU) {
		return -1;
	}

	/* BCM2712의 PHY에 SD 속도 계열을 선택한다. */
	pin_select = (volatile uint32_t *) (sdhci_cfg + SDIO_CFG_SD_PIN_SEL);
	*pin_select = (*pin_select & ~SDIO_CFG_SD_PIN_SEL_MASK) |
		SDIO_CFG_SD_PIN_SEL_SD;
	__asm__ volatile ("dsb sy" ::: "memory");

	write8(SDHCI_SOFTWARE_RESET, SDHCI_RESET_ALL);
	for (retry = 0; retry < SDHCI_TIMEOUT; retry++) {
		if ((read8(SDHCI_SOFTWARE_RESET) & SDHCI_RESET_ALL) == 0) {
			break;
		}
	}
	if (retry == SDHCI_TIMEOUT) {
		return -1;
	}
	write8(SDHCI_POWER_CONTROL, 0x0f); /* 3.3 V, bus power on */
	delay_us(2000);
	write8(SDHCI_TIMEOUT_CONTROL, 0x0e);
	write32(SDHCI_INT_ENABLE, 0xffffffffU);
	write32(SDHCI_SIGNAL_ENABLE, 0); /* polling only */
	if (set_clock(400000U) != 0 ||
			send_command(0, 0, SDHCI_CMD_RESP_NONE, NULL) != 0 ||
			send_command(8, 0x1aa,
				SDHCI_CMD_RESP_SHORT | SDHCI_CMD_CRC | SDHCI_CMD_INDEX,
				&response) != 0 || (response & 0xfffU) != 0x1aaU) {
		return -1;
	}

	card_rca = 0;
	for (retry = 0; retry < 1000U; retry++) {
		if (send_app_command(41, SD_OCR_CCS | SD_OCR_VOLTAGE,
				SDHCI_CMD_RESP_SHORT, &response) != 0) {
			return -1;
		}
		if ((response & SD_OCR_BUSY) != 0) {
			break;
		}
		delay_us(1000);
	}
	if (retry == 1000U) {
		return -1;
	}
	card_high_capacity = (response & SD_OCR_CCS) != 0;
	if (send_command(2, 0, SDHCI_CMD_RESP_LONG | SDHCI_CMD_CRC, NULL) != 0 ||
			send_command(3, 0, SDHCI_CMD_RESP_SHORT | SDHCI_CMD_CRC |
				SDHCI_CMD_INDEX, &response) != 0) {
		return -1;
	}
	card_rca = response >> 16;
	if (card_rca == 0 || read_card_capacity() != 0 ||
			send_command(7, card_rca << 16,
			SDHCI_CMD_RESP_SHORT_BUSY | SDHCI_CMD_CRC | SDHCI_CMD_INDEX,
			NULL) != 0) {
		return -1;
	}
	if (!card_high_capacity && send_command(16, BLOCK64_SECTOR_SIZE,
			SDHCI_CMD_RESP_SHORT | SDHCI_CMD_CRC | SDHCI_CMD_INDEX,
			NULL) != 0) {
		return -1;
	}
	if (send_app_command(6, 2, SDHCI_CMD_RESP_SHORT | SDHCI_CMD_CRC |
			SDHCI_CMD_INDEX, NULL) != 0) {
		return -1;
	}
	write8(SDHCI_HOST_CONTROL,
		read8(SDHCI_HOST_CONTROL) | SDHCI_CTRL_4BIT);
	if (set_clock(25000000U) != 0) {
		return -1;
	}
	try_high_speed();
	card_ready = 1;
	return 0;
}

int sdhci64_probe(void)
{
	if (card_ready != 0) {
		return 0;
	}
	return controller_init();
}

static uint32_t card_argument(uint64_t lba)
{
	return card_high_capacity != 0 ? (uint32_t) lba :
		(uint32_t) (lba * BLOCK64_SECTOR_SIZE);
}

static int sdhci64_read(uint64_t lba, uint32_t count, void *dst)
{
	uint8_t *out;
	uint32_t i;

	if (card_ready == 0 || dst == NULL || lba > 0xffffffffULL) {
		return -1;
	}
	out = (uint8_t *) dst;
	for (i = 0; i < count; i++) {
		if (read_data_command(17, card_argument(lba + i),
				out + (size_t) i * BLOCK64_SECTOR_SIZE,
				BLOCK64_SECTOR_SIZE) != 0) {
			return -1;
		}
	}
	return 0;
}

static int sdhci64_write(uint64_t lba, uint32_t count, const void *src)
{
	const uint8_t *in;
	uint32_t i;

	if (card_ready == 0 || src == NULL || lba > 0xffffffffULL) {
		return -1;
	}
	in = (const uint8_t *) src;
	for (i = 0; i < count; i++) {
		if (write_data_command(24, card_argument(lba + i),
				in + (size_t) i * BLOCK64_SECTOR_SIZE,
				BLOCK64_SECTOR_SIZE) != 0) {
			return -1;
		}
	}
	return 0;
}

static uint64_t sdhci64_sector_count(void)
{
	return card_sectors;
}

const struct BLOCK64_OPS sdhci64_ops = {
	"sdhci",
	sdhci64_read,
	sdhci64_write,
	sdhci64_sector_count
};
