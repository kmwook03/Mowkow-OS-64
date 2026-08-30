/*
 * 주소 순으로 정렬된 자유 리스트 할당기.
 *
 * 커널의 SYS_ALLOC은 순수 범프다 (syscall64.c: heap_next += size). SYS_FREE는
 * 범위만 확인하고 아무것도 돌려받지 않는다. 그래서 예전 malloc/free는 짝을
 * 맞춰 불러도 전부 새는 구조였고, 편집기처럼 한 글자마다 할당/해제하는 앱은
 * 몇 초 만에 힙을 다 쓴다.
 *
 * 여기서는 커널에서 큼직하게 얻어와 앱 안에서 쪼개 쓰고, 해제된 블록은
 * 자유 리스트로 돌려 인접한 것끼리 합친다. 커널에는 돌려주지 않는다 - 힙은
 * 프로세스마다 고정 크기(process64.c의 USER_HEAP_SIZE)라 돌려줘 봐야 쓸 데가
 * 없다.
 */
#include <mowos.h>

#define ALIGN 16
#define CHUNK 4096

struct BLOCK {
	size_t size;            /* 페이로드 바이트 수, ALIGN의 배수 */
	struct BLOCK *next;     /* 자유 리스트에서만 쓴다. 주소 오름차순 */
};

/* 헤더가 ALIGN 크기라 페이로드도 ALIGN에 맞는다. */
#define HDR sizeof(struct BLOCK)

static struct BLOCK *free_list;

static size_t align_up(size_t n)
{
	return (n + (ALIGN - 1)) & ~(size_t) (ALIGN - 1);
}

static char *block_end(struct BLOCK *b)
{
	return (char *) b + HDR + b->size;
}

/* 주소 순 자리에 끼워 넣고 앞뒤로 붙어 있으면 합친다. */
static void free_list_insert(struct BLOCK *b)
{
	struct BLOCK *prev;
	struct BLOCK *cur;

	prev = 0;
	cur = free_list;
	while (cur != 0 && cur < b) {
		prev = cur;
		cur = cur->next;
	}
	b->next = cur;
	if (prev == 0) {
		free_list = b;
	} else {
		prev->next = b;
	}
	if (cur != 0 && block_end(b) == (char *) cur) {
		b->size += HDR + cur->size;
		b->next = cur->next;
	}
	if (prev != 0 && block_end(prev) == (char *) b) {
		prev->size += HDR + b->size;
		prev->next = b->next;
	}
}

/* 큰 블록에서 필요한 만큼만 떼어 낸다. 남는 조각이 헤더 + ALIGN보다 작으면
   쪼개지 않고 통째로 준다 - 쓸 수 없는 부스러기를 만들지 않기 위해. */
static void split(struct BLOCK *b, size_t want)
{
	struct BLOCK *rest;

	if (b->size < want + HDR + ALIGN) {
		return;
	}
	rest = (struct BLOCK *) ((char *) b + HDR + want);
	rest->size = b->size - want - HDR;
	b->size = want;
	free_list_insert(rest);
}

static int grow(size_t want)
{
	struct BLOCK *b;
	size_t need;

	need = want + HDR;
	if (need < CHUNK) {
		need = CHUNK;
	}
	b = (struct BLOCK *) alloc(need);
	if (b == 0) {
		/* 딱 필요한 만큼만 다시 시도한다. 힙 끝자락에서는 CHUNK가 안 들어와도
		   요청한 크기는 들어올 수 있다. */
		need = want + HDR;
		b = (struct BLOCK *) alloc(need);
		if (b == 0) {
			return 0;
		}
	}
	b->size = need - HDR;
	free_list_insert(b);
	return 1;
}

void *malloc(size_t size)
{
	struct BLOCK *prev;
	struct BLOCK *cur;
	int grown;

	if (size == 0 || size > ~(size_t) 0 - HDR - ALIGN) {
		return 0;
	}
	size = align_up(size);
	grown = 0;
	for (;;) {
		prev = 0;
		cur = free_list;
		while (cur != 0) {
			if (cur->size >= size) {
				if (prev == 0) {
					free_list = cur->next;
				} else {
					prev->next = cur->next;
				}
				split(cur, size);
				return (char *) cur + HDR;
			}
			prev = cur;
			cur = cur->next;
		}
		if (grown != 0 || grow(size) == 0) {
			return 0;
		}
		grown = 1;
	}
}

void free(void *ptr)
{
	if (ptr == 0) {
		return;
	}
	free_list_insert((struct BLOCK *) ((char *) ptr - HDR));
}
