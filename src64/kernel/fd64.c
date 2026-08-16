#include <block64.h>
#include <cache64.h>
#include <fd64.h>
#include <stddef.h>
#include <stdint.h>
#include <utf864.h>

#define FD64_SECTOR_SIZE BLOCK64_SECTOR_SIZE

/* FAT32: entries at or above this end a chain, and the top 4 bits of every
   entry are reserved and must survive a write. */
#define FAT32_EOC  0x0ffffff8
#define FAT32_MASK 0x0fffffff
#define FAT32_LAST 0x0fffffff
/* no RTC driver in src64 yet, so stamp every write with a fixed valid date
   (2026-01-01 00:00) -- host tools reject 0 as a directory timestamp. */
#define FD64_FIXED_DATE (((2026 - 1980) << 9) | (1 << 5) | 1)
#define FD64_FIXED_TIME 0

#define DIR_ENTRY_SIZE ((uint32_t) sizeof(struct FDINFO64))

static uint16_t bytes_per_sector;
static uint8_t sectors_per_cluster;
static uint32_t reserved_sectors;
static uint8_t fat_count;
static uint32_t sectors_per_fat;
static uint32_t root_cluster;
static uint32_t data_lba;
static uint32_t total_sectors;
static uint32_t max_cluster;
static uint32_t alloc_hint;
static int initialized;

static uint16_t read16(const uint8_t *p)
{
	return (uint16_t) p[0] | ((uint16_t) p[1] << 8);
}

static uint32_t read32(const uint8_t *p)
{
	return (uint32_t) read16(p) | ((uint32_t) read16(p + 2) << 16);
}

static uint32_t cluster_bytes(void)
{
	return (uint32_t) bytes_per_sector * sectors_per_cluster;
}

static uint32_t info_cluster(const struct FDINFO64 *info)
{
	return ((uint32_t) info->clustno_hi << 16) | (uint32_t) info->clustno;
}

static void info_set_cluster(struct FDINFO64 *info, uint32_t cluster)
{
	info->clustno = (uint16_t) cluster;
	info->clustno_hi = (uint16_t) (cluster >> 16);
}

static int cluster_valid(uint32_t cluster)
{
	return cluster >= 2 && cluster < FAT32_EOC;
}

/* Every accessor below returns a pointer into the cache that stays valid only
   until the next cache64_get. Copy what you need before calling again. */
static uint8_t *fat_entry(uint32_t copy, uint32_t cluster, int mode)
{
	uint32_t offset;
	uint8_t *p;

	/* a 4-byte entry never straddles a 512-byte sector */
	offset = cluster * 4;
	p = cache64_get(reserved_sectors + copy * sectors_per_fat +
		offset / bytes_per_sector, mode);
	return p == NULL ? NULL : p + offset % bytes_per_sector;
}

static struct FDINFO64 *dir_at(const struct FDPOS64 *pos, int mode)
{
	uint8_t *p;

	p = cache64_get(data_lba + (pos->cluster - 2) * sectors_per_cluster +
		pos->offset / bytes_per_sector, mode);
	return p == NULL ? NULL : (struct FDINFO64 *) (p + pos->offset % bytes_per_sector);
}

static uint8_t *cluster_sector(uint32_t cluster, uint32_t offset, int mode)
{
	uint8_t *p;

	p = cache64_get(data_lba + (cluster - 2) * sectors_per_cluster +
		offset / bytes_per_sector, mode);
	return p == NULL ? NULL : p + offset % bytes_per_sector;
}

int fd64_sync(void)
{
	int written;
	int n;
	uint32_t i;
	/* File data, then the FAT copies, then directory entries. A failure
	   between the FAT and the directory leaves orphan clusters, which fsck
	   repairs; the reverse order would leave a directory entry pointing at
	   an unwritten chain. The directory lives in the data area on FAT32, so
	   the ordering comes from the cache's metadata flag, not from the LBA. */
	const uint32_t starts[3] = { data_lba, 0, 0 };
	const uint32_t ends[3] = { 0xffffffff, data_lba, 0xffffffff };
	const int metas[3] = { 0, 0, 1 };

	if (initialized == 0) {
		return -1;
	}
	written = 0;
	for (i = 0; i < 3; i++) {
		n = cache64_flush(starts[i], ends[i], metas[i]);
		if (n < 0) {
			/* keep what reached the disk, drop the rest, report the error */
			cache64_discard_dirty();
			return -1;
		}
		written += n;
	}
	return written;
}

uint32_t fd64_next_cluster(uint32_t cluster)
{
	const uint8_t *p;

	if (cluster > max_cluster) {
		return FAT32_LAST;
	}
	p = fat_entry(0, cluster, CACHE64_READ);
	if (p == NULL) {
		return FAT32_LAST;
	}
	return read32(p) & FAT32_MASK;
}

static int fat_set(uint32_t cluster, uint32_t value)
{
	uint32_t copy;
	uint32_t old;
	uint8_t *p;

	if (cluster > max_cluster) {
		return -1;
	}
	for (copy = 0; copy < fat_count; copy++) {
		p = fat_entry(copy, cluster, CACHE64_WRITE);
		if (p == NULL) {
			return -1;
		}
		/* the top 4 bits of a FAT32 entry are reserved: preserve them */
		old = read32(p) & 0xf0000000;
		old |= value & FAT32_MASK;
		p[0] = (uint8_t) old;
		p[1] = (uint8_t) (old >> 8);
		p[2] = (uint8_t) (old >> 16);
		p[3] = (uint8_t) (old >> 24);
	}
	return 0;
}

static uint32_t alloc_cluster(void)
{
	uint32_t c;
	uint32_t scanned;

	/* ponytail: linear scan, but resuming from the last allocation instead
	   of from cluster 2 -- with 128046 clusters, restarting every time made
	   a multi-cluster write quadratic. */
	c = alloc_hint;
	for (scanned = 0; scanned <= max_cluster - 2; scanned++) {
		if (c > max_cluster) {
			c = 2;
		}
		if (fd64_next_cluster(c) == 0) {
			if (fat_set(c, FAT32_LAST) != 0) {
				return 0;
			}
			alloc_hint = c + 1;
			return c;
		}
		c++;
	}
	return 0;
}

static void free_chain(uint32_t cluster)
{
	uint32_t next;

	while (cluster_valid(cluster)) {
		next = fd64_next_cluster(cluster);
		if (fat_set(cluster, 0) != 0) {
			return;
		}
		if (cluster < alloc_hint) {
			alloc_hint = cluster;
		}
		cluster = next;
	}
}

static int zero_cluster(uint32_t cluster)
{
	uint32_t offset;
	uint32_t i;
	uint8_t *p;

	for (offset = 0; offset < cluster_bytes(); offset += bytes_per_sector) {
		p = cluster_sector(cluster, offset, CACHE64_WRITE_META);
		if (p == NULL) {
			return -1;
		}
		for (i = 0; i < bytes_per_sector; i++) {
			p[i] = 0;
		}
	}
	return 0;
}

/* Advances to the next directory slot, following the chain. Returns 0 at the
   end of the chain; `grow` links a fresh zeroed cluster instead. */
static int dir_advance(struct FDPOS64 *pos, int grow)
{
	uint32_t next;

	pos->offset += DIR_ENTRY_SIZE;
	if (pos->offset < cluster_bytes()) {
		return 1;
	}
	next = fd64_next_cluster(pos->cluster);
	if (cluster_valid(next) == 0) {
		if (grow == 0) {
			return 0;
		}
		next = alloc_cluster();
		if (next == 0 || zero_cluster(next) != 0 ||
				fat_set(pos->cluster, next) != 0) {
			return 0;
		}
	}
	pos->cluster = next;
	pos->offset = 0;
	return 1;
}

static void dir_first(struct FDPOS64 *pos)
{
	pos->cluster = root_cluster;
	pos->offset = 0;
}

/* ---- VFAT long names ----------------------------------------------------
   A long name is stored in the entries physically *before* its 8.3 entry, in
   reverse order: the entry carrying the last 13 UTF-16 units comes first and
   is flagged LFN_LAST. Every long entry repeats a checksum of the 8.3 name,
   which is what ties the two together. */

#define LFN_ATTR 0x0f
#define LFN_LAST 0x40
#define LFN_UNITS_PER_ENTRY 13
#define LFN_MAX_ENTRIES (FD64_LFN_MAX_UNITS / LFN_UNITS_PER_ENTRY)
/* NT case flags: an all-lowercase 8.3 name is stored upper case plus a hint,
   instead of spending long entries on it. Linux and Windows both honour it. */
#define NT_LOWER_BASE 0x08
#define NT_LOWER_EXT 0x10
/* bound on directory scanning, so a corrupt chain cannot loop forever */
#define DIR_SCAN_LIMIT 8192

struct LFN64_STATE {
	uint16_t units[FD64_LFN_MAX_UNITS];
	uint8_t checksum;
	uint8_t next_ord;
	int units_total;
};

static int is_file_entry(const struct FDINFO64 *finfo)
{
	/* 0x08 volume label, 0x10 directory, 0x0f long-name entry (0x0f & 0x18
	   is 0x08, so long entries are skipped too) */
	return finfo->name[0] != 0xe5 && (finfo->type & 0x18) == 0;
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

static void entry_name11(const struct FDINFO64 *finfo, uint8_t out[FD64_NAME_LEN])
{
	uint32_t i;

	for (i = 0; i < FD64_NAME_LEN; i++) {
		out[i] = i < 8 ? finfo->name[i] : finfo->ext[i - 8];
	}
}

static int name_eq83(const struct FDINFO64 *finfo, const uint8_t b[FD64_NAME_LEN])
{
	uint8_t name11[FD64_NAME_LEN];
	uint32_t i;

	entry_name11(finfo, name11);
	for (i = 0; i < FD64_NAME_LEN; i++) {
		if (name11[i] != b[i]) {
			return 0;
		}
	}
	return 1;
}

static char lower_ascii(char c)
{
	return c >= 'A' && c <= 'Z' ? (char) (c + 0x20) : c;
}

static int name_eq_ci(const char *a, const char *b)
{
	while (*a != '\0' && *b != '\0') {
		if (lower_ascii(*a) != lower_ascii(*b)) {
			return 0;
		}
		a++;
		b++;
	}
	return *a == *b;
}

static uint8_t short_checksum(const uint8_t name11[FD64_NAME_LEN])
{
	uint8_t sum;
	uint32_t i;

	sum = 0;
	for (i = 0; i < FD64_NAME_LEN; i++) {
		sum = (uint8_t) (((sum & 1) << 7) + (sum >> 1) + name11[i]);
	}
	return sum;
}

/* byte offset of long-name unit `i` inside a 32-byte entry */
static uint32_t lfn_unit_offset(int i)
{
	if (i < 5) {
		return 1 + (uint32_t) i * 2;
	}
	if (i < 11) {
		return 14 + ((uint32_t) i - 5) * 2;
	}
	return 28 + ((uint32_t) i - 11) * 2;
}

/* renders the 8.3 name as text, applying the NT lower-case hints */
static void short_name_text(const struct FDINFO64 *finfo, char *out, size_t out_size)
{
	size_t n;
	uint32_t i;
	char c;

	n = 0;
	for (i = 0; i < 8 && n + 1 < out_size; i++) {
		if (finfo->name[i] == ' ') {
			break;
		}
		c = (char) finfo->name[i];
		out[n++] = (finfo->reserved[0] & NT_LOWER_BASE) != 0 ? lower_ascii(c) : c;
	}
	if (finfo->ext[0] != ' ' && n + 1 < out_size) {
		out[n++] = '.';
		for (i = 0; i < 3 && n + 1 < out_size; i++) {
			if (finfo->ext[i] == ' ') {
				break;
			}
			c = (char) finfo->ext[i];
			out[n++] = (finfo->reserved[0] & NT_LOWER_EXT) != 0 ? lower_ascii(c) : c;
		}
	}
	out[n] = '\0';
}

static void lfn_reset(struct LFN64_STATE *st)
{
	st->units_total = 0;
	st->next_ord = 0;
}

static void lfn_collect(struct LFN64_STATE *st, const uint8_t *raw)
{
	uint32_t offset;
	int base;
	int i;
	uint8_t ord;

	ord = raw[0] & 0x3f;
	if ((raw[0] & LFN_LAST) != 0) {
		lfn_reset(st);
		if (ord == 0 || ord > LFN_MAX_ENTRIES) {
			return;		/* longer than this kernel accepts */
		}
		st->checksum = raw[13];
		st->units_total = ord * LFN_UNITS_PER_ENTRY;
		st->next_ord = ord;
	}
	if (st->units_total == 0 || ord != st->next_ord || raw[13] != st->checksum) {
		lfn_reset(st);
		return;
	}
	base = ((int) ord - 1) * LFN_UNITS_PER_ENTRY;
	for (i = 0; i < LFN_UNITS_PER_ENTRY; i++) {
		offset = lfn_unit_offset(i);
		st->units[base + i] = (uint16_t) raw[offset] |
			((uint16_t) raw[offset + 1] << 8);
	}
	st->next_ord = (uint8_t) (ord - 1);
}

/* 1 when a complete long name whose checksum matches `finfo` was collected */
static int lfn_finish(const struct LFN64_STATE *st, const struct FDINFO64 *finfo,
	char *out, size_t out_size)
{
	uint8_t name11[FD64_NAME_LEN];
	int units;

	if (st->units_total == 0 || st->next_ord != 0) {
		return 0;
	}
	entry_name11(finfo, name11);
	if (short_checksum(name11) != st->checksum) {
		return 0;
	}
	units = 0;
	while (units < st->units_total && st->units[units] != 0x0000) {
		units++;
	}
	return utf16_to_utf8_64(st->units, units, out, (int) out_size) > 0;
}

/* Returns the next real file entry at or after *pos, leaving *pos on the slot
   after it. `at` receives the 8.3 entry's location, `name` its long name when
   it has one and its 8.3 name otherwise. */
static int dir_scan_next(struct FDPOS64 *pos, struct FDINFO64 *out,
	struct FDPOS64 *at, char *name, size_t name_size)
{
	struct LFN64_STATE lfn;
	struct FDINFO64 entry;
	const struct FDINFO64 *e;
	uint32_t guard;

	lfn_reset(&lfn);
	for (guard = 0; guard < DIR_SCAN_LIMIT; guard++) {
		e = dir_at(pos, CACHE64_READ);
		if (e == NULL || e->name[0] == 0x00) {
			return 0;
		}
		if (e->name[0] != 0xe5 && e->type == LFN_ATTR) {
			lfn_collect(&lfn, (const uint8_t *) e);
		} else if (is_file_entry(e) != 0) {
			entry = *e;
			if (out != NULL) {
				*out = entry;
			}
			if (at != NULL) {
				*at = *pos;
			}
			if (name != NULL && name_size > 0) {
				if (lfn_finish(&lfn, &entry, name, name_size) == 0) {
					short_name_text(&entry, name, name_size);
				}
			}
			dir_advance(pos, 0);
			return 1;
		} else {
			lfn_reset(&lfn);
		}
		if (dir_advance(pos, 0) == 0) {
			return 0;
		}
	}
	return 0;
}

static int dir_write(struct FDHANDLE64 *fh)
{
	struct FDINFO64 *entry;

	entry = dir_at(&fh->dir, CACHE64_WRITE_META);
	if (entry == NULL) {
		return -1;
	}
	*entry = fh->info;
	return 0;
}

int fd64_init(void)
{
	const uint8_t *bpb;
	uint64_t device_sectors;
	uint32_t fat_capacity;

	if (initialized != 0) {
		return 0;
	}
	if (cache64_init() != 0) {
		return -1;
	}
	bpb = cache64_get(0, CACHE64_READ);
	if (bpb == NULL) {
		return -1;
	}
	bytes_per_sector = read16(bpb + 11);
	sectors_per_cluster = bpb[13];
	reserved_sectors = read16(bpb + 14);
	fat_count = bpb[16];
	total_sectors = read32(bpb + 32);
	sectors_per_fat = read32(bpb + 36);
	root_cluster = read32(bpb + 44);
	if (bytes_per_sector != FD64_SECTOR_SIZE || sectors_per_cluster == 0 ||
			fat_count == 0 || sectors_per_fat == 0 || root_cluster < 2 ||
			read16(bpb + 17) != 0 || read16(bpb + 22) != 0) {
		return -1;
	}
	data_lba = reserved_sectors + fat_count * sectors_per_fat;
	if (total_sectors <= data_lba) {
		return -1;
	}
	/* the BPB is on-disk data: refuse to mount a volume that claims more
	   sectors than the drive has, rather than writing past its end */
	device_sectors = block64_sector_count();
	if (device_sectors != 0 && total_sectors > device_sectors) {
		return -1;
	}
	max_cluster = (total_sectors - data_lba) / sectors_per_cluster + 1;
	/* never index past the FAT itself, however large the volume claims to be */
	fat_capacity = sectors_per_fat * (bytes_per_sector / 4) - 1;
	if (max_cluster > fat_capacity) {
		max_cluster = fat_capacity;
	}
	alloc_hint = 2;
	initialized = 1;
	return 0;
}

uint32_t fd64_file_count(void)
{
	struct FDPOS64 pos;
	uint32_t count;

	if (initialized == 0) {
		return 0;
	}
	count = 0;
	dir_first(&pos);
	while (dir_scan_next(&pos, NULL, NULL, NULL, 0) != 0) {
		count++;
	}
	return count;
}

/* ponytail: rescans from the first entry per call, so listing a directory is
   quadratic in its entry count; fine for the tens of files this OS holds,
   worth an iterator if a listing ever gets slow. */
int fd64_file_at(uint32_t index, struct FDINFO64 *out, char *name, size_t name_size)
{
	struct FDPOS64 pos;
	uint32_t count;

	if (initialized == 0) {
		return 0;
	}
	count = 0;
	dir_first(&pos);
	for (;;) {
		if (dir_scan_next(&pos, out, NULL, name, name_size) == 0) {
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
	uint8_t name83[FD64_NAME_LEN];
	char found[FD64_NAME_MAX];
	struct FDINFO64 entry;
	struct FDPOS64 pos;
	struct FDPOS64 at;

	if (fh == NULL || initialized == 0 || name == NULL) {
		return 0;
	}
	fh->dir.cluster = 0;
	make_name83(name83, name);
	dir_first(&pos);
	while (dir_scan_next(&pos, &entry, &at, found, sizeof(found)) != 0) {
		/* match the long name first, then the 8.3 name, so a file can be
		   opened by either spelling */
		if (name_eq_ci(found, name) != 0 || name_eq83(&entry, name83) != 0) {
			fh->info = entry;
			fh->dir = at;
			fh->pos = 0;
			fh->cluster = info_cluster(&fh->info);
			return 1;
		}
	}
	return 0;
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
	cb = cluster_bytes();
	out = (uint8_t *) dst;
	read_size = 0;
	while (request_size > 0 && fh->pos < fh->info.size && cluster_valid(fh->cluster)) {
		offset = fh->pos % cb;
		/* one cached sector at a time -- a cluster is not contiguous in RAM */
		chunk = request_size;
		limit = bytes_per_sector - offset % bytes_per_sector;
		if (chunk > limit) {
			chunk = limit;
		}
		limit = fh->info.size - fh->pos;
		if (chunk > limit) {
			chunk = limit;
		}
		src = cluster_sector(fh->cluster, offset, CACHE64_READ);
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
	fh->cluster = info_cluster(&fh->info);
	cb = cluster_bytes();
	if (cb == 0) {
		return -1;
	}
	skip_clusters = fh->pos / cb;
	while (skip_clusters-- > 0 && cluster_valid(fh->cluster)) {
		fh->cluster = fd64_next_cluster(fh->cluster);
	}
	if (cluster_valid(fh->cluster) == 0 && fh->pos < fh->info.size) {
		return -1;
	}
	return 0;
}

/* maps one character into the 8.3 charset; 0 when it has no representation */
static char shortname_char(char c)
{
	const char *ok = "$%'-_@~`!(){}^#&";
	uint32_t i;

	if (c >= 'a' && c <= 'z') {
		return (char) (c - 0x20);
	}
	if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) {
		return c;
	}
	for (i = 0; ok[i] != '\0'; i++) {
		if (c == ok[i]) {
			return c;
		}
	}
	return 0;
}

/* Builds the 8.3 name. *lossy is set when the result does not spell the
   original back, which is exactly when long entries are needed. */
static void make_shortname(uint8_t out[FD64_NAME_LEN], const char *name, int *lossy)
{
	uint32_t i;
	uint32_t dot;
	uint32_t n;
	char c;

	for (i = 0; i < FD64_NAME_LEN; i++) {
		out[i] = ' ';
	}
	*lossy = 0;
	dot = 0;
	for (i = 0; name[i] != '\0'; i++) {
		if (name[i] == '.') {
			dot = i;		/* last dot separates the extension */
		}
	}
	n = 0;
	for (i = 0; name[i] != '\0' && (dot == 0 || i < dot); i++) {
		c = shortname_char(name[i]);
		if (c == 0) {
			c = '_';
			*lossy = 1;
		}
		if (n < 8) {
			out[n++] = (uint8_t) c;
		} else {
			*lossy = 1;
		}
	}
	if (n == 0) {
		out[0] = '_';
		*lossy = 1;
	}
	if (dot == 0) {
		return;
	}
	n = 0;
	for (i = dot + 1; name[i] != '\0'; i++) {
		c = shortname_char(name[i]);
		if (c == 0) {
			c = '_';
			*lossy = 1;
		}
		if (n < 3) {
			out[8 + n++] = (uint8_t) c;
		} else {
			*lossy = 1;
		}
	}
}

static int name_eq(const char *a, const char *b)
{
	while (*a != '\0' && *b != '\0') {
		if (*a != *b) {
			return 0;
		}
		a++;
		b++;
	}
	return *a == *b;
}

/* NT_LOWER_* for whichever half of the name carries no upper-case ASCII */
static uint8_t shortname_case_flags(const char *name)
{
	uint8_t flags;
	uint32_t i;

	flags = NT_LOWER_BASE | NT_LOWER_EXT;
	for (i = 0; name[i] != '\0' && name[i] != '.'; i++) {
		if (name[i] >= 'A' && name[i] <= 'Z') {
			flags &= (uint8_t) ~NT_LOWER_BASE;
		}
	}
	for (; name[i] != '\0'; i++) {
		if (name[i] >= 'A' && name[i] <= 'Z') {
			flags &= (uint8_t) ~NT_LOWER_EXT;
		}
	}
	return flags;
}

static int shortname_taken(const uint8_t name11[FD64_NAME_LEN])
{
	struct FDINFO64 entry;
	struct FDPOS64 pos;

	dir_first(&pos);
	while (dir_scan_next(&pos, &entry, NULL, NULL, 0) != 0) {
		if (name_eq83(&entry, name11) != 0) {
			return 1;
		}
	}
	return 0;
}

/* rewrites the basis name as BASE~N until it is unique; 0 when it cannot be */
static int shortname_unique(uint8_t name11[FD64_NAME_LEN])
{
	uint8_t basis[FD64_NAME_LEN];
	uint32_t n;
	uint32_t digits;
	uint32_t at;
	uint32_t i;
	uint32_t value;

	for (i = 0; i < FD64_NAME_LEN; i++) {
		basis[i] = name11[i];
	}
	for (n = 1; n < 1000; n++) {
		digits = n < 10 ? 1 : (n < 100 ? 2 : 3);
		at = 8 - digits - 1;
		for (i = 0; i < 8; i++) {
			name11[i] = i < at ? basis[i] : ' ';
		}
		name11[at] = '~';
		value = n;
		for (i = 0; i < digits; i++) {
			name11[at + digits - i] = (uint8_t) ('0' + value % 10);
			value /= 10;
		}
		if (shortname_taken(name11) == 0) {
			return 1;
		}
	}
	return 0;
}

static int lfn_write_entry(const struct FDPOS64 *pos, const uint16_t *units,
	int units_total, int ord, int last, uint8_t checksum)
{
	uint8_t *raw;
	uint32_t offset;
	int i;
	int index;
	uint16_t unit;

	raw = (uint8_t *) dir_at(pos, CACHE64_WRITE_META);
	if (raw == NULL) {
		return -1;
	}
	for (i = 0; i < 32; i++) {
		raw[i] = 0;
	}
	raw[0] = (uint8_t) (ord | (last != 0 ? LFN_LAST : 0));
	raw[11] = LFN_ATTR;
	raw[13] = checksum;
	for (i = 0; i < LFN_UNITS_PER_ENTRY; i++) {
		index = (ord - 1) * LFN_UNITS_PER_ENTRY + i;
		if (index < units_total) {
			unit = units[index];
		} else if (index == units_total) {
			unit = 0x0000;		/* terminator */
		} else {
			unit = 0xffff;		/* padding */
		}
		offset = lfn_unit_offset(i);
		raw[offset] = (uint8_t) unit;
		raw[offset + 1] = (uint8_t) (unit >> 8);
	}
	return 0;
}

/* Finds `need` consecutive free slots, growing the directory chain when the
   run does not fit in what already exists. */
static int dir_find_run(struct FDPOS64 *start, uint32_t need)
{
	struct FDPOS64 pos;
	struct FDPOS64 run;
	const struct FDINFO64 *e;
	uint32_t have;
	uint32_t guard;

	dir_first(&pos);
	run = pos;
	have = 0;
	for (guard = 0; guard < DIR_SCAN_LIMIT; guard++) {
		e = dir_at(&pos, CACHE64_READ);
		if (e == NULL) {
			return 0;
		}
		if (e->name[0] == 0x00 || e->name[0] == 0xe5) {
			if (have == 0) {
				run = pos;
			}
			have++;
			if (have == need) {
				*start = run;
				return 1;
			}
		} else {
			have = 0;
		}
		if (dir_advance(&pos, 1) == 0) {
			return 0;
		}
	}
	return 0;
}

int fd64_create(struct FDHANDLE64 *fh, const char *name)
{
	uint16_t units[FD64_LFN_MAX_UNITS];
	uint8_t name11[FD64_NAME_LEN];
	char text[FD64_NAME_MAX];
	struct FDINFO64 probe;
	struct FDPOS64 pos;
	int units_total;
	int entries;
	int lossy;
	int ord;
	uint32_t j;

	if (fh == NULL || initialized == 0 || name == NULL || name[0] == '\0') {
		return 0;
	}
	if (fd64_open(fh, name) != 0) {
		return fd64_truncate(fh, 0) == 0 ? 1 : 0;
	}
	units_total = utf8_to_utf16_64(name, units, FD64_LFN_MAX_UNITS);
	if (units_total <= 0) {
		return 0;		/* empty, or longer than this kernel accepts */
	}
	make_shortname(name11, name, &lossy);
	for (j = 0; j < sizeof(struct FDINFO64); j++) {
		((uint8_t *) &fh->info)[j] = 0;
	}
	/* An 8.3 name that spells the request back exactly needs no long
	   entries; a pure case difference is carried by the NT hints instead. */
	entries = 0;
	if (lossy == 0) {
		probe = fh->info;
		for (j = 0; j < 8; j++) {
			probe.name[j] = name11[j];
		}
		for (j = 0; j < 3; j++) {
			probe.ext[j] = name11[8 + j];
		}
		probe.reserved[0] = shortname_case_flags(name);
		short_name_text(&probe, text, sizeof(text));
		if (name_eq(text, name) == 0) {
			lossy = 1;
		} else {
			fh->info.reserved[0] = probe.reserved[0];
		}
	}
	if (lossy != 0) {
		if (shortname_unique(name11) == 0) {
			return 0;
		}
		entries = (units_total + LFN_UNITS_PER_ENTRY - 1) / LFN_UNITS_PER_ENTRY;
	}
	if (dir_find_run(&pos, (uint32_t) entries + 1) == 0) {
		return 0;
	}
	for (ord = entries; ord >= 1; ord--) {
		if (lfn_write_entry(&pos, units, units_total, ord, ord == entries,
				short_checksum(name11)) != 0) {
			return 0;
		}
		if (dir_advance(&pos, 1) == 0) {
			return 0;
		}
	}
	for (j = 0; j < 8; j++) {
		fh->info.name[j] = name11[j];
	}
	for (j = 0; j < 3; j++) {
		fh->info.ext[j] = name11[8 + j];
	}
	fh->info.type = 0x20;
	fh->info.time = FD64_FIXED_TIME;
	fh->info.date = FD64_FIXED_DATE;
	fh->dir = pos;
	fh->pos = 0;
	fh->cluster = 0;
	if (dir_write(fh) != 0) {
		fh->dir.cluster = 0;
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
			initialized == 0 || size == 0) {
		return 0;
	}
	cb = cluster_bytes();
	if (info_cluster(&fh->info) == 0) {
		cluster = alloc_cluster();
		if (cluster == 0) {
			return 0;
		}
		info_set_cluster(&fh->info, cluster);
	}
	/* ponytail: walk the chain from the start once per call to find the
	   cluster holding fh->pos; cache it in the handle if appends get hot. */
	cluster = info_cluster(&fh->info);
	for (index = fh->pos / cb; index > 0; index--) {
		next = fd64_next_cluster(cluster);
		if (cluster_valid(next) == 0) {
			next = alloc_cluster();
			if (next == 0 || fat_set(cluster, next) != 0) {
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
		chunk = bytes_per_sector - offset % bytes_per_sector;
		if (chunk > size) {
			chunk = size;
		}
		dst = cluster_sector(fh->cluster, offset, CACHE64_WRITE);
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
			if (cluster_valid(next) == 0) {
				next = alloc_cluster();
				if (next == 0 || fat_set(fh->cluster, next) != 0) {
					break;
				}
			}
			fh->cluster = next;
		}
	}
	fh->info.date = FD64_FIXED_DATE;
	fh->info.time = FD64_FIXED_TIME;
	if (dir_write(fh) != 0) {
		return 0;
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
	uint32_t cluster;
	uint32_t next;

	if (fh == NULL || fh->dir.cluster == 0 || initialized == 0 ||
			size > fh->info.size) {
		return -1;
	}
	cb = cluster_bytes();
	keep = (size + cb - 1) / cb;
	cluster = info_cluster(&fh->info);
	if (keep == 0) {
		info_set_cluster(&fh->info, 0);
		free_chain(cluster);
	} else {
		for (i = 1; i < keep && cluster_valid(cluster); i++) {
			cluster = fd64_next_cluster(cluster);
		}
		if (cluster_valid(cluster) != 0) {
			next = fd64_next_cluster(cluster);
			if (fat_set(cluster, FAT32_LAST) != 0) {
				return -1;
			}
			free_chain(next);
		}
	}
	fh->info.size = size;
	fh->info.date = FD64_FIXED_DATE;
	fh->info.time = FD64_FIXED_TIME;
	fh->pos = 0;
	fh->cluster = info_cluster(&fh->info);
	if (dir_write(fh) != 0) {
		return -1;
	}
	return fd64_sync() < 0 ? -1 : 0;
}
