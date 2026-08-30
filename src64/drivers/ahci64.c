/*
 * ahci64.c -- AHCI(SATA) 전송 계층
 *
 * PCI로 컨트롤러를 찾고, 포트 하나를 열어 48비트 DMA로 읽고 쓴다.
 * 인터럽트 없이 기다리며 한 번에 명령 하나만 보낸다.
 * 위쪽 파일 시스템이 한 줄로만 요청하므로 그 이상은 필요 없다.
 *
 * 주의: 쓸 수 있는 첫 포트만 쓴다.
 * 여러 포트나 NCQ를 쓰려면 진짜 요청 큐가 있어야 하는데 아직 구현되지 않았다.
 */
#include <block64.h>
#include <memory64.h>
#include <pci64.h>
#include <stddef.h>
#include <stdint.h>

/* HBA 레지스터 */
#define HBA_GHC 0x04
#define HBA_PI 0x0c
#define GHC_AE 0x80000000u
#define GHC_IE 0x00000002u

/* 포트 레지스터. 0x100 + 포트 번호 * 0x80 을 기준으로 한 상대 위치 */
#define PORT_CLB 0x00
#define PORT_CLBU 0x04
#define PORT_FB 0x08
#define PORT_FBU 0x0c
#define PORT_IS 0x10
#define PORT_CMD 0x18
#define PORT_TFD 0x20
#define PORT_SIG 0x24
#define PORT_SSTS 0x28
#define PORT_SERR 0x30
#define PORT_CI 0x38

#define CMD_ST 0x0001
#define CMD_FRE 0x0010
#define CMD_FR 0x4000
#define CMD_CR 0x8000

#define TFD_ERR 0x01
#define TFD_DRQ 0x08
#define TFD_BSY 0x80
#define IS_TFES 0x40000000u

#define SIG_SATA 0x00000101
#define SSTS_DET_PRESENT 0x03

#define ATA_CMD_READ_DMA_EX 0x25
#define ATA_CMD_WRITE_DMA_EX 0x35
#define ATA_CMD_IDENTIFY 0xec

#define AHCI_TIMEOUT 10000000
/* 명령 하나가 옮기는 최대 섹터 수. PRDT 항목 하나로 8192섹터까지 되지만,
   캐시는 한 번에 블록 하나보다 많이 요청하지 않는다. */
#define AHCI_MAX_SECTORS 128

static volatile uint8_t *abar;
static uint32_t port_base;
static uint8_t *command_list;
static uint8_t *received_fis;
static uint8_t *command_table;
static uint64_t sector_total;
static int ready;

static uint32_t reg_read(uint32_t offset)
{
	return *(volatile uint32_t *) (abar + offset);
}

static void reg_write(uint32_t offset, uint32_t value)
{
	*(volatile uint32_t *) (abar + offset) = value;
}

static uint32_t port_read(uint32_t offset)
{
	return reg_read(port_base + offset);
}

static void port_write(uint32_t offset, uint32_t value)
{
	reg_write(port_base + offset, value);
}

static void write32(uint8_t *p, uint32_t value)
{
	p[0] = (uint8_t) value;
	p[1] = (uint8_t) (value >> 8);
	p[2] = (uint8_t) (value >> 16);
	p[3] = (uint8_t) (value >> 24);
}

static void zero(uint8_t *p, uint32_t size)
{
	uint32_t i;

	for (i = 0; i < size; i++) {
		p[i] = 0;
	}
}

static int port_stop(void)
{
	uint32_t timeout;

	port_write(PORT_CMD, port_read(PORT_CMD) & ~(CMD_ST | CMD_FRE));
	for (timeout = 0; timeout < AHCI_TIMEOUT; timeout++) {
		if ((port_read(PORT_CMD) & (CMD_CR | CMD_FR)) == 0) {
			return 0;
		}
	}
	return -1;
}

static void port_start(void)
{
	port_write(PORT_CMD, port_read(PORT_CMD) | CMD_FRE);
	port_write(PORT_CMD, port_read(PORT_CMD) | CMD_ST);
}

/* 명령 하나를 만들고 끝날 때까지 기다린다.
   `write`가 방향, `buffer`가 DMA 대상, `bytes`가 그 길이 */
static int run_command(uint8_t command, uint64_t lba, uint32_t sectors,
	void *buffer, uint32_t bytes, int write)
{
	uint8_t *header;
	uint8_t *fis;
	uint8_t *prdt;
	uint32_t timeout;

	if (ready == 0 && command != ATA_CMD_IDENTIFY) {
		return -1;
	}
	for (timeout = 0; timeout < AHCI_TIMEOUT; timeout++) {
		if ((port_read(PORT_TFD) & (TFD_BSY | TFD_DRQ)) == 0) {
			break;
		}
		if (timeout + 1 == AHCI_TIMEOUT) {
			return -1;
		}
	}
	port_write(PORT_IS, port_read(PORT_IS));
	port_write(PORT_SERR, port_read(PORT_SERR));

	/* 슬롯 0만 쓴다. (명령을 한 번에 하나씩만 보냄) */
	header = command_list;
	zero(header, 32);
	/* 명령 FIS 길이(dword 단위), 쓰기 표시, PRDT 항목 한 개 */
	write32(header, 5 | (write != 0 ? (1u << 6) : 0) | (1u << 16));
	write32(header + 8, (uint32_t) (uintptr_t) command_table);
	write32(header + 12, (uint32_t) ((uint64_t) (uintptr_t) command_table >> 32));

	zero(command_table, 128 + 16);
	fis = command_table;
	fis[0] = 0x27;			/* 호스트에서 장치로 */
	fis[1] = 0x80;			/* 명령이라는 표시 */
	fis[2] = command;
	fis[4] = (uint8_t) lba;
	fis[5] = (uint8_t) (lba >> 8);
	fis[6] = (uint8_t) (lba >> 16);
	fis[7] = 0x40;			/* LBA 모드 */
	fis[8] = (uint8_t) (lba >> 24);
	fis[9] = (uint8_t) (lba >> 32);
	fis[10] = (uint8_t) (lba >> 40);
	fis[12] = (uint8_t) sectors;
	fis[13] = (uint8_t) (sectors >> 8);

	prdt = command_table + 128;
	write32(prdt, (uint32_t) (uintptr_t) buffer);
	write32(prdt + 4, (uint32_t) ((uint64_t) (uintptr_t) buffer >> 32));
	write32(prdt + 12, bytes - 1);	/* 바이트 수는 0부터 센다 */

	port_write(PORT_CI, 1);
	for (timeout = 0; timeout < AHCI_TIMEOUT; timeout++) {
		if ((port_read(PORT_CI) & 1) == 0) {
			break;
		}
		if ((port_read(PORT_IS) & IS_TFES) != 0) {
			return -1;
		}
		if (timeout + 1 == AHCI_TIMEOUT) {
			return -1;
		}
	}
	if ((port_read(PORT_TFD) & TFD_ERR) != 0 || (port_read(PORT_IS) & IS_TFES) != 0) {
		return -1;
	}
	return 0;
}

static int transfer(uint64_t lba, uint32_t count, uint8_t *buffer, int write)
{
	uint32_t chunk;

	while (count > 0) {
		chunk = count > AHCI_MAX_SECTORS ? AHCI_MAX_SECTORS : count;
		if (run_command(write != 0 ? ATA_CMD_WRITE_DMA_EX : ATA_CMD_READ_DMA_EX,
				lba, chunk, buffer, chunk * BLOCK64_SECTOR_SIZE, write) != 0) {
			return -1;
		}
		lba += chunk;
		buffer += chunk * BLOCK64_SECTOR_SIZE;
		count -= chunk;
	}
	return 0;
}

static int ahci64_read(uint64_t lba, uint32_t count, void *dst)
{
	return transfer(lba, count, (uint8_t *) dst, 0);
}

static int ahci64_write(uint64_t lba, uint32_t count, const void *src)
{
	return transfer(lba, count, (uint8_t *) (uintptr_t) src, 1);
}

static uint64_t ahci64_sector_count(void)
{
	return sector_total;
}

static int identify(void)
{
	uint8_t *buffer;
	uint64_t total;
	int i;

	buffer = command_table + 256;	/* 같은 페이지 안의 임시 버퍼 */
	zero(buffer, BLOCK64_SECTOR_SIZE);
	if (run_command(ATA_CMD_IDENTIFY, 0, 0, buffer, BLOCK64_SECTOR_SIZE, 0) != 0) {
		return -1;
	}
	total = 0;
	for (i = 0; i < 4; i++) {	/* 워드 100..103: 48비트 섹터 수 */
		total |= (uint64_t) ((uint16_t) buffer[200 + i * 2] |
			((uint16_t) buffer[201 + i * 2] << 8)) << (i * 16);
	}
	if (total == 0) {		/* 없으면 28비트 값으로 */
		total = (uint64_t) buffer[120] | ((uint64_t) buffer[121] << 8) |
			((uint64_t) buffer[122] << 16) | ((uint64_t) buffer[123] << 24);
	}
	sector_total = total;
	return 0;
}

int ahci64_probe(void)
{
	uintptr_t page;
	uint32_t bdf;
	uint32_t command;
	uint32_t ports;
	uint32_t status;
	uint32_t i;

	if (ready != 0) {
		return 0;
	}
	bdf = pci64_find_class(0x01, 0x06);	/* 대용량 저장 장치, AHCI */
	if (bdf == PCI64_NONE) {
		return -1;
	}
	abar = (volatile uint8_t *) (uintptr_t) (pci64_read32(bdf, PCI64_REG_BAR5) & ~0xfu);
	if (abar == NULL) {
		return -1;
	}
	/* QEMU는 누가 켜 달라고 하기 전까지 버스 마스터링을 꺼 둔다. 켜지 않으면
	   DMA 전송이 아무 말 없이 아무것도 옮기지 않는다. */
	command = pci64_read32(bdf, PCI64_REG_COMMAND);
	pci64_write32(bdf, PCI64_REG_COMMAND,
		command | PCI64_COMMAND_MEMORY | PCI64_COMMAND_MASTER);

	page = memman64_alloc_4k(&memman64, MEMMAN64_PAGE_SIZE);
	if (page == 0 || (page & 0x3ff) != 0) {
		return -1;		/* 구조체들이 1KiB 경계에 맞아야 한다 */
	}
	/* 한 페이지에 셋을 다 넣는다. 명령 목록(1KiB, 1KiB 정렬), 받은 FIS
	   (256B, 256 정렬), 명령 테이블과 임시 버퍼(128 정렬). */
	command_list = (uint8_t *) page;
	received_fis = command_list + 1024;
	command_table = command_list + 1280;
	zero(command_list, MEMMAN64_PAGE_SIZE);

	reg_write(HBA_GHC, reg_read(HBA_GHC) | GHC_AE);
	reg_write(HBA_GHC, reg_read(HBA_GHC) & ~GHC_IE);
	ports = reg_read(HBA_PI);
	for (i = 0; i < 32; i++) {
		if ((ports & (1u << i)) == 0) {
			continue;
		}
		port_base = 0x100 + i * 0x80;
		status = port_read(PORT_SSTS);
		if ((status & 0x0f) != SSTS_DET_PRESENT) {
			continue;
		}
		if (port_read(PORT_SIG) != SIG_SATA) {
			continue;
		}
		if (port_stop() != 0) {
			continue;
		}
		port_write(PORT_CLB, (uint32_t) (uintptr_t) command_list);
		port_write(PORT_CLBU, (uint32_t) ((uint64_t) (uintptr_t) command_list >> 32));
		port_write(PORT_FB, (uint32_t) (uintptr_t) received_fis);
		port_write(PORT_FBU, (uint32_t) ((uint64_t) (uintptr_t) received_fis >> 32));
		port_write(PORT_SERR, port_read(PORT_SERR));
		port_start();
		if (identify() != 0) {
			port_stop();
			continue;
		}
		ready = 1;
		return 0;
	}
	return -1;
}

const struct BLOCK64_OPS ahci64_ops = {
	"ahci",
	ahci64_read,
	ahci64_write,
	ahci64_sector_count
};
