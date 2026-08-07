#include <asmfunc64.h>
#include <fd64.h>
#include <memory64.h>
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
#define ATA_CMD_READ   0x20
#define ATA_CMD_WRITE  0x30
#define ATA_CMD_FLUSH  0xe7

#define FD64_SECTOR_SIZE 512
/* 2 MiB. tools/mkfat12_64.py의 TOTAL_SECTORS, boot64.asm BPB의 total_sectors16과
   같아야 한다. 나머지 지오메트리는 부팅 때 BPB에서 읽는다. 이미지 전체를
   RAM에 올리므로 이 값이 곧 RAM 비용이다. */
#define FD64_IMAGE_SECTORS 4096

#define FAT12_EOC 0x0ff0
/* no RTC driver in src64 yet, so stamp every write with a fixed valid date
   (2026-01-01 00:00) -- host tools reject 0 as a directory timestamp. */
#define FD64_FIXED_DATE (((2026 - 1980) << 9) | (1 << 5) | 1)
#define FD64_FIXED_TIME 0

static uint8_t *disk_image;
static uint8_t *fat;
static struct FDINFO64 *root;
static uint8_t dirty[FD64_IMAGE_SECTORS / 8];
static uint16_t bytes_per_sector;
static uint8_t sectors_per_cluster;
static uint16_t reserved_sectors;
static uint8_t fat_count;
static uint16_t root_entries;
static uint16_t sectors_per_fat;
static uint32_t root_lba;
static uint32_t root_sectors;
static uint32_t data_lba;
static uint16_t max_cluster;
static int initialized;

static uint16_t read16(const uint8_t *p)
{
	return (uint16_t) p[0] | ((uint16_t) p[1] << 8);
}

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

static int ata_read_sector(uint32_t lba, uint8_t *dst)
{
	uint16_t i;
	uint16_t word;

	if (ata_wait_not_busy() != 0) {
		return -1;
	}
	io_out8(ATA_DRIVE, (uint8_t) (0xe0 | ((lba >> 24) & 0x0f)));
	io_out8(ATA_SECCOUNT, 1);
	io_out8(ATA_LBA_LOW, (uint8_t) lba);
	io_out8(ATA_LBA_MID, (uint8_t) (lba >> 8));
	io_out8(ATA_LBA_HIGH, (uint8_t) (lba >> 16));
	io_out8(ATA_COMMAND, ATA_CMD_READ);
	if (ata_wait_drq() != 0) {
		return -1;
	}
	for (i = 0; i < FD64_SECTOR_SIZE / 2; i++) {
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
	io_out8(ATA_DRIVE, (uint8_t) (0xe0 | ((lba >> 24) & 0x0f)));
	io_out8(ATA_SECCOUNT, 1);
	io_out8(ATA_LBA_LOW, (uint8_t) lba);
	io_out8(ATA_LBA_MID, (uint8_t) (lba >> 8));
	io_out8(ATA_LBA_HIGH, (uint8_t) (lba >> 16));
	io_out8(ATA_COMMAND, ATA_CMD_WRITE);
	if (ata_wait_drq() != 0) {
		return -1;
	}
	for (i = 0; i < FD64_SECTOR_SIZE / 2; i++) {
		ata_out16((uint16_t) src[i * 2] | ((uint16_t) src[i * 2 + 1] << 8));
	}
	if (ata_wait_not_busy() != 0) {
		return -1;
	}
	io_out8(ATA_COMMAND, ATA_CMD_FLUSH);
	return ata_wait_not_busy();
}

static void mark_dirty(const void *ptr, size_t size)
{
	uint32_t first;
	uint32_t last;
	uint32_t i;

	if (size == 0) {
		return;
	}
	first = (uint32_t) (((const uint8_t *) ptr - disk_image) / FD64_SECTOR_SIZE);
	last = (uint32_t) (((const uint8_t *) ptr + size - 1 - disk_image) / FD64_SECTOR_SIZE);
	for (i = first; i <= last && i < FD64_IMAGE_SECTORS; i++) {
		dirty[i / 8] |= (uint8_t) (1 << (i % 8));
	}
}

int fd64_sync(void)
{
	uint32_t i;
	int written;

	if (initialized == 0) {
		return -1;
	}
	written = 0;
	for (i = 0; i < FD64_IMAGE_SECTORS; i++) {
		if ((dirty[i / 8] & (1 << (i % 8))) == 0) {
			continue;
		}
		if (ata_write_sector(i, disk_image + i * FD64_SECTOR_SIZE) != 0) {
			return -1;
		}
		dirty[i / 8] &= (uint8_t) ~(1 << (i % 8));
		written++;
	}
	return written;
}

static int load_disk_image(void)
{
	uint32_t lba;

	disk_image = (uint8_t *) memman64_alloc_4k(&memman64, FD64_IMAGE_SECTORS * FD64_SECTOR_SIZE);
	if (disk_image == NULL) {
		return -1;
	}
	for (lba = 0; lba < FD64_IMAGE_SECTORS; lba++) {
		if (ata_read_sector(lba, disk_image + lba * FD64_SECTOR_SIZE) != 0) {
			return -1;
		}
	}
	return 0;
}

static void make_name83(uint8_t out[FD64_NAME_LEN], const char *name)
{
	uint32_t i;
	uint32_t j;
	char c;

	for (i = 0; i < FD64_NAME_LEN; i++) {
		out[i] = ' ';
	}
	j = 0;
	for (i = 0; j < FD64_NAME_LEN && name[i] != '\0'; i++) {
		c = name[i];
		if (c >= 'a' && c <= 'z') {
			c = (char) (c - 0x20);
		}
		if (c == '.') {
			j = 8;
		} else {
			out[j++] = (uint8_t) c;
		}
	}
}

static int name_eq83(const struct FDINFO64 *finfo, const uint8_t b[FD64_NAME_LEN])
{
	uint32_t i;
	uint8_t c;

	for (i = 0; i < FD64_NAME_LEN; i++) {
		c = i < 8 ? finfo->name[i] : finfo->ext[i - 8];
		if (c != b[i]) {
			return 0;
		}
	}
	return 1;
}

static uint8_t *cluster_data(uint16_t cluster)
{
	uint32_t lba;

	lba = data_lba + ((uint32_t) cluster - 2) * sectors_per_cluster;
	return disk_image + lba * bytes_per_sector;
}

static uint32_t cluster_bytes(void)
{
	return (uint32_t) bytes_per_sector * sectors_per_cluster;
}

static void fat_set(uint16_t cluster, uint16_t value)
{
	uint32_t offset;
	uint32_t copy;
	uint8_t *p;

	offset = cluster + cluster / 2;
	value &= 0x0fff;
	for (copy = 0; copy < fat_count; copy++) {
		p = disk_image + ((uint32_t) reserved_sectors + copy * sectors_per_fat) *
			bytes_per_sector + offset;
		if ((cluster & 1) != 0) {
			p[0] = (uint8_t) ((p[0] & 0x0f) | ((value << 4) & 0xf0));
			p[1] = (uint8_t) (value >> 4);
		} else {
			p[0] = (uint8_t) value;
			p[1] = (uint8_t) ((p[1] & 0xf0) | ((value >> 8) & 0x0f));
		}
		mark_dirty(p, 2);
	}
}

static uint16_t alloc_cluster(void)
{
	uint16_t c;

	/* ponytail: linear free-cluster scan from 2 every time; 2835 clusters
	   max on a 1.44M image, so a free-cluster hint only matters if the
	   image grows. */
	for (c = 2; c < max_cluster; c++) {
		if (fd64_next_cluster(c) == 0) {
			fat_set(c, 0x0fff);
			return c;
		}
	}
	return 0;
}

static void free_chain(uint16_t cluster)
{
	uint16_t next;

	while (cluster >= 2 && cluster < FAT12_EOC) {
		next = fd64_next_cluster(cluster);
		fat_set(cluster, 0);
		cluster = next;
	}
}

int fd64_init(void)
{
	const uint8_t *bpb;

	if (initialized != 0) {
		return 0;
	}
	if (load_disk_image() != 0) {
		return -1;
	}
	bpb = disk_image;
	bytes_per_sector = read16(bpb + 11);
	sectors_per_cluster = bpb[13];
	reserved_sectors = read16(bpb + 14);
	fat_count = bpb[16];
	root_entries = read16(bpb + 17);
	sectors_per_fat = read16(bpb + 22);
	if (bytes_per_sector != FD64_SECTOR_SIZE || sectors_per_cluster == 0 ||
			fat_count == 0 || root_entries == 0 || sectors_per_fat == 0) {
		return -1;
	}
	root_lba = reserved_sectors + (uint32_t) fat_count * sectors_per_fat;
	root_sectors = ((uint32_t) root_entries * 32 + bytes_per_sector - 1) / bytes_per_sector;
	data_lba = root_lba + root_sectors;
	fat = disk_image + (uint32_t) reserved_sectors * bytes_per_sector;
	root = (struct FDINFO64 *) (disk_image + root_lba * bytes_per_sector);
	max_cluster = (uint16_t) ((FD64_IMAGE_SECTORS - data_lba) / sectors_per_cluster + 2);
	initialized = 1;
	return 0;
}

uint32_t fd64_file_count(void)
{
	uint32_t i;
	uint32_t count;

	if (initialized == 0) {
		return 0;
	}
	count = 0;
	for (i = 0; i < root_entries; i++) {
		if (root[i].name[0] == 0x00) {
			break;
		}
		if (root[i].name[0] != 0xe5 && (root[i].type & 0x18) == 0) {
			count++;
		}
	}
	return count;
}

const struct FDINFO64 *fd64_file_at(uint32_t index)
{
	uint32_t i;
	uint32_t count;

	if (initialized == 0) {
		return NULL;
	}
	count = 0;
	for (i = 0; i < root_entries; i++) {
		if (root[i].name[0] == 0x00) {
			return NULL;
		}
		if (root[i].name[0] != 0xe5 && (root[i].type & 0x18) == 0) {
			if (count == index) {
				return &root[i];
			}
			count++;
		}
	}
	return NULL;
}

uint16_t fd64_next_cluster(uint16_t cluster)
{
	uint32_t offset;
	uint16_t value;

	offset = cluster + cluster / 2;
	value = (uint16_t) fat[offset] | ((uint16_t) fat[offset + 1] << 8);
	if ((cluster & 1) != 0) {
		value >>= 4;
	} else {
		value &= 0x0fff;
	}
	return value;
}

int fd64_open(struct FDHANDLE64 *fh, const char *name)
{
	uint8_t name83[FD64_NAME_LEN];
	uint32_t i;

	if (fh == NULL || initialized == 0) {
		return 0;
	}
	make_name83(name83, name);
	for (i = 0; i < root_entries; i++) {
		if (root[i].name[0] == 0x00) {
			break;
		}
		if (root[i].name[0] != 0xe5 && (root[i].type & 0x18) == 0 &&
				name_eq83(&root[i], name83) != 0) {
			fh->finfo = &root[i];
			fh->pos = 0;
			fh->cluster = root[i].clustno;
			return 1;
		}
	}
	fh->finfo = NULL;
	return 0;
}

size_t fd64_read(struct FDHANDLE64 *fh, void *dst, size_t request_size)
{
	uint8_t *out;
	size_t read_size;
	size_t offset;
	size_t left_in_cluster;
	size_t left_in_file;
	size_t chunk;
	const uint8_t *src;
	uint32_t i;

	if (fh == NULL || fh->finfo == NULL || dst == NULL) {
		return 0;
	}
	out = (uint8_t *) dst;
	read_size = 0;
	while (request_size > 0 && fh->pos < fh->finfo->size && fh->cluster < 0x0ff0) {
		offset = fh->pos % (bytes_per_sector * sectors_per_cluster);
		left_in_cluster = bytes_per_sector * sectors_per_cluster - offset;
		left_in_file = fh->finfo->size - fh->pos;
		chunk = request_size;
		if (chunk > left_in_cluster) {
			chunk = left_in_cluster;
		}
		if (chunk > left_in_file) {
			chunk = left_in_file;
		}
		src = cluster_data(fh->cluster) + offset;
		for (i = 0; i < chunk; i++) {
			out[i] = src[i];
		}
		out += chunk;
		fh->pos += (uint32_t) chunk;
		read_size += chunk;
		request_size -= chunk;
		if (offset + chunk == bytes_per_sector * sectors_per_cluster &&
				fh->pos < fh->finfo->size) {
			fh->cluster = fd64_next_cluster(fh->cluster);
		}
	}
	return read_size;
}

int fd64_seek(struct FDHANDLE64 *fh, int64_t offset, int whence)
{
	int64_t base;
	int64_t new_pos;
	uint32_t cluster_bytes;
	uint32_t skip_clusters;

	if (fh == NULL || fh->finfo == NULL) {
		return -1;
	}
	if (whence == 0) {
		base = 0;
	} else if (whence == 1) {
		base = fh->pos;
	} else if (whence == 2) {
		base = fh->finfo->size;
	} else {
		return -1;
	}
	new_pos = base + offset;
	if (new_pos < 0 || new_pos > (int64_t) fh->finfo->size) {
		return -1;
	}
	fh->pos = (uint32_t) new_pos;
	fh->cluster = fh->finfo->clustno;
	cluster_bytes = bytes_per_sector * sectors_per_cluster;
	if (cluster_bytes == 0) {
		return -1;
	}
	skip_clusters = fh->pos / cluster_bytes;
	while (skip_clusters-- > 0 && fh->cluster < 0x0ff0) {
		fh->cluster = fd64_next_cluster(fh->cluster);
	}
	if (fh->cluster >= 0x0ff0 && fh->pos < fh->finfo->size) {
		return -1;
	}
	return 0;
}

int fd64_create(struct FDHANDLE64 *fh, const char *name)
{
	uint8_t name83[FD64_NAME_LEN];
	struct FDINFO64 *entry;
	uint32_t i;
	uint32_t j;

	if (fh == NULL || initialized == 0) {
		return 0;
	}
	if (fd64_open(fh, name) != 0) {
		return fd64_truncate(fh, 0) == 0 ? 1 : 0;
	}
	make_name83(name83, name);
	entry = NULL;
	for (i = 0; i < root_entries; i++) {
		if (root[i].name[0] == 0x00 || root[i].name[0] == 0xe5) {
			entry = &root[i];
			break;
		}
	}
	if (entry == NULL) {
		/* root directory is fixed-size in FAT12: fail instead of clobbering */
		fh->finfo = NULL;
		return 0;
	}
	for (j = 0; j < sizeof(struct FDINFO64); j++) {
		((uint8_t *) entry)[j] = 0;
	}
	for (j = 0; j < FD64_NAME_LEN; j++) {
		if (j < 8) {
			entry->name[j] = name83[j];
		} else {
			entry->ext[j - 8] = name83[j];
		}
	}
	entry->type = 0x20;
	entry->time = FD64_FIXED_TIME;
	entry->date = FD64_FIXED_DATE;
	mark_dirty(entry, sizeof(struct FDINFO64));
	fh->finfo = entry;
	fh->pos = 0;
	fh->cluster = 0;
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
	uint16_t cluster;
	uint16_t next;

	if (fh == NULL || fh->finfo == NULL || src == NULL || initialized == 0 || size == 0) {
		return 0;
	}
	cb = cluster_bytes();
	if (fh->finfo->clustno == 0) {
		cluster = alloc_cluster();
		if (cluster == 0) {
			return 0;
		}
		fh->finfo->clustno = cluster;
		mark_dirty(fh->finfo, sizeof(struct FDINFO64));
	}
	/* ponytail: walk the chain from the start once per call to find the
	   cluster holding fh->pos; cache it in the handle if appends get hot. */
	cluster = fh->finfo->clustno;
	for (index = fh->pos / cb; index > 0; index--) {
		next = fd64_next_cluster(cluster);
		if (next < 2 || next >= FAT12_EOC) {
			next = alloc_cluster();
			if (next == 0) {
				return 0;
			}
			fat_set(cluster, next);
		}
		cluster = next;
	}
	fh->cluster = cluster;
	in = (const uint8_t *) src;
	written = 0;
	while (size > 0) {
		offset = fh->pos % cb;
		chunk = cb - offset;
		if (chunk > size) {
			chunk = size;
		}
		dst = cluster_data(fh->cluster) + offset;
		for (i = 0; i < chunk; i++) {
			dst[i] = in[i];
		}
		mark_dirty(dst, chunk);
		in += chunk;
		fh->pos += (uint32_t) chunk;
		written += chunk;
		size -= chunk;
		if (fh->pos > fh->finfo->size) {
			fh->finfo->size = fh->pos;
			fh->finfo->date = FD64_FIXED_DATE;
			fh->finfo->time = FD64_FIXED_TIME;
			mark_dirty(fh->finfo, sizeof(struct FDINFO64));
		}
		if (size > 0) {
			next = fd64_next_cluster(fh->cluster);
			if (next < 2 || next >= FAT12_EOC) {
				next = alloc_cluster();
				if (next == 0) {
					break;
				}
				fat_set(fh->cluster, next);
			}
			fh->cluster = next;
		}
	}
	/* ponytail: write through on every call -- no flush syscall, no data
	   lost to a QEMU kill. Batch behind an explicit fd64_sync() if PIO
	   write cost ever shows up. */
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
	uint16_t cluster;
	uint16_t next;

	if (fh == NULL || fh->finfo == NULL || initialized == 0 || size > fh->finfo->size) {
		return -1;
	}
	cb = cluster_bytes();
	keep = (size + cb - 1) / cb;
	cluster = fh->finfo->clustno;
	if (keep == 0) {
		fh->finfo->clustno = 0;
		free_chain(cluster);
	} else {
		for (i = 1; i < keep && cluster >= 2 && cluster < FAT12_EOC; i++) {
			cluster = fd64_next_cluster(cluster);
		}
		if (cluster >= 2 && cluster < FAT12_EOC) {
			next = fd64_next_cluster(cluster);
			fat_set(cluster, 0x0fff);
			free_chain(next);
		}
	}
	fh->finfo->size = size;
	fh->finfo->date = FD64_FIXED_DATE;
	fh->finfo->time = FD64_FIXED_TIME;
	mark_dirty(fh->finfo, sizeof(struct FDINFO64));
	fh->pos = 0;
	fh->cluster = fh->finfo->clustno;
	return fd64_sync() < 0 ? -1 : 0;
}
