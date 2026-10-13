/*
 * fd64.c -- FAT32 마운트와 공개 파일 API
 *
 * FAT/클러스터 관리는 fd64_fat.c, VFAT 이름과 루트 디렉터리 반복은
 * fd64_dir.c가 맡는다. 공개 ABI는 fd64.h에만 둔다.
 */
#include <block64.h>
#include <cache64.h>
#include <fd64.h>
#include <mutex64.h>
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
int fd64_read_only;
static struct MUTEX64 fd64_mutex;

static int fd64_truncate_unlocked(struct FDHANDLE64 *fh, uint32_t size);

void fd64_mark_read_only(void)
{
	fd64_read_only = 1;
}

static int fd64_flush_ordered(int *written_out)
{
	int written;
	int n;
	uint32_t i;
	const uint32_t starts[3] = { fd64_data_lba, 0, 0 };
	const uint32_t ends[3] = { UINT32_MAX, fd64_data_lba, UINT32_MAX };
	const int metas[3] = { 0, 0, 1 };

	written = 0;
	*written_out = 0;
	for (i = 0; i < sizeof(starts) / sizeof(starts[0]); i++) {
		n = cache64_flush(starts[i], ends[i], metas[i]);
		if (n < 0) {
			*written_out = written;
			return -1;
		}
		written += n;
	}
	*written_out = written;
	return 0;
}

static int fd64_sync_unlocked(void)
{
	/* 파일 데이터, FAT 사본, 디렉터리 항목 순서로 내보낸다. FAT과 디렉터리
	   사이에서 실패하면 주인 없는 클러스터가 남고 이건 fsck가 고칠 수 있다.
	   순서를 뒤집으면 아직 쓰이지 않은 사슬을 가리키는 디렉터리 항목이 남는다.
	   FAT32에서는 디렉터리도 데이터 영역에 있으므로, 이 순서는 LBA가 아니라
	   캐시의 metadata 표시에서 나온다. */
	int first_written;
	int second_written;

	if (fd64_initialized == 0) {
		return -1;
	}
	if (fd64_flush_ordered(&first_written) == 0) {
		return first_written;
	}
	/* 실패한 dirty block은 보존된다. 전체 순서를 처음부터 한 번 재시도하고,
	   같은 오류가 반복되면 더 이상 안전한 변경을 받지 않는다. */
	if (fd64_flush_ordered(&second_written) != 0) {
		fd64_mark_read_only();
		return -1;
	}
	return first_written + second_written;
}

static int fd64_init_unlocked(void)
{
	const uint8_t *bpb;
	uint16_t bytes_per_sector;
	uint16_t reserved_sectors;
	uint8_t sectors_per_cluster;
	uint8_t fat_count;
	uint64_t device_sectors;
	uint64_t fat_sectors;
	uint64_t data_lba;
	uint64_t data_sectors;
	uint64_t cluster_count;
	uint64_t max_cluster;
	uint64_t fat_entries;
	uint32_t total_sectors;
	uint32_t sectors_per_fat;
	uint32_t root_cluster;

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
	bytes_per_sector = fd64_read16(bpb + 11);
	sectors_per_cluster = bpb[13];
	reserved_sectors = fd64_read16(bpb + 14);
	fat_count = bpb[16];
	total_sectors = fd64_read32(bpb + 32);
	sectors_per_fat = fd64_read32(bpb + 36);
	root_cluster = fd64_read32(bpb + 44);
	if (bpb[510] != 0x55 || bpb[511] != 0xaa ||
			bytes_per_sector != FD64_SECTOR_SIZE ||
			sectors_per_cluster == 0 || sectors_per_cluster > 128 ||
			(sectors_per_cluster & (sectors_per_cluster - 1)) != 0 ||
			reserved_sectors == 0 || fat_count == 0 ||
			sectors_per_fat == 0 || total_sectors == 0 ||
			root_cluster < 2 || fd64_read16(bpb + 17) != 0 ||
			fd64_read16(bpb + 19) != 0 ||
			fd64_read16(bpb + 22) != 0 ||
			fd64_read16(bpb + 42) != 0) {
		return -1;
	}
	/* BPB 필드는 작아도 곱셈 결과는 32비트를 넘을 수 있다. 캐시 LBA로
	   줄이기 전에 전체 geometry를 64비트로 계산하고 볼륨 안인지 확인한다. */
	fat_sectors = (uint64_t) fat_count * sectors_per_fat;
	data_lba = (uint64_t) reserved_sectors + fat_sectors;
	if (data_lba >= total_sectors || data_lba > UINT32_MAX) {
		return -1;
	}
	/* BPB가 드라이브보다 큰 볼륨을 주장하면 끝을 넘어 쓰기 전에 거절한다. */
	device_sectors = block64_sector_count();
	if (device_sectors != 0 && total_sectors > device_sectors) {
		return -1;
	}
	data_sectors = (uint64_t) total_sectors - data_lba;
	cluster_count = data_sectors / sectors_per_cluster;
	max_cluster = cluster_count + 1;
	fat_entries = (uint64_t) sectors_per_fat *
		(bytes_per_sector / sizeof(uint32_t));
	if (cluster_count < FD64_MIN_DATA_CLUSTERS ||
			max_cluster >= FD64_FAT_RESERVED ||
			fat_entries <= max_cluster || root_cluster > max_cluster) {
		return -1;
	}

	/* 실패한 mount가 부분 geometry를 노출하지 않도록 검증 뒤에만 게시한다. */
	fd64_bytes_per_sector = bytes_per_sector;
	fd64_sectors_per_cluster = sectors_per_cluster;
	fd64_reserved_sectors = reserved_sectors;
	fd64_fat_count = fat_count;
	fd64_sectors_per_fat = sectors_per_fat;
	fd64_root_cluster = root_cluster;
	fd64_data_lba = (uint32_t) data_lba;
	fd64_total_sectors = total_sectors;
	fd64_max_cluster = (uint32_t) max_cluster;
	fd64_alloc_hint = 2;
	fd64_read_only = 0;
	if (fd64_chain_validate(fd64_root_cluster, 1) != 0) {
		fd64_bytes_per_sector = 0;
		fd64_sectors_per_cluster = 0;
		fd64_reserved_sectors = 0;
		fd64_fat_count = 0;
		fd64_sectors_per_fat = 0;
		fd64_root_cluster = 0;
		fd64_data_lba = 0;
		fd64_total_sectors = 0;
		fd64_max_cluster = 0;
		fd64_alloc_hint = 0;
		fd64_read_only = 0;
		return -1;
	}
	fd64_initialized = 1;
	return 0;
}

static int fd64_file_chain_valid(const struct FDINFO64 *info)
{
	uint32_t cluster_bytes;
	uint32_t required;

	cluster_bytes = fd64_cluster_bytes();
	if (cluster_bytes == 0) {
		return 0;
	}
	required = info->size / cluster_bytes;
	if (info->size % cluster_bytes != 0) {
		required++;
	}
	return fd64_chain_validate(fd64_info_cluster(info), required) == 0;
}

static uint32_t fd64_file_count_unlocked(void)
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
static int fd64_file_at_unlocked(uint32_t index, struct FDINFO64 *out, char *name,
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

static int fd64_open_unlocked(struct FDHANDLE64 *fh, const char *name)
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
	if (fd64_file_chain_valid(&entry) == 0) {
		return 0;
	}
	fh->info = entry;
	fh->dir = at;
	fh->pos = 0;
	fh->cluster = fd64_info_cluster(&fh->info);
	return 1;
}

static size_t fd64_read_unlocked(struct FDHANDLE64 *fh, void *dst,
	size_t request_size)
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
	if (fd64_file_chain_valid(&fh->info) == 0) {
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
			fh->cluster = fd64_next_cluster_unlocked(fh->cluster);
		}
	}
	return read_size;
}

static int fd64_seek_unlocked(struct FDHANDLE64 *fh, int64_t offset,
	int whence)
{
	int64_t base;
	int64_t new_pos;
	uint32_t cb;
	uint32_t skip_clusters;

	if (fh == NULL || fh->dir.cluster == 0) {
		return -1;
	}
	if (fd64_file_chain_valid(&fh->info) == 0) {
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
	/* 범위를 먼저 비교해 base + offset의 signed overflow를 피한다. */
	if (offset < -base || offset > (int64_t) fh->info.size - base) {
		return -1;
	}
	new_pos = base + offset;
	fh->pos = (uint32_t) new_pos;
	fh->cluster = fd64_info_cluster(&fh->info);
	cb = fd64_cluster_bytes();
	if (cb == 0) {
		return -1;
	}
	skip_clusters = fh->pos / cb;
	while (skip_clusters-- > 0 && fd64_cluster_valid(fh->cluster)) {
		fh->cluster = fd64_next_cluster_unlocked(fh->cluster);
	}
	if (fd64_cluster_valid(fh->cluster) == 0 &&
			fh->pos < fh->info.size) {
		return -1;
	}
	return 0;
}

static int fd64_create_unlocked(struct FDHANDLE64 *fh, const char *name)
{
	if (fh == NULL || fd64_initialized == 0 || fd64_read_only != 0 ||
			name == NULL ||
			name[0] == '\0') {
		return 0;
	}
	if (fd64_open_unlocked(fh, name) != 0) {
		return fd64_truncate_unlocked(fh, 0) == 0 ? 1 : 0;
	}
	if (fd64_dir_create(fh, name) == 0) {
		return 0;
	}
	return fd64_sync_unlocked() < 0 ? 0 : 1;
}

struct FDWRITE_GROW64 {
	uint32_t parent;
	uint32_t first;
	uint32_t old_parent;
};

static int fd64_write_grow(uint32_t parent, uint32_t old_parent,
	struct FDWRITE_GROW64 *growth, uint32_t *cluster)
{
	uint32_t next;

	next = fd64_alloc_cluster();
	if (next == 0) {
		return -1;
	}
	if (parent != 0 && fd64_fat_set(parent, next) != 0) {
		if (fd64_free_chain(next) != 0) {
			fd64_mark_read_only();
		}
		return -1;
	}
	if (growth->first == 0) {
		growth->parent = parent;
		growth->first = next;
		growth->old_parent = old_parent;
	}
	*cluster = next;
	return 0;
}

static int fd64_write_growth_rollback(struct FDWRITE_GROW64 *growth)
{
	if (growth->first == 0) {
		return 0;
	}
	if (growth->parent != 0 &&
			fd64_fat_set(growth->parent, growth->old_parent) != 0) {
		fd64_mark_read_only();
		return -1;
	}
	if (fd64_free_chain(growth->first) != 0) {
		fd64_mark_read_only();
		return -1;
	}
	growth->first = 0;
	return 0;
}

static size_t fd64_write_unlocked(struct FDHANDLE64 *fh, const void *src,
	size_t size)
{
	struct FDHANDLE64 original;
	struct FDWRITE_GROW64 growth;
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
	int write_error;

	if (fh == NULL || fh->dir.cluster == 0 || src == NULL ||
			fd64_initialized == 0 || size == 0) {
		return 0;
	}
	if (fd64_read_only != 0) {
		return 0;
	}
	/* FAT directory의 파일 크기는 uint32_t다. 끝 위치를 계산하기 전에
	   검사해 4 GiB 경계에서 fh->pos가 되감기지 않게 한다. */
	if (size > (size_t) (UINT32_MAX - fh->pos)) {
		return 0;
	}
	if (fd64_file_chain_valid(&fh->info) == 0) {
		return 0;
	}
	original = *fh;
	growth.parent = 0;
	growth.first = 0;
	growth.old_parent = 0;
	write_error = 0;
	cb = fd64_cluster_bytes();
	if (fd64_info_cluster(&fh->info) == 0) {
		if (fd64_write_grow(0, 0, &growth, &cluster) != 0) {
			return 0;
		}
		fd64_info_set_cluster(&fh->info, cluster);
	}
	/* fh->pos가 든 클러스터를 찾으려고 부를 때마다 사슬을 처음부터
	   따라간다. 이어 쓰기가 잦아지면 핸들에 이 값을 기억해 두면 된다. */
	cluster = fd64_info_cluster(&fh->info);
	for (index = fh->pos / cb; index > 0; index--) {
		next = fd64_next_cluster_unlocked(cluster);
		if (fd64_cluster_valid(next) == 0) {
			if (fd64_write_grow(cluster, next, &growth, &next) != 0) {
				fd64_write_growth_rollback(&growth);
				*fh = original;
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
			write_error = 1;
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
			next = fd64_next_cluster_unlocked(fh->cluster);
			if (fd64_cluster_valid(next) == 0) {
				if (fd64_write_grow(fh->cluster, next, &growth,
						&next) != 0) {
					write_error = 1;
					break;
				}
			}
			fh->cluster = next;
		}
	}
	if (write_error != 0 && written == 0) {
		fd64_write_growth_rollback(&growth);
		*fh = original;
		return 0;
	}
	fh->info.date = FD64_FIXED_DATE;
	fh->info.time = FD64_FIXED_TIME;
	if (fd64_dir_write(fh) != 0) {
		fd64_write_growth_rollback(&growth);
		*fh = original;
		return 0;
	}
	/* 공개 flush 호출이 없어, 쓰기는 디렉터리 갱신까지 즉시 동기화한다. */
	if (fd64_sync_unlocked() < 0) {
		return 0;
	}
	return written;
}

static int fd64_truncate_unlocked(struct FDHANDLE64 *fh, uint32_t size)
{
	struct FDHANDLE64 old_handle;
	uint32_t cb;
	uint32_t keep;
	uint32_t i;
	uint32_t cluster;
	uint32_t next;

	if (fh == NULL || fh->dir.cluster == 0 || fd64_initialized == 0 ||
			fd64_read_only != 0 ||
			size > fh->info.size) {
		return -1;
	}
	if (fd64_file_chain_valid(&fh->info) == 0) {
		return -1;
	}
	old_handle = *fh;
	cb = fd64_cluster_bytes();
	keep = size / cb;
	if (size % cb != 0) {
		keep++;
	}
	cluster = fd64_info_cluster(&fh->info);
	if (keep == 0) {
		fd64_info_set_cluster(&fh->info, 0);
	} else {
		for (i = 1; i < keep && fd64_cluster_valid(cluster); i++) {
			cluster = fd64_next_cluster_unlocked(cluster);
		}
	}
	next = fd64_cluster_valid(cluster) != 0 ?
		fd64_next_cluster_unlocked(cluster) : FD64_FAT_LAST;
	fh->info.size = size;
	fh->info.date = FD64_FIXED_DATE;
	fh->info.time = FD64_FIXED_TIME;
	fh->pos = 0;
	fh->cluster = fd64_info_cluster(&fh->info);
	if (fd64_dir_write(fh) != 0) {
		*fh = old_handle;
		return -1;
	}
	/* 축소된 directory를 먼저 확정하면 이후 FAT 해제가 실패해도 이미
	   해제된 cluster를 가리키는 항목 대신 fsck 가능한 orphan만 남는다. */
	if (fd64_sync_unlocked() < 0) {
		return -1;
	}
	if (keep == 0) {
		cluster = fd64_info_cluster(&old_handle.info);
		if (fd64_free_chain(cluster) != 0) {
			fd64_mark_read_only();
			return -1;
		}
	} else if (fd64_cluster_valid(next) != 0) {
		if (fd64_fat_set(cluster, FD64_FAT_LAST) != 0 ||
				fd64_free_chain(next) != 0) {
			fd64_mark_read_only();
			return -1;
		}
	} else {
		return 0;
	}
	return fd64_sync_unlocked() < 0 ? -1 : 0;
}

/* Public filesystem operations are task-context only. Lock order is
   fd64_mutex -> cache64 -> block64; lower layers never call back into fd64.
   Holding the mutex for the complete operation also bounds every cache64_get
   pointer: no competing filesystem access can evict its direct-mapped block
   before the caller has copied or updated the pointed-to bytes. */
int fd64_init(void)
{
	int status;

	if (mutex64_lock(&fd64_mutex) != 0) {
		return -1;
	}
	status = fd64_init_unlocked();
	mutex64_unlock(&fd64_mutex);
	return status;
}

int fd64_sync(void)
{
	int status;

	if (mutex64_lock(&fd64_mutex) != 0) {
		return -1;
	}
	status = fd64_sync_unlocked();
	mutex64_unlock(&fd64_mutex);
	return status;
}

uint32_t fd64_file_count(void)
{
	uint32_t count;

	if (mutex64_lock(&fd64_mutex) != 0) {
		return 0;
	}
	count = fd64_file_count_unlocked();
	mutex64_unlock(&fd64_mutex);
	return count;
}

int fd64_file_at(uint32_t index, struct FDINFO64 *out, char *name,
	size_t name_size)
{
	int status;

	if (mutex64_lock(&fd64_mutex) != 0) {
		return 0;
	}
	status = fd64_file_at_unlocked(index, out, name, name_size);
	mutex64_unlock(&fd64_mutex);
	return status;
}

int fd64_open(struct FDHANDLE64 *fh, const char *name)
{
	int status;

	if (mutex64_lock(&fd64_mutex) != 0) {
		return 0;
	}
	status = fd64_open_unlocked(fh, name);
	mutex64_unlock(&fd64_mutex);
	return status;
}

size_t fd64_read(struct FDHANDLE64 *fh, void *dst, size_t request_size)
{
	size_t size;

	if (mutex64_lock(&fd64_mutex) != 0) {
		return 0;
	}
	size = fd64_read_unlocked(fh, dst, request_size);
	mutex64_unlock(&fd64_mutex);
	return size;
}

int fd64_seek(struct FDHANDLE64 *fh, int64_t offset, int whence)
{
	int status;

	if (mutex64_lock(&fd64_mutex) != 0) {
		return -1;
	}
	status = fd64_seek_unlocked(fh, offset, whence);
	mutex64_unlock(&fd64_mutex);
	return status;
}

uint32_t fd64_next_cluster(uint32_t cluster)
{
	uint32_t next;

	if (mutex64_lock(&fd64_mutex) != 0) {
		return 0;
	}
	next = fd64_next_cluster_unlocked(cluster);
	mutex64_unlock(&fd64_mutex);
	return next;
}

int fd64_create(struct FDHANDLE64 *fh, const char *name)
{
	int status;

	if (mutex64_lock(&fd64_mutex) != 0) {
		return 0;
	}
	status = fd64_create_unlocked(fh, name);
	mutex64_unlock(&fd64_mutex);
	return status;
}

size_t fd64_write(struct FDHANDLE64 *fh, const void *src, size_t size)
{
	size_t written;

	if (mutex64_lock(&fd64_mutex) != 0) {
		return 0;
	}
	written = fd64_write_unlocked(fh, src, size);
	mutex64_unlock(&fd64_mutex);
	return written;
}

int fd64_truncate(struct FDHANDLE64 *fh, uint32_t size)
{
	int status;

	if (mutex64_lock(&fd64_mutex) != 0) {
		return -1;
	}
	status = fd64_truncate_unlocked(fh, size);
	mutex64_unlock(&fd64_mutex);
	return status;
}
