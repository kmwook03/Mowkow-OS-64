/*
 * block64.c -- 저장 장치 전송 계층 고르기와 파티션 시작 위치
 *
 * 위쪽(캐시, 파일 시스템)은 파일 시스템 기준 LBA만 쓰고, 어느 컨트롤러가
 * 응답했는지는 끝까지 모른다. 전송 계층을 하나 더 붙이려면 struct
 * BLOCK64_OPS만 채우면 되고 위쪽은 그대로 둔다.
 */
#include <block64.h>
#ifdef __aarch64__
#include <arch/arch64.h>
#endif
#include <stddef.h>
#include <stdint.h>

static const struct BLOCK64_OPS *ops;
static uint64_t part_base;

#ifdef __aarch64__
static struct BLOCK64_OPS aarch64_ops;

/* The AArch64 image is linked at its physical load address, then executes
   through a TTBR1 high-half alias. Pointers stored by static initializers keep
   the low link-time value, so rebase every pointer in an ops table before the
   first indirect call. */
static uintptr_t kernel_pointer64(uintptr_t address)
{
	if (address < (uintptr_t) ARCH64_KERNEL_VA_BASE) {
		return arch64_phys_to_virt(address);
	}
	return address;
}

static void select_sdhci64(void)
{
	aarch64_ops.name = (const char *) kernel_pointer64(
		(uintptr_t) sdhci64_ops.name);
	aarch64_ops.read = (int (*)(uint64_t, uint32_t, void *)) kernel_pointer64(
		(uintptr_t) sdhci64_ops.read);
	aarch64_ops.write = (int (*)(uint64_t, uint32_t, const void *))
		kernel_pointer64((uintptr_t) sdhci64_ops.write);
	aarch64_ops.sector_count = (uint64_t (*)(void)) kernel_pointer64(
		(uintptr_t) sdhci64_ops.sector_count);
	ops = (const struct BLOCK64_OPS *) kernel_pointer64(
		(uintptr_t) &aarch64_ops);
}
#endif

static uint32_t read32(const uint8_t *p)
{
	return (uint32_t) p[0] | ((uint32_t) p[1] << 8) |
		((uint32_t) p[2] << 16) | ((uint32_t) p[3] << 24);
}

static int looks_like_fat(const uint8_t *sector)
{
	/* 파일 시스템 종류 문자열: FAT12/16은 0x36, FAT32는 0x52에 있다. */
	return (sector[54] == 'F' && sector[55] == 'A' && sector[56] == 'T') ||
		(sector[82] == 'F' && sector[83] == 'A' && sector[84] == 'T');
}

/* 0번 섹터에 MBR이 있으면 거기서 part_base를 정한다. 우리가 만드는 이미지는
   슈퍼플로피(LBA 0부터 파일 시스템)라서 보통은 0으로 설정한다. */
static void find_partition(void)
{
	uint8_t sector[BLOCK64_SECTOR_SIZE];
	const uint8_t *entry;
	uint32_t start;
	uint32_t i;

	part_base = 0;
	if (ops->read(0, 1, sector) != 0) {
		return;
	}
	if (sector[510] != 0x55 || sector[511] != 0xaa) {
		return;
	}
	for (i = 0; i < 4; i++) {
		entry = sector + 446 + i * 16;
		start = read32(entry + 8);
		if (entry[4] == 0x00 || start == 0) {
			continue;
		}
		/* 주의: 그 자리에 진짜 파일 시스템이 있을 때만 믿는다.
		   우리 부트 섹터도 0x55aa로 끝나므로 MBR로 잘못 읽으면 안 된다. */
		if (ops->read(start, 1, sector) != 0 || looks_like_fat(sector) == 0) {
			continue;
		}
		part_base = start;
		return;
	}
}

int block64_init(void)
{
	if (ops != NULL) {
		return 0;
	}
#ifdef __aarch64__
	if (sdhci64_probe() != 0) {
		return -1;
	}
	select_sdhci64();
#else
	ops = ahci64_probe() == 0 ? &ahci64_ops : &ata64_ops;
#endif
	find_partition();
	return 0;
}

const char *block64_transport(void)
{
	return ops == NULL ? "none" : ops->name;
}

uint64_t block64_part_base(void)
{
	return part_base;
}

uint64_t block64_sector_count(void)
{
	uint64_t total;

	if (ops == NULL) {
		return 0;
	}
	total = ops->sector_count();
	if (total <= part_base) {
		return 0;
	}
	return total - part_base;
}

int block64_read(uint64_t lba, uint32_t count, void *dst)
{
	if (ops == NULL || dst == NULL) {
		return -1;
	}
	return ops->read(part_base + lba, count, dst);
}

int block64_write(uint64_t lba, uint32_t count, const void *src)
{
	if (ops == NULL || src == NULL) {
		return -1;
	}
	return ops->write(part_base + lba, count, src);
}
