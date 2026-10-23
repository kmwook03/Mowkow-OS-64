/*
 * sdhci64.c -- Raspberry Pi 5 BCM2712 SD 카드 전송 계층
 *
 * SDHCI 표준 레지스터만 사용하고 명령과 데이터 전송을 polling한다.
 * cache64가 4 KiB 요청을 내더라도 여기서는 단일 블록 CMD17/CMD24로
 * 나눠 처리한다. DMA와 CQE는 필요해질 때 별도로 추가한다.
 */
#include <arch/arch64.h>
#include <arch/aarch64_poll64.h>
#include "platform_io64.h"
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
#define SDHCI_RESET_CMD (1U << 1)
#define SDHCI_RESET_DATA (1U << 2)
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

#define SDHCI_TIMEOUT_MS64 1000U
#define SDHCI_OCR_TIMEOUT_MS64 1000U

static uintptr_t sdhci;
static uintptr_t sdhci_cfg;
static uint32_t card_rca;
static uint64_t card_sectors;
static int card_high_capacity;
static int card_ready;
static int recovery_failed;

#ifdef SDHCI64_HOST_TEST
uint8_t sdhci64_test_read8(uint32_t offset);
uint16_t sdhci64_test_read16(uint32_t offset);
uint32_t sdhci64_test_read32(uint32_t offset);
void sdhci64_test_write8(uint32_t offset, uint8_t value);
void sdhci64_test_write16(uint32_t offset, uint16_t value);
void sdhci64_test_write32(uint32_t offset, uint32_t value);

uint64_t sdhci64_test_counter64(void);
uint64_t sdhci64_test_frequency64(void);
void sdhci64_test_relax64(void);
void sdhci64_test_configure_pins64(void);
#endif

static uint64_t counter64(void)
{
#ifdef SDHCI64_HOST_TEST
	return sdhci64_test_counter64();
#else
	return a64_counter64();
#endif
}

static uint64_t frequency64(void)
{
#ifdef SDHCI64_HOST_TEST
	return sdhci64_test_frequency64();
#else
	return a64_frequency64();
#endif
}

static void cpu_relax(void)
{
#ifdef SDHCI64_HOST_TEST
	sdhci64_test_relax64();
#else
	a64_relax64();
#endif
}

static void mmio_barrier(void)
{
#ifndef SDHCI64_HOST_TEST
	a64_barrier64();
#endif
}

static int deadline64(struct POLL_DEADLINE64 *deadline, uint32_t duration,
	uint32_t units, const char *stage)
{
	uint64_t frequency = frequency64();
	int status = poll_deadline_init64(deadline, counter64(), frequency, duration, units);

	if (status != 0) {
		aarch64_poll_fail64("sdhci", stage, "CNTFRQ_EL0", 0, frequency, status);
	}
	return status;
}

static int delay_us(uint32_t microseconds)
{
	struct POLL_DEADLINE64 deadline;

	if (deadline64(&deadline, microseconds, 1000000U, "delay") != 0) { return -1; }
	while (!poll_deadline_expired64(&deadline, counter64())) { cpu_relax(); }
	return 0;
}

static uint8_t read8(uint32_t offset)
{
#ifdef SDHCI64_HOST_TEST
	return sdhci64_test_read8(offset);
#else
	return *(volatile uint8_t *) (sdhci + offset);
#endif
}

static uint16_t read16(uint32_t offset)
{
#ifdef SDHCI64_HOST_TEST
	return sdhci64_test_read16(offset);
#else
	return *(volatile uint16_t *) (sdhci + offset);
#endif
}

static uint32_t read32(uint32_t offset)
{
#ifdef SDHCI64_HOST_TEST
	return sdhci64_test_read32(offset);
#else
	return *(volatile uint32_t *) (sdhci + offset);
#endif
}

static void write8(uint32_t offset, uint8_t value)
{
#ifdef SDHCI64_HOST_TEST
	sdhci64_test_write8(offset, value);
#else
	*(volatile uint8_t *) (sdhci + offset) = value;
#endif
}

static void write16(uint32_t offset, uint16_t value)
{
#ifdef SDHCI64_HOST_TEST
	sdhci64_test_write16(offset, value);
#else
	*(volatile uint16_t *) (sdhci + offset) = value;
#endif
}

static void write32(uint32_t offset, uint32_t value)
{
#ifdef SDHCI64_HOST_TEST
	sdhci64_test_write32(offset, value);
#else
	*(volatile uint32_t *) (sdhci + offset) = value;
#endif
}

static int wait_reg32(uint32_t offset, uint32_t mask, uint32_t value, const char *stage)
{
	struct POLL_DEADLINE64 deadline;
	uint32_t sampled;

	if (deadline64(&deadline, SDHCI_TIMEOUT_MS64, 1000U, stage) != 0) { return -1; }
	for (;;) {
		sampled = read32(offset);
		if ((sampled & mask) == value) { return 0; }
		if (poll_deadline_expired64(&deadline, counter64())) {
			aarch64_poll_fail64("sdhci", stage, offset == SDHCI_CLOCK_CONTROL ? "CLOCK_CONTROL" : "PRESENT_STATE",
				sdhci + offset, sampled, -110);
			return -1;
		}
		cpu_relax();
	}
}

static int wait_interrupt(uint32_t wanted, const char *stage)
{
	struct POLL_DEADLINE64 deadline;
	uint32_t status;

	if (deadline64(&deadline, SDHCI_TIMEOUT_MS64, 1000U, stage) != 0) { return -1; }
	for (;;) {
		status = read32(SDHCI_INT_STATUS);
		if ((status & (SDHCI_INT_ERROR | SDHCI_INT_ERROR_MASK)) != 0) {
			aarch64_poll_fail64("sdhci", stage, "INT_STATUS", sdhci + SDHCI_INT_STATUS,
				status, -5);
			write32(SDHCI_INT_STATUS, status);
			return -1;
		}
		if ((status & wanted) != 0) {
			write32(SDHCI_INT_STATUS, wanted);
			return 0;
		}
		if (poll_deadline_expired64(&deadline, counter64())) {
			aarch64_poll_fail64("sdhci", stage, "INT_STATUS", sdhci + SDHCI_INT_STATUS,
				status, -110);
			return -1;
		}
		cpu_relax();
	}
}

static int wait_reset64(uint8_t mask, const char *stage)
{
	struct POLL_DEADLINE64 deadline;
	uint8_t value;

	if (deadline64(&deadline, SDHCI_TIMEOUT_MS64, 1000U, stage) != 0) { return -1; }
	for (;;) {
		value = read8(SDHCI_SOFTWARE_RESET);
		if ((value & mask) == 0) { return 0; }
		if (poll_deadline_expired64(&deadline, counter64())) {
			aarch64_poll_fail64("sdhci", stage, "SOFTWARE_RESET", sdhci + SDHCI_SOFTWARE_RESET,
				value, -110);
			return -1;
		}
		cpu_relax();
	}
}

static void recover_command(int has_data)
{
	uint8_t reset = SDHCI_RESET_CMD;

	write32(SDHCI_INT_STATUS, 0xffffffffU);
	if (has_data != 0) { reset |= SDHCI_RESET_DATA; }
	write8(SDHCI_SOFTWARE_RESET, reset);
	if (wait_reset64(reset, "command-data-reset") == 0) { return; }
	/* Even a successful full reset requires reprobe before further card I/O. */
	card_ready = 0;
	recovery_failed = 1;
	write8(SDHCI_SOFTWARE_RESET, SDHCI_RESET_ALL);
	(void) wait_reset64(SDHCI_RESET_ALL, "recovery-reset-all");
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
			SDHCI_CLOCK_INT_STABLE, "clock-stable") != 0) {
		return -1;
	}
	write16(SDHCI_CLOCK_CONTROL, control | SDHCI_CLOCK_CARD_EN);
	return 0;
}

static int send_command(uint32_t index, uint32_t argument, uint16_t flags,
	uint32_t *response)
{
	int has_data;
	uint32_t inhibit;

	has_data = (flags & SDHCI_CMD_DATA) != 0 ||
		(flags & 3U) == SDHCI_CMD_RESP_SHORT_BUSY;
	inhibit = SDHCI_CMD_INHIBIT;
	if (has_data != 0) {
		inhibit |= SDHCI_DATA_INHIBIT;
	}
	if (wait_reg32(SDHCI_PRESENT_STATE, inhibit, 0, "command-inhibit") != 0) {
		recover_command(has_data);
		return -1;
	}
	write32(SDHCI_INT_STATUS, 0xffffffffU);
	write32(SDHCI_ARGUMENT, argument);
	write16(SDHCI_COMMAND, SDHCI_MAKE_CMD(index, flags));
	if (wait_interrupt(SDHCI_INT_CMD_COMPLETE, "command-complete") != 0) {
		recover_command(has_data);
		return -1;
	}
	if (response != NULL) {
		*response = read32(SDHCI_RESPONSE0);
	}
	if ((flags & 3U) == SDHCI_CMD_RESP_SHORT_BUSY &&
			wait_reg32(SDHCI_PRESENT_STATE, SDHCI_DATA_INHIBIT, 0, "busy-release") != 0) {
		recover_command(1);
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
	return value & (UINT32_MAX >> (32U - size));
}

static int decode_card_capacity(const uint32_t response[4],
	uint64_t *sectors)
{
	uint32_t structure;
	uint32_t c_size;
	uint32_t c_size_mult;
	uint32_t read_bl_len;
	uint64_t value;

	if (response == NULL || sectors == NULL) {
		return -1;
	}
	structure = response_bits(response, 126, 2);
	if (structure == 1U) {
		c_size = response_bits(response, 48, 22);
		value = ((uint64_t) c_size + 1ULL) * 1024ULL;
	} else if (structure == 0U) {
		read_bl_len = response_bits(response, 80, 4);
		c_size = response_bits(response, 62, 12);
		c_size_mult = response_bits(response, 47, 3);
		value = (((uint64_t) c_size + 1ULL) <<
			(c_size_mult + 2U + read_bl_len)) / BLOCK64_SECTOR_SIZE;
	} else {
		return -1;
	}
	if (value == 0 || (structure == 1U && value > UINT32_MAX + 1ULL) ||
			(structure == 0U && value >
			(UINT32_MAX / BLOCK64_SECTOR_SIZE) + 1ULL)) {
		return -1;
	}
	*sectors = value;
	return 0;
}

static int read_card_capacity(void)
{
	uint32_t response[4];
	uint32_t offset;
	uint32_t i;
	uint32_t structure;

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
	structure = response_bits(response, 126, 2);
	if ((card_high_capacity != 0 && structure != 1U) ||
			(card_high_capacity == 0 && structure != 0U)) {
		return -1;
	}
	return decode_card_capacity(response, &card_sectors);
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
	uint8_t *out;
	uint32_t words;
	uint32_t i;
	uint32_t value;

	if (dst == NULL || bytes == 0 ||
			(bytes & (sizeof(uint32_t) - 1U)) != 0) {
		return -1;
	}

	write16(SDHCI_BLOCK_SIZE, bytes);
	write16(SDHCI_BLOCK_COUNT, 1);
	write16(SDHCI_TRANSFER_MODE,
		SDHCI_TRNS_BLK_CNT_EN | SDHCI_TRNS_READ);
	if (send_command(index, argument, SDHCI_CMD_RESP_SHORT | SDHCI_CMD_CRC |
			SDHCI_CMD_INDEX | SDHCI_CMD_DATA, NULL) != 0) {
		return -1;
	}
	if (wait_interrupt(SDHCI_INT_BUF_READ_READY, "read-ready") != 0) {
		recover_command(1);
		return -1;
	}
	/* The SDHCI buffer is a 32-bit PIO port, not DMA.  Marshal bytes so an
	   unaligned cache buffer is safe and no cache maintenance is required. */
	out = (uint8_t *) dst;
	words = bytes / sizeof(uint32_t);
	for (i = 0; i < words; i++) {
		value = read32(SDHCI_BUFFER);
		out[i * 4U] = (uint8_t) value;
		out[i * 4U + 1U] = (uint8_t) (value >> 8);
		out[i * 4U + 2U] = (uint8_t) (value >> 16);
		out[i * 4U + 3U] = (uint8_t) (value >> 24);
	}
	if (wait_interrupt(SDHCI_INT_XFER_COMPLETE, "transfer-complete") != 0) {
		recover_command(1);
		return -1;
	}
	return 0;
}

static int write_data_command(uint32_t index, uint32_t argument,
	const void *src, uint16_t bytes)
{
	const uint8_t *in;
	uint32_t words;
	uint32_t i;
	uint32_t value;

	if (src == NULL || bytes == 0 ||
			(bytes & (sizeof(uint32_t) - 1U)) != 0) {
		return -1;
	}

	write16(SDHCI_BLOCK_SIZE, bytes);
	write16(SDHCI_BLOCK_COUNT, 1);
	write16(SDHCI_TRANSFER_MODE, SDHCI_TRNS_BLK_CNT_EN);
	if (send_command(index, argument, SDHCI_CMD_RESP_SHORT | SDHCI_CMD_CRC |
			SDHCI_CMD_INDEX | SDHCI_CMD_DATA, NULL) != 0) {
		return -1;
	}
	if (wait_interrupt(SDHCI_INT_BUF_WRITE_READY, "write-ready") != 0) {
		recover_command(1);
		return -1;
	}
	in = (const uint8_t *) src;
	words = bytes / sizeof(uint32_t);
	for (i = 0; i < words; i++) {
		value = (uint32_t) in[i * 4U] |
			((uint32_t) in[i * 4U + 1U] << 8) |
			((uint32_t) in[i * 4U + 2U] << 16) |
			((uint32_t) in[i * 4U + 3U] << 24);
		write32(SDHCI_BUFFER, value);
	}
	if (wait_interrupt(SDHCI_INT_XFER_COMPLETE, "transfer-complete") != 0) {
		recover_command(1);
		return -1;
	}
	return 0;
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
	uint32_t response;
	struct POLL_DEADLINE64 ocr_deadline;

	sdhci = arch64_phys_to_virt(SDHCI_BASE);
	sdhci_cfg = arch64_phys_to_virt(SDHCI_CFG_BASE);
	if (read16(SDHCI_HOST_VERSION) == 0xffffU) {
		return -1;
	}

	/* BCM2712의 PHY에 SD 속도 계열을 선택한다. */
#ifdef SDHCI64_HOST_TEST
	sdhci64_test_configure_pins64();
#else
	a64_write32_64(sdhci_cfg + SDIO_CFG_SD_PIN_SEL,
		(a64_read32_64(sdhci_cfg + SDIO_CFG_SD_PIN_SEL) & ~SDIO_CFG_SD_PIN_SEL_MASK) |
		SDIO_CFG_SD_PIN_SEL_SD);
#endif
	mmio_barrier();

	write8(SDHCI_SOFTWARE_RESET, SDHCI_RESET_ALL);
	if (wait_reset64(SDHCI_RESET_ALL, "controller-reset") != 0) { return -1; }
	recovery_failed = 0;
	write8(SDHCI_POWER_CONTROL, 0x0f); /* 3.3 V, bus power on */
	if (delay_us(2000) != 0) { return -1; }
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
	if (deadline64(&ocr_deadline, SDHCI_OCR_TIMEOUT_MS64, 1000U, "ocr-ready") != 0) {
		return -1;
	}
	for (;;) {
		if (send_app_command(41, SD_OCR_CCS | SD_OCR_VOLTAGE,
				SDHCI_CMD_RESP_SHORT, &response) != 0) {
			return -1;
		}
		if ((response & SD_OCR_BUSY) != 0) {
			break;
		}
		if (poll_deadline_expired64(&ocr_deadline, counter64())) {
			aarch64_poll_fail64("sdhci", "ocr-ready", "RESPONSE0", sdhci + SDHCI_RESPONSE0,
				response, -110);
			return -1;
		}
		if (delay_us(1000) != 0) { return -1; }
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
	if (recovery_failed != 0) {
		return -1;
	}
	card_ready = 1;
	return 0;
}

int sdhci64_probe(void)
{
	int status;

	if (card_ready != 0) {
		return 0;
	}
	card_rca = 0;
	card_sectors = 0;
	card_high_capacity = 0;
	recovery_failed = 0;
	status = controller_init();
	if (status != 0) {
		card_ready = 0;
		card_rca = 0;
		card_sectors = 0;
		card_high_capacity = 0;
		recovery_failed = 0;
	}
	return status;
}

static int card_argument(uint64_t lba, uint32_t *argument)
{
	if (argument == NULL) {
		return -1;
	}
	if (card_high_capacity != 0) {
		if (lba > UINT32_MAX) {
			return -1;
		}
		*argument = (uint32_t) lba;
		return 0;
	}
	if (lba > UINT32_MAX / BLOCK64_SECTOR_SIZE) {
		return -1;
	}
	*argument = (uint32_t) (lba * BLOCK64_SECTOR_SIZE);
	return 0;
}

static int request_valid(uint64_t lba, uint32_t count, const void *buffer)
{
	uint64_t last;
	size_t bytes;

	if (count == 0 || card_ready == 0 || buffer == NULL || card_sectors == 0 ||
			lba >= card_sectors || (uint64_t) count > card_sectors - lba) {
		return 0;
	}
	bytes = (size_t) count * BLOCK64_SECTOR_SIZE;
	if ((uintptr_t) buffer > UINTPTR_MAX - bytes) {
		return 0;
	}
	last = lba + (uint64_t) count - 1ULL;
	if (card_high_capacity != 0) {
		return last <= UINT32_MAX;
	}
	return last <= UINT32_MAX / BLOCK64_SECTOR_SIZE;
}

static int sdhci64_read(uint64_t lba, uint32_t count, void *dst)
{
	uint8_t *out;
	uint32_t argument;
	uint32_t i;

	if (count == 0) {
		return 0;
	}
	if (request_valid(lba, count, dst) == 0) {
		return -1;
	}
	out = (uint8_t *) dst;
	for (i = 0; i < count; i++) {
		if (card_argument(lba + i, &argument) != 0 ||
				read_data_command(17, argument,
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
	uint32_t argument;
	uint32_t i;

	if (count == 0) {
		return 0;
	}
	if (request_valid(lba, count, src) == 0) {
		return -1;
	}
	in = (const uint8_t *) src;
	for (i = 0; i < count; i++) {
		if (card_argument(lba + i, &argument) != 0 ||
				write_data_command(24, argument,
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
