#ifndef MOWKOW64_FD64_H
#define MOWKOW64_FD64_H

#include <stddef.h>
#include <stdint.h>

#define FD64_NAME_LEN 11
/* VFAT은 UTF-16 255단위까지 허용하지만, 이 커널은 이름을 실어 나르는
   버퍼를 스택에 두려고 더 짧게 자른다. 이름을 받는 모든 경로가 두 한계를
   함께 지킨다. */
#define FD64_LFN_MAX_UNITS 52
#define FD64_NAME_MAX 160

struct FDINFO64 {
	uint8_t name[8];
	uint8_t ext[3];
	uint8_t type;
	uint8_t reserved[8];
	uint16_t clustno_hi;
	uint16_t time;
	uint16_t date;
	uint16_t clustno;
	uint32_t size;
} __attribute__((packed));

/* 디렉터리 항목이 어디 있는지. FAT32의 루트는 클러스터 사슬이라 고정 배열
   번호로는 자리를 짚을 수 없다. cluster == 0은 "항목 없음", 즉 닫힌
   핸들이라는 뜻이다. */
struct FDPOS64 {
	uint32_t cluster;
	uint32_t offset;
};

/* 캐시된 블록은 밀려날 수 있으므로 핸들이 그 안을 가리켜서는 안 된다.
   대신 디렉터리 항목의 사본과 그 항목이 디스크에서 있던 자리를 들고 있다. */
struct FDHANDLE64 {
	struct FDINFO64 info;
	struct FDPOS64 dir;
	uint32_t pos;
	uint32_t cluster;
};

int fd64_init(void);
uint32_t fd64_file_count(void);
/* `index`번째 항목을 *out에, 그 이름을 `name`에 복사한다. 이름은 긴 이름이
   있으면 긴 이름, 없으면 8.3 이름이다. 둘 다 NULL이어도 된다. 항목이 있으면
   1, 없으면 0을 돌려준다. */
int fd64_file_at(uint32_t index, struct FDINFO64 *out, char *name, size_t name_size);
int fd64_open(struct FDHANDLE64 *fh, const char *name);
size_t fd64_read(struct FDHANDLE64 *fh, void *dst, size_t request_size);
int fd64_seek(struct FDHANDLE64 *fh, int64_t offset, int whence);
uint32_t fd64_next_cluster(uint32_t cluster);
int fd64_create(struct FDHANDLE64 *fh, const char *name);
size_t fd64_write(struct FDHANDLE64 *fh, const void *src, size_t size);
int fd64_truncate(struct FDHANDLE64 *fh, uint32_t size);
/* 더러운 섹터만 내보낸다. 내보낸 섹터 수를 돌려주고, 입출력 오류면 -1. */
int fd64_sync(void);

#endif
