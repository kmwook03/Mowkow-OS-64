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
	return cluster >= 2 && cluster <= fd64_max_cluster;
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

uint32_t fd64_next_cluster_unlocked(uint32_t cluster)
{
	const uint8_t *p;

	if (fd64_cluster_valid(cluster) == 0) {
		return FD64_FAT_LAST;
	}
	p = fd64_fat_entry(0, cluster, CACHE64_READ);
	if (p == NULL) {
		return FD64_FAT_LAST;
	}
	return fd64_read32(p) & FD64_FAT_MASK;
}

/* 정상 data cluster면 1, chain 끝이면 0, 손상이나 I/O 실패면 -1. */
static int fd64_fat_next_checked(uint32_t cluster, uint32_t *next)
{
	const uint8_t *p;
	uint32_t value;

	if (next == NULL || fd64_cluster_valid(cluster) == 0) {
		return -1;
	}
	p = fd64_fat_entry(0, cluster, CACHE64_READ);
	if (p == NULL) {
		return -1;
	}
	value = fd64_read32(p) & FD64_FAT_MASK;
	if (value >= FD64_FAT_EOC) {
		*next = FD64_FAT_LAST;
		return 0;
	}
	if (fd64_cluster_valid(value) == 0) {
		return -1;
	}
	*next = value;
	return 1;
}

int fd64_chain_init(struct FDCHAIN64 *chain, uint32_t first)
{
	if (chain == NULL || fd64_cluster_valid(first) == 0 ||
			fd64_max_cluster < 2) {
		return -1;
	}
	chain->current = first;
	chain->hare = first;
	chain->remaining = fd64_max_cluster - 1;
	return 0;
}

int fd64_chain_advance(struct FDCHAIN64 *chain, uint32_t *next)
{
	uint32_t hare_next;
	int status;

	if (chain == NULL || next == NULL || chain->remaining == 0) {
		return -1;
	}
	status = fd64_fat_next_checked(chain->current, next);
	if (status <= 0) {
		return status;
	}
	/* data cluster 수보다 많은 유효 링크는 반드시 cycle 또는 손상이다. */
	if (chain->remaining == 1) {
		return -1;
	}
	chain->remaining--;
	chain->current = *next;

	if (fd64_cluster_valid(chain->hare) != 0) {
		status = fd64_fat_next_checked(chain->hare, &hare_next);
		if (status < 0) {
			return -1;
		}
		chain->hare = status == 0 ? FD64_FAT_LAST : hare_next;
	}
	if (fd64_cluster_valid(chain->hare) != 0) {
		status = fd64_fat_next_checked(chain->hare, &hare_next);
		if (status < 0) {
			return -1;
		}
		chain->hare = status == 0 ? FD64_FAT_LAST : hare_next;
	}
	if (fd64_cluster_valid(chain->hare) != 0 &&
			chain->current == chain->hare) {
		return -1;
	}
	return 1;
}

int fd64_chain_validate(uint32_t first, uint32_t required_clusters)
{
	struct FDCHAIN64 chain;
	uint32_t count;
	uint32_t next;
	int status;

	if (first == 0) {
		return required_clusters == 0 ? 0 : -1;
	}
	if (required_clusters > fd64_max_cluster - 1 ||
			fd64_chain_init(&chain, first) != 0) {
		return -1;
	}
	count = 1;
	for (;;) {
		status = fd64_chain_advance(&chain, &next);
		if (status < 0) {
			return -1;
		}
		if (status == 0) {
			return count >= required_clusters ? 0 : -1;
		}
		count++;
	}
}

int fd64_fat_set(uint32_t cluster, uint32_t value)
{
	uint32_t old_values[UINT8_MAX];
	uint32_t copy;
	uint32_t rollback;
	uint32_t old;
	uint8_t *p;

	if (fd64_cluster_valid(cluster) == 0) {
		return -1;
	}
	/* 어느 FAT 사본도 바꾸기 전에 모두 읽어 둬 중간 실패를 되돌린다. */
	for (copy = 0; copy < fd64_fat_count; copy++) {
		p = fd64_fat_entry(copy, cluster, CACHE64_READ);
		if (p == NULL) {
			return -1;
		}
		old_values[copy] = fd64_read32(p);
	}
	for (copy = 0; copy < fd64_fat_count; copy++) {
		p = fd64_fat_entry(copy, cluster, CACHE64_WRITE);
		if (p == NULL) {
			for (rollback = 0; rollback < copy; rollback++) {
				p = fd64_fat_entry(rollback, cluster, CACHE64_WRITE);
				if (p == NULL) {
					fd64_mark_read_only();
					return -1;
				}
				old = old_values[rollback];
				p[0] = (uint8_t) old;
				p[1] = (uint8_t) (old >> 8);
				p[2] = (uint8_t) (old >> 16);
				p[3] = (uint8_t) (old >> 24);
			}
			return -1;
		}
		/* FAT32 항목의 위쪽 4비트는 예약된 자리다. 그대로 살려 둔다. */
		old = old_values[copy] & 0xf0000000;
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

	if (fd64_read_only != 0) {
		return 0;
	}
	/* 지난번 할당 위치부터 이어서 찾아 여러 클러스터 쓰기의 제곱 탐색을
	   피한다. */
	c = fd64_alloc_hint;
	for (scanned = 0; scanned <= fd64_max_cluster - 2; scanned++) {
		if (c > fd64_max_cluster) {
			c = 2;
		}
		if (fd64_next_cluster_unlocked(c) == 0) {
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

int fd64_free_chain(uint32_t cluster)
{
	uint32_t next;
	uint32_t remaining;
	int status;

	if (cluster == 0) {
		return 0;
	}
	/* 손상된 사슬을 일부만 해제하지 않도록 쓰기 전에 전체를 확인한다. */
	if (fd64_chain_validate(cluster, 1) != 0) {
		return -1;
	}
	remaining = fd64_max_cluster - 1;
	while (fd64_cluster_valid(cluster) != 0 && remaining-- > 0) {
		status = fd64_fat_next_checked(cluster, &next);
		if (status < 0) {
			return -1;
		}
		if (fd64_fat_set(cluster, 0) != 0) {
			return -1;
		}
		if (cluster < fd64_alloc_hint) {
			fd64_alloc_hint = cluster;
		}
		if (status == 0) {
			return 0;
		}
		cluster = next;
	}
	return -1;
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
