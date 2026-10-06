/*
 * fd64.c -- FAT32 마운트와 공개 파일 API
 *
 * FAT/클러스터 관리는 fd64_fat.c, VFAT 이름과 루트 디렉터리 반복은
 * fd64_dir.c가 맡는다. 공개 ABI는 fd64.h에만 둔다.
 */
#include <block64.h>
#include <cache64.h>
#include <fd64.h>
#include "fd64_internal.h"
#include <stddef.h>
#include <stdint.h>

uint16_t fd64_bytes_per_sector;
uint8_t fd64_sectors_per_cluster;
uint32_t fd64_reserved_sectors;
uint8_t fd64_fat_count;
uint32_t fd64_sectors_per_fat;
uint32_t fd64_root_cluster;
uint32_t fd64_data_lba;
uint32_t fd64_total_sectors;
uint32_t fd64_max_cluster;
uint32_t fd64_alloc_hint;
int fd64_initialized;

int fd64_sync(void)
{
	int written;
	int n;
	uint32_t i;
	/* 파일 데이터, FAT 사본, 디렉터리 항목 순서로 내보낸다. FAT과 디렉터리
	   사이에서 실패하면 주인 없는 클러스터가 남고 이건 fsck가 고칠 수 있다.
	   순서를 뒤집으면 아직 쓰이지 않은 사슬을 가리키는 디렉터리 항목이 남는다.
	   FAT32에서는 디렉터리도 데이터 영역에 있으므로, 이 순서는 LBA가 아니라
	   캐시의 metadata 표시에서 나온다. */
	const uint32_t starts[3] = { fd64_data_lba, 0, 0 };
	const uint32_t ends[3] = { 0xffffffff, fd64_data_lba, 0xffffffff };
	const int metas[3] = { 0, 0, 1 };

	if (fd64_initialized == 0) {
		return -1;
	}
	written = 0;
	for (i = 0; i < 3; i++) {
		n = cache64_flush(starts[i], ends[i], metas[i]);
		if (n < 0) {
			/* 실패한 블록과 아직 시도하지 않은 블록은 다음 sync에서
			   재시도할 수 있도록 dirty로 둔다. */
			return -1;
		}
		written += n;
	}
	return written;
}

int fd64_init(void)
{
	const uint8_t *bpb;
	uint64_t device_sectors;
	uint32_t fat_capacity;

	if (fd64_initialized != 0) {
		return 0;
	}
	if (cache64_init() != 0) {
		return -1;
	}
	bpb = cache64_get(0, CACHE64_READ);
	if (bpb == NULL) {
		return -1;
	}
	fd64_bytes_per_sector = fd64_read16(bpb + 11);
	fd64_sectors_per_cluster = bpb[13];
	fd64_reserved_sectors = fd64_read16(bpb + 14);
	fd64_fat_count = bpb[16];
	fd64_total_sectors = fd64_read32(bpb + 32);
	fd64_sectors_per_fat = fd64_read32(bpb + 36);
	fd64_root_cluster = fd64_read32(bpb + 44);
	if (fd64_bytes_per_sector != FD64_SECTOR_SIZE ||
			fd64_sectors_per_cluster == 0 || fd64_fat_count == 0 ||
			fd64_sectors_per_fat == 0 || fd64_root_cluster < 2 ||
			fd64_read16(bpb + 17) != 0 || fd64_read16(bpb + 22) != 0) {
		return -1;
	}
	fd64_data_lba = fd64_reserved_sectors +
		fd64_fat_count * fd64_sectors_per_fat;
	if (fd64_total_sectors <= fd64_data_lba) {
		return -1;
	}
	/* BPB가 드라이브보다 큰 볼륨을 주장하면 끝을 넘어 쓰기 전에 거절한다. */
	device_sectors = block64_sector_count();
	if (device_sectors != 0 && fd64_total_sectors > device_sectors) {
		return -1;
	}
	fd64_max_cluster = (fd64_total_sectors - fd64_data_lba) /
		fd64_sectors_per_cluster + 1;
	/* 볼륨이 아무리 크다고 우겨도 FAT 자체를 넘어서 접근하지 않는다. */
	fat_capacity = fd64_sectors_per_fat * (fd64_bytes_per_sector / 4) - 1;
	if (fd64_max_cluster > fat_capacity) {
		fd64_max_cluster = fat_capacity;
	}
	fd64_alloc_hint = 2;
	fd64_initialized = 1;
	return 0;
}

uint32_t fd64_file_count(void)
{
	struct FDPOS64 pos;
	uint32_t count;

	if (fd64_initialized == 0) {
		return 0;
	}
	count = 0;
	fd64_dir_first(&pos);
	while (fd64_dir_scan_next(&pos, NULL, NULL, NULL, 0) != 0) {
		count++;
	}
	return count;
}

/* 부를 때마다 첫 항목부터 다시 훑는다. 현재 이미지의 수십 개 파일에는
   충분하며, 더 큰 디렉터리를 지원할 때 공개 반복자 API를 추가한다. */
int fd64_file_at(uint32_t index, struct FDINFO64 *out, char *name,
	size_t name_size)
{
	struct FDPOS64 pos;
	uint32_t count;

	if (fd64_initialized == 0) {
		return 0;
	}
	count = 0;
	fd64_dir_first(&pos);
	for (;;) {
		if (fd64_dir_scan_next(&pos, out, NULL, name, name_size) == 0) {
			return 0;
		}
		if (count == index) {
			return 1;
		}
		count++;
	}
}

int fd64_open(struct FDHANDLE64 *fh, const char *name)
{
	struct FDINFO64 entry;
	struct FDPOS64 at;

	if (fh == NULL || fd64_initialized == 0 || name == NULL) {
		return 0;
	}
	fh->dir.cluster = 0;
	if (fd64_dir_find(name, &entry, &at) == 0) {
		return 0;
	}
	fh->info = entry;
	fh->dir = at;
	fh->pos = 0;
	fh->cluster = fd64_info_cluster(&fh->info);
	return 1;
}

size_t fd64_read(struct FDHANDLE64 *fh, void *dst, size_t request_size)
{
	uint8_t *out;
	size_t read_size;
	size_t chunk;
	size_t limit;
	uint32_t offset;
	uint32_t cb;
	uint32_t i;
	const uint8_t *src;

	if (fh == NULL || fh->dir.cluster == 0 || dst == NULL) {
		return 0;
	}
	cb = fd64_cluster_bytes();
	out = (uint8_t *) dst;
	read_size = 0;
	while (request_size > 0 && fh->pos < fh->info.size &&
			fd64_cluster_valid(fh->cluster)) {
		offset = fh->pos % cb;
		chunk = request_size;
		limit = fd64_bytes_per_sector - offset % fd64_bytes_per_sector;
		if (chunk > limit) {
			chunk = limit;
		}
		limit = fh->info.size - fh->pos;
		if (chunk > limit) {
			chunk = limit;
		}
		src = fd64_cluster_sector(fh->cluster, offset, CACHE64_READ);
		if (src == NULL) {
			break;
		}
		for (i = 0; i < chunk; i++) {
			out[i] = src[i];
		}
		out += chunk;
		fh->pos += (uint32_t) chunk;
		read_size += chunk;
		request_size -= chunk;
		if (offset + chunk == cb && fh->pos < fh->info.size) {
			fh->cluster = fd64_next_cluster(fh->cluster);
		}
	}
	return read_size;
}

int fd64_seek(struct FDHANDLE64 *fh, int64_t offset, int whence)
{
	int64_t base;
	int64_t new_pos;
	uint32_t cb;
	uint32_t skip_clusters;

	if (fh == NULL || fh->dir.cluster == 0) {
		return -1;
	}
	if (whence == 0) {
		base = 0;
	} else if (whence == 1) {
		base = fh->pos;
	} else if (whence == 2) {
		base = fh->info.size;
	} else {
		return -1;
	}
	new_pos = base + offset;
	if (new_pos < 0 || new_pos > (int64_t) fh->info.size) {
		return -1;
	}
	fh->pos = (uint32_t) new_pos;
	fh->cluster = fd64_info_cluster(&fh->info);
	cb = fd64_cluster_bytes();
	if (cb == 0) {
		return -1;
	}
	skip_clusters = fh->pos / cb;
	while (skip_clusters-- > 0 && fd64_cluster_valid(fh->cluster)) {
		fh->cluster = fd64_next_cluster(fh->cluster);
	}
	if (fd64_cluster_valid(fh->cluster) == 0 &&
			fh->pos < fh->info.size) {
		return -1;
	}
	return 0;
}

int fd64_create(struct FDHANDLE64 *fh, const char *name)
{
	if (fh == NULL || fd64_initialized == 0 || name == NULL ||
			name[0] == '\0') {
		return 0;
	}
	if (fd64_open(fh, name) != 0) {
		return fd64_truncate(fh, 0) == 0 ? 1 : 0;
	}
	if (fd64_dir_create(fh, name) == 0) {
		return 0;
	}
	return fd64_sync() < 0 ? 0 : 1;
}

size_t fd64_write(struct FDHANDLE64 *fh, const void *src, size_t size)
{
	const uint8_t *in;
	uint8_t *dst;
	size_t written;
	size_t chunk;
	uint32_t cb;
	uint32_t index;
	uint32_t offset;
	uint32_t i;
	uint32_t cluster;
	uint32_t next;

	if (fh == NULL || fh->dir.cluster == 0 || src == NULL ||
			fd64_initialized == 0 || size == 0) {
		return 0;
	}
	cb = fd64_cluster_bytes();
	if (fd64_info_cluster(&fh->info) == 0) {
		cluster = fd64_alloc_cluster();
		if (cluster == 0) {
			return 0;
		}
		fd64_info_set_cluster(&fh->info, cluster);
	}
	/* fh->pos가 든 클러스터를 찾으려고 부를 때마다 사슬을 처음부터
	   따라간다. 이어 쓰기가 잦아지면 핸들에 이 값을 기억해 두면 된다. */
	cluster = fd64_info_cluster(&fh->info);
	for (index = fh->pos / cb; index > 0; index--) {
		next = fd64_next_cluster(cluster);
		if (fd64_cluster_valid(next) == 0) {
			next = fd64_alloc_cluster();
			if (next == 0 || fd64_fat_set(cluster, next) != 0) {
				return 0;
			}
		}
		cluster = next;
	}
	fh->cluster = cluster;
	in = (const uint8_t *) src;
	written = 0;
	while (size > 0) {
		offset = fh->pos % cb;
		chunk = fd64_bytes_per_sector - offset % fd64_bytes_per_sector;
		if (chunk > size) {
			chunk = size;
		}
		dst = fd64_cluster_sector(fh->cluster, offset, CACHE64_WRITE);
		if (dst == NULL) {
			break;
		}
		for (i = 0; i < chunk; i++) {
			dst[i] = in[i];
		}
		in += chunk;
		fh->pos += (uint32_t) chunk;
		written += chunk;
		size -= chunk;
		if (fh->pos > fh->info.size) {
			fh->info.size = fh->pos;
		}
		if (size > 0 && offset + chunk == cb) {
			next = fd64_next_cluster(fh->cluster);
			if (fd64_cluster_valid(next) == 0) {
				next = fd64_alloc_cluster();
				if (next == 0 || fd64_fat_set(fh->cluster, next) != 0) {
					break;
				}
			}
			fh->cluster = next;
		}
	}
	fh->info.date = FD64_FIXED_DATE;
	fh->info.time = FD64_FIXED_TIME;
	if (fd64_dir_write(fh) != 0) {
		return 0;
	}
	/* 공개 flush 호출이 없어, 쓰기는 디렉터리 갱신까지 즉시 동기화한다. */
	if (fd64_sync() < 0) {
		return 0;
	}
	return written;
}

int fd64_truncate(struct FDHANDLE64 *fh, uint32_t size)
{
	uint32_t cb;
	uint32_t keep;
	uint32_t i;
	uint32_t cluster;
	uint32_t next;

	if (fh == NULL || fh->dir.cluster == 0 || fd64_initialized == 0 ||
			size > fh->info.size) {
		return -1;
	}
	cb = fd64_cluster_bytes();
	keep = (size + cb - 1) / cb;
	cluster = fd64_info_cluster(&fh->info);
	if (keep == 0) {
		fd64_info_set_cluster(&fh->info, 0);
		fd64_free_chain(cluster);
	} else {
		for (i = 1; i < keep && fd64_cluster_valid(cluster); i++) {
			cluster = fd64_next_cluster(cluster);
		}
		if (fd64_cluster_valid(cluster) != 0) {
			next = fd64_next_cluster(cluster);
			if (fd64_fat_set(cluster, FD64_FAT_LAST) != 0) {
				return -1;
			}
			fd64_free_chain(next);
		}
	}
	fh->info.size = size;
	fh->info.date = FD64_FIXED_DATE;
	fh->info.time = FD64_FIXED_TIME;
	fh->pos = 0;
	fh->cluster = fd64_info_cluster(&fh->info);
	if (fd64_dir_write(fh) != 0) {
		return -1;
	}
	return fd64_sync() < 0 ? -1 : 0;
}
