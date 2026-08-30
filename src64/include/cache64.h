#ifndef MOWKOW64_CACHE64_H
#define MOWKOW64_CACHE64_H

#include <stdint.h>

#define CACHE64_BLOCK_SECTORS 8
#define CACHE64_BLOCKS 1024

/* cache64_get의 모드 */
#define CACHE64_READ 0
#define CACHE64_WRITE 1
/* 메타데이터(디렉터리 항목). 파일 데이터와 FAT 다음에 내보낸다. 그래야
   내보내기가 실패해도 주인 없는 클러스터가 남을 뿐, 아직 쓰이지 않은
   사슬을 가리키는 디렉터리 항목은 생기지 않는다. */
#define CACHE64_WRITE_META 2

int cache64_init(void);
/* 섹터 하나의 캐시된 사본을 가리킨다. 입출력 오류면 NULL. 쓰기 모드면 그
   블록을 dirty로 표시한다. 이 포인터는 다음 cache64_get 전까지만 쓸 수
   있다. 캐시 실패가 나면 이 블록을 포함해 어떤 블록이든 밀려날 수 있다. */
uint8_t *cache64_get(uint32_t lba, int mode);
/* 첫 섹터가 [start, end)에 들고 metadata 표시가 `meta`와 같은 더티 블록들을 내보낸다. 
   내보낸 섹터 수를 돌려주고, 입출력 오류면 -1. */
int cache64_flush(uint32_t start, uint32_t end, int meta);
/* 더티 블록을 쓰지 않고 깨끗하다고만 표시한다. */
void cache64_discard_dirty(void);

#endif
