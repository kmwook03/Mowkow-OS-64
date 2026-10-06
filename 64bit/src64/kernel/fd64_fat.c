/*
 * fd64_fat.c -- FAT32 클러스터 사슬과 데이터 영역 접근
 *
 * 아래로는 cache64(되쓰기 섹터 캐시)를 쓰고, 그 아래가 block64다.
 * 반환된 캐시 포인터는 다음 cache64_get 호출 전까지만 유효하다.
 */
#include <cache64.h>
#include "fd64_internal.h"
#include <stddef.h>
#include <stdint.h>

uint16_t fd64_read16(const uint8_t *p)
{
	return (uint16_t) p[0] | ((uint16_t) p[1] << 8);
}

uint32_t fd64_read32(const uint8_t *p)
{
	return (uint32_t) fd64_read16(p) |
		((uint32_t) fd64_read16(p + 2) << 16);
}

uint32_t fd64_cluster_bytes(void)
{
	return (uint32_t) fd64_bytes_per_sector * fd64_sectors_per_cluster;
}

uint32_t fd64_info_cluster(const struct FDINFO64 *info)
{
	return ((uint32_t) info->clustno_hi << 16) | (uint32_t) info->clustno;
}

void fd64_info_set_cluster(struct FDINFO64 *info, uint32_t cluster)
{
	info->clustno = (uint16_t) cluster;
	info->clustno_hi = (uint16_t) (cluster >> 16);
}

int fd64_cluster_valid(uint32_t cluster)
{
	return cluster >= 2 && cluster < FD64_FAT_EOC;
}

static uint8_t *fd64_fat_entry(uint32_t copy, uint32_t cluster, int mode)
{
	uint32_t offset;
	uint8_t *p;

	/* 4바이트짜리 항목은 512바이트 섹터 경계에 걸치지 않는다. */
	offset = cluster * 4;
	p = cache64_get(fd64_reserved_sectors + copy * fd64_sectors_per_fat +
		offset / fd64_bytes_per_sector, mode);
	return p == NULL ? NULL : p + offset % fd64_bytes_per_sector;
}

struct FDINFO64 *fd64_dir_at(const struct FDPOS64 *pos, int mode)
{
	uint8_t *p;

	p = cache64_get(fd64_data_lba +
		(pos->cluster - 2) * fd64_sectors_per_cluster +
		pos->offset / fd64_bytes_per_sector, mode);
	return p == NULL ? NULL :
		(struct FDINFO64 *) (p + pos->offset % fd64_bytes_per_sector);
}

uint8_t *fd64_cluster_sector(uint32_t cluster, uint32_t offset, int mode)
{
	uint8_t *p;

	p = cache64_get(fd64_data_lba +
		(cluster - 2) * fd64_sectors_per_cluster +
		offset / fd64_bytes_per_sector, mode);
	return p == NULL ? NULL : p + offset % fd64_bytes_per_sector;
}

uint32_t fd64_next_cluster(uint32_t cluster)
{
	const uint8_t *p;

	if (cluster > fd64_max_cluster) {
		return FD64_FAT_LAST;
	}
	p = fd64_fat_entry(0, cluster, CACHE64_READ);
	if (p == NULL) {
		return FD64_FAT_LAST;
	}
	return fd64_read32(p) & FD64_FAT_MASK;
}

int fd64_fat_set(uint32_t cluster, uint32_t value)
{
	uint32_t copy;
	uint32_t old;
	uint8_t *p;

	if (cluster > fd64_max_cluster) {
		return -1;
	}
	for (copy = 0; copy < fd64_fat_count; copy++) {
		p = fd64_fat_entry(copy, cluster, CACHE64_WRITE);
		if (p == NULL) {
			return -1;
		}
		/* FAT32 항목의 위쪽 4비트는 예약된 자리다. 그대로 살려 둔다. */
		old = fd64_read32(p) & 0xf0000000;
		old |= value & FD64_FAT_MASK;
		p[0] = (uint8_t) old;
		p[1] = (uint8_t) (old >> 8);
		p[2] = (uint8_t) (old >> 16);
		p[3] = (uint8_t) (old >> 24);
	}
	return 0;
}

uint32_t fd64_alloc_cluster(void)
{
	uint32_t c;
	uint32_t scanned;

	/* 지난번 할당 위치부터 이어서 찾아 여러 클러스터 쓰기의 제곱 탐색을
	   피한다. */
	c = fd64_alloc_hint;
	for (scanned = 0; scanned <= fd64_max_cluster - 2; scanned++) {
		if (c > fd64_max_cluster) {
			c = 2;
		}
		if (fd64_next_cluster(c) == 0) {
			if (fd64_fat_set(c, FD64_FAT_LAST) != 0) {
				return 0;
			}
			fd64_alloc_hint = c + 1;
			return c;
		}
		c++;
	}
	return 0;
}

void fd64_free_chain(uint32_t cluster)
{
	uint32_t next;

	while (fd64_cluster_valid(cluster)) {
		next = fd64_next_cluster(cluster);
		if (fd64_fat_set(cluster, 0) != 0) {
			return;
		}
		if (cluster < fd64_alloc_hint) {
			fd64_alloc_hint = cluster;
		}
		cluster = next;
	}
}

int fd64_zero_cluster(uint32_t cluster)
{
	uint32_t offset;
	uint32_t i;
	uint8_t *p;

	for (offset = 0; offset < fd64_cluster_bytes();
			offset += fd64_bytes_per_sector) {
		p = fd64_cluster_sector(cluster, offset, CACHE64_WRITE_META);
		if (p == NULL) {
			return -1;
		}
		for (i = 0; i < fd64_bytes_per_sector; i++) {
			p[i] = 0;
		}
	}
	return 0;
}
