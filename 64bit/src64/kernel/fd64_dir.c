/*
 * fd64_dir.c -- FAT32 루트 디렉터리와 VFAT 긴 이름
 *
 * 디렉터리는 루트 하나뿐이다. 하위 디렉터리는 만들지도, 따라가지도 않는다.
 */
#include <cache64.h>
#include "fd64_internal.h"
#include <stddef.h>
#include <stdint.h>
#include <utf864.h>

/* 사슬을 따라 다음 디렉터리 자리로 넘어간다. 사슬 끝이면 0을 돌려주고,
   `grow`가 참이면 대신 0으로 채운 클러스터를 새로 이어 붙인다. */
static int dir_advance(struct FDPOS64 *pos, int grow)
{
	uint32_t next;

	pos->offset += FD64_DIR_ENTRY_SIZE;
	if (pos->offset < fd64_cluster_bytes()) {
		return 1;
	}
	next = fd64_next_cluster(pos->cluster);
	if (fd64_cluster_valid(next) == 0) {
		if (grow == 0) {
			return 0;
		}
		next = fd64_alloc_cluster();
		if (next == 0 || fd64_zero_cluster(next) != 0 ||
				fd64_fat_set(pos->cluster, next) != 0) {
			return 0;
		}
	}
	pos->cluster = next;
	pos->offset = 0;
	return 1;
}

void fd64_dir_first(struct FDPOS64 *pos)
{
	pos->cluster = fd64_root_cluster;
	pos->offset = 0;
}

/* ---- VFAT 긴 이름 ------------------------------------------------------
   긴 이름은 8.3 항목보다 물리적으로 앞에, 그것도 거꾸로 놓인다. 마지막
   UTF-16 13단위를 담은 항목이 맨 앞에 오고 LFN_LAST 표시를 단다. 긴 항목
   마다 8.3 이름의 검사합이 들어 있고, 둘을 묶어 주는 것이 바로 그 값이다. */

#define LFN_ATTR 0x0f
#define LFN_LAST 0x40
#define LFN_UNITS_PER_ENTRY 13
#define LFN_MAX_ENTRIES (FD64_LFN_MAX_UNITS / LFN_UNITS_PER_ENTRY)
/* NT 대소문자 표시. 전부 소문자인 8.3 이름은 긴 항목을 쓰는 대신 대문자로
   저장하고 힌트 비트만 남긴다. 리눅스와 윈도우 모두 이 표시를 따른다. */
#define NT_LOWER_BASE 0x08
#define NT_LOWER_EXT 0x10
/* 디렉터리 훑기의 상한. 사슬이 망가져도 무한히 돌지 않게 한다. */
#define DIR_SCAN_LIMIT 8192

struct LFN64_STATE {
	uint16_t units[FD64_LFN_MAX_UNITS];
	uint8_t checksum;
	uint8_t next_ord;
	int units_total;
};

static int is_file_entry(const struct FDINFO64 *finfo)
{
	/* 0x08 볼륨 이름, 0x10 디렉터리, 0x0f 긴 이름 항목
	   (0x0f & 0x18 == 0x08이라 긴 이름 항목도 같이 걸러진다) */
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

/* 32바이트 항목 안에서 긴 이름 `i`번째 단위의 바이트 위치 */
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

/* NT 소문자 힌트를 적용해 8.3 이름을 글자로 옮긴다 */
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
			return;		/* 이 커널이 받는 길이를 넘었다 */
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

/* 검사합이 `finfo`와 맞는 긴 이름을 온전히 모았으면 1 */
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

/* *pos 이후(그 자리 포함)의 진짜 파일 항목을 찾아 돌려주고, *pos를 그 다음
   자리로 옮긴다. `at`에는 8.3 항목의 위치가, `name`에는 긴 이름이(없으면
   8.3 이름이) 담긴다. */
int fd64_dir_scan_next(struct FDPOS64 *pos, struct FDINFO64 *out,
	struct FDPOS64 *at, char *name, size_t name_size)
{
	struct LFN64_STATE lfn;
	struct FDINFO64 entry;
	const struct FDINFO64 *e;
	uint32_t guard;

	lfn_reset(&lfn);
	for (guard = 0; guard < DIR_SCAN_LIMIT; guard++) {
		e = fd64_dir_at(pos, CACHE64_READ);
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

int fd64_dir_find(const char *name, struct FDINFO64 *out,
	struct FDPOS64 *at)
{
	uint8_t name83[FD64_NAME_LEN];
	char found[FD64_NAME_MAX];
	struct FDINFO64 entry;
	struct FDPOS64 pos;

	make_name83(name83, name);
	fd64_dir_first(&pos);
	while (fd64_dir_scan_next(&pos, &entry, at, found, sizeof(found)) != 0) {
		/* 긴 이름과 8.3 별칭 어느 쪽 철자로도 열 수 있게 한다. */
		if (name_eq_ci(found, name) != 0 || name_eq83(&entry, name83) != 0) {
			*out = entry;
			return 1;
		}
	}
	return 0;
}

int fd64_dir_write(struct FDHANDLE64 *fh)
{
	struct FDINFO64 *entry;

	entry = fd64_dir_at(&fh->dir, CACHE64_WRITE_META);
	if (entry == NULL) {
		return -1;
	}
	*entry = fh->info;
	return 0;
}


/* 글자 하나를 8.3 문자 집합으로 옮긴다. 옮길 수 없으면 0. */
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

/* 8.3 이름을 만든다. 결과를 되읽어도 원래 이름이 나오지 않으면 *lossy를
   세우는데, 그때가 바로 긴 항목이 필요한 경우다. */
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
			dot = i;		/* 마지막 점이 확장자를 가른다 */
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

/* 이름의 두 부분 중 대문자 ASCII가 없는 쪽에 NT_LOWER_*를 세운다 */
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

	fd64_dir_first(&pos);
	while (fd64_dir_scan_next(&pos, &entry, NULL, NULL, 0) != 0) {
		if (name_eq83(&entry, name11) != 0) {
			return 1;
		}
	}
	return 0;
}

/* 겹치지 않을 때까지 이름을 BASE~N 꼴로 바꾼다. 못 만들면 0. */
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

	raw = (uint8_t *) fd64_dir_at(pos, CACHE64_WRITE_META);
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
			unit = 0x0000;		/* 끝 표시 */
		} else {
			unit = 0xffff;		/* 채움 */
		}
		offset = lfn_unit_offset(i);
		raw[offset] = (uint8_t) unit;
		raw[offset + 1] = (uint8_t) (unit >> 8);
	}
	return 0;
}

/* 비어 있는 자리 `need`개가 잇달아 있는 곳을 찾는다. 지금 있는 디렉터리
   안에 그만한 자리가 없으면 사슬을 늘려서 만든다. */
static int dir_find_run(struct FDPOS64 *start, uint32_t need)
{
	struct FDPOS64 pos;
	struct FDPOS64 run;
	const struct FDINFO64 *e;
	uint32_t have;
	uint32_t guard;

	fd64_dir_first(&pos);
	run = pos;
	have = 0;
	for (guard = 0; guard < DIR_SCAN_LIMIT; guard++) {
		e = fd64_dir_at(&pos, CACHE64_READ);
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

int fd64_dir_create(struct FDHANDLE64 *fh, const char *name)
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

	if (fh == NULL || fd64_initialized == 0 || name == NULL || name[0] == '\0') {
		return 0;
	}
	units_total = utf8_to_utf16_64(name, units, FD64_LFN_MAX_UNITS);
	if (units_total <= 0) {
		return 0;		/* 비었거나, 이 커널이 받는 길이를 넘었다 */
	}
	make_shortname(name11, name, &lossy);
	for (j = 0; j < sizeof(struct FDINFO64); j++) {
		((uint8_t *) &fh->info)[j] = 0;
	}
	/* 8.3 이름만으로 요청한 철자가 그대로 되살아나면 긴 항목이 필요 없다.
	   대소문자만 다른 경우는 NT 힌트가 대신 실어 나른다. */
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
	if (fd64_dir_write(fh) != 0) {
		fh->dir.cluster = 0;
		return 0;
	}
	return 1;
}
