/*
 * block64.c -- 저장 장치 전송 계층 고르기와 파티션 시작 위치
 *
 * 위쪽(캐시, 파일 시스템)은 파일 시스템 기준 LBA만 쓰고, 어느 컨트롤러가
 * 응답했는지는 끝까지 모른다. 전송 계층을 하나 더 붙이려면 struct
 * BLOCK64_OPS만 채우면 되고 위쪽은 그대로 둔다.
 */
#include <block64.h>
#include <stddef.h>
#include <stdint.h>

static const struct BLOCK64_OPS *ops;
static uint64_t part_base;

static int range_valid(uint64_t lba, uint32_t count, uint64_t limit)
{
	if (lba > UINT64_MAX - (uint64_t) count) {
		return 0;
	}
	if (limit != 0 && (lba >= limit || (uint64_t) count > limit - lba)) {
		return 0;
	}
	return 1;
}

static int translate_request(uint64_t lba, uint32_t count, uint64_t *physical)
{
	uint64_t total;

	if (range_valid(lba, count, 0) == 0 || part_base > UINT64_MAX - lba) {
		return -1;
	}
	*physical = part_base + lba;
	total = ops->sector_count();
	if (range_valid(*physical, count, total) == 0) {
		return -1;
	}
	return 0;
}

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
	uint64_t total;
	uint32_t start;
	uint32_t i;

	part_base = 0;
	total = ops->sector_count();
	if (range_valid(0, 1, total) == 0) {
		return;
	}
	if (ops->read(0, 1, sector) != 0) {
		return;
	}
	if (sector[510] != 0x55 || sector[511] != 0xaa) {
		return;
	}
	for (i = 0; i < 4; i++) {
		entry = sector + 446 + i * 16;
		start = read32(entry + 8);
		if (entry[4] == 0x00 || start == 0 ||
				range_valid(start, 1, total) == 0) {
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
	ops = &sdhci64_ops;
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
	uint64_t physical;

	if (ops == NULL) {
		return -1;
	}
	if (count == 0) {
		return 0;
	}
	if (dst == NULL || translate_request(lba, count, &physical) != 0) {
		return -1;
	}
	return ops->read(physical, count, dst);
}

int block64_write(uint64_t lba, uint32_t count, const void *src)
{
	uint64_t physical;

	if (ops == NULL) {
		return -1;
	}
	if (count == 0) {
		return 0;
	}
	if (src == NULL || translate_request(lba, count, &physical) != 0) {
		return -1;
	}
	return ops->write(physical, count, src);
}
