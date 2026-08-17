#ifndef MOWKOW64_BLOCK64_H
#define MOWKOW64_BLOCK64_H

#include <stdint.h>

#define BLOCK64_SECTOR_SIZE 512

/* 저장 장치 전송 계층 하나. 여기 LBA는 절대값이다. 파티션 시작 위치는
   block64_read/block64_write가 넘기기 전에 더한다. */
struct BLOCK64_OPS {
	const char *name;
	int (*read)(uint64_t lba, uint32_t count, void *dst);
	int (*write)(uint64_t lba, uint32_t count, const void *src);
	uint64_t (*sector_count)(void);
};

/* 전송 계층을 고르고(AHCI가 있으면 AHCI, 없으면 ATA PIO) 파티션 표를
   읽는다. 이 파일의 다른 함수보다 먼저 불러야 한다. */
int block64_init(void);
const char *block64_transport(void);
/* 모든 요청에 더하는 LBA 값. 슈퍼플로피면 0이고, 디스크에 MBR이 있으면
   그 파티션의 첫 섹터다. */
uint64_t block64_part_base(void);
/* 볼륨의 전체 섹터 수. 장치가 크기를 알려 주지 않으면 0. */
uint64_t block64_sector_count(void);
int block64_read(uint64_t lba, uint32_t count, void *dst);
int block64_write(uint64_t lba, uint32_t count, const void *src);

extern const struct BLOCK64_OPS ata64_ops;
extern const struct BLOCK64_OPS ahci64_ops;
/* AHCI 컨트롤러를 찾아 포트를 열었으면 0 */
int ahci64_probe(void);

#endif
