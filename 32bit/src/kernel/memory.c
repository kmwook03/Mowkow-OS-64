/**
 * @file memory.c
 * @brief 메모리 관리 함수 구현
 * 
 * 하리보테 OS는 기본적으로 가변 메모리 관리 방식을 사용합니다. (하단에 4KB 단위 고정 메모리 관리 함수도 있긴 하지만 페이징을 위한 밑작업으로 봐야함)
 * 이 파일에는 메모리 테스트, 메모리 관리자 초기화, 메모리 할당 및 해제 함수가 포함되어 있습니다.
 */

#include "../include/bootpack.h"

#define EFLAGS_AC_BIT      0x00040000
#define CR0_CACHE_DISABLE  0x60000000

/**
 * @brief 메모리 테스트 함수
 * 
 * 1. CPU가 386인지 486 이상인지 확인
 * 
 * 2. 486 이상일 경우 캐시를 비활성화 (캐시가 활성화 되면 메모리 테스트가 올바르게 수행되지 않을 수 있음)
 * 
 * 3. 지정된 메모리 영역에 대해 메모리 테스트 수행
 * 
 * 4. 486 이상일 경우 캐시를 다시 활성화
 * 
 * @param start: 테스트 시작 주소
 * @param end: 테스트 종료 주소
 * @return: 테스트된 메모리 크기 (바이트 단위)
 */
unsigned int memtest(unsigned int start, unsigned int end)
{                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                      
    char flg486 = 0;
    unsigned int eflg, cr0, i;

    // check if CPU is 386 or 486+
    eflg = io_load_eflags();
    eflg |= EFLAGS_AC_BIT; // AC-bit = 1
    io_store_eflags(eflg);
    eflg = io_load_eflags();
    if ((eflg & EFLAGS_AC_BIT) != 0) { // if AC-bit can be set, it is 486+
        flg486 = 1;
    }
    eflg &= ~EFLAGS_AC_BIT; // AC-bit = 0
    io_store_eflags(eflg);

    if (flg486 != 0) {
        cr0 = load_cr0();
        cr0 |= CR0_CACHE_DISABLE; // disable cache
        store_cr0(cr0);
    }

    i = memtest_sub(start, end);

    if (flg486 != 0) {
        cr0 = load_cr0();
        cr0 &= ~CR0_CACHE_DISABLE; // enable cache
        store_cr0(cr0);
    }

    return i;
}

/**
 * @brief 메모리 관리자 초기화 함수.
 * 
 * @param man: 메모리 관리자 구조체 포인터
 * @return: void
 */
void memman_init(struct MEMMAN *man)
{
    man->frees = 0;
    man->maxfrees = 0;
    man->lostsize = 0;
    man->losts = 0;
    return;
}

/**
 * @brief 사용 가능한 전체 메모리 크기 반환 함수
 * 
 * @param man: 메모리 관리자 구조체 포인터
 * @return: 사용 가능한 전체 메모리 크기 (바이트 단위)
 */
unsigned int memman_total(struct MEMMAN *man)
{
    unsigned int i, t = 0;
    for (i=0; i<man->frees; i++) {
        t += man->free[i].size;
    }
    return t;
}

/**
 * @brief 메모리 할당 함수
 * 
 * 최초 적합(First Fit) 정책에 따라 가장 먼저 발견된 충분히 큰 빈 공간을 할당
 * 
 * @param man: 메모리 관리자 구조체 포인터
 * @param size: 할당할 메모리 크기 (바이트 단위)
 * @return: 할당된 메모리의 시작 주소, 할당 실패 시 0 반환
 */
unsigned int memman_alloc(struct MEMMAN *man, unsigned int size)
{
    unsigned int i, a;
    for (i=0; i<man->frees; i++) {              // 모든 빈 공간 탐색
        if (man->free[i].size >= size) {        // 할당 가능한 블록이면?
            a = man->free[i].addr;              // 할당할 메모리의 시작 주소
            man->free[i].addr += size;          // 빈 공간의 시작 주소가 할당된 만큼 뒤로 이동
            man->free[i].size -= size;          // 빈 공간의 크기가 할당된 만큼 줄어든다
            if (man->free[i].size == 0) {       // 빈 공간이 없어졌으면
                man->frees--;                   // 빈 공간 목록에서 제거
                for (; i<man->frees; i++) {     // 제거된 뒤의 빈 공간을 앞으로 당김
                    man->free[i] = man->free[i+1];
                }
            }
            return a;                           // 할당된 메모리의 시작 주소 반환
        }
    }
    return 0; // 할당 실패
}

/**
 * @brief 메모리 해제 함수
 * 
 * @param man: 메모리 관리자 구조체 포인터
 * @param addr: 해제할 메모리의 시작 주소
 * @param size: 해제할 메모리 크기 (바이트 단위)
 * @return: 0: 성공, -1: 실패
 */
int memman_free(struct MEMMAN *man, unsigned int addr, unsigned int size)
{
    int i, j;
    // find the place to insert
    for (i=0; i<man->frees; i++) {
        if (man->free[i].addr > addr) {
            break;
        }
    }

    // free[i-1].addr < addr < free[i].addr
    // try to merge with previous
    if (i > 0) {
        if (man->free[i-1].addr + man->free[i-1].size == addr) {
            man->free[i-1].size += size;
            if (i < man->frees) {
                if (addr + size == man->free[i].addr) {
                    man->free[i-1].size += man->free[i].size;
                    man->frees--;
                    for (; i<man->frees; i++) {
                        man->free[i] = man->free[i+1];
                    }
                }
            }
            return 0; // freed successfully
        }
    }

    if (i < man->frees) {
        if (addr + size == man->free[i].addr) {
            man->free[i].addr = addr;
            man->free[i].size += size;
            return 0; // freed successfully
        }
    }

    if (man->frees < MEMMAN_FREES) {
        // move forward to make space
        for (j=man->frees; j>i; j--) {
            man->free[j] = man->free[j-1];
        }
        man->frees++;
        if (man->maxfrees < man->frees) {
            man->maxfrees = man->frees;
        }
        man->free[i].addr = addr;
        man->free[i].size = size;
        return 0;
    }
    man->losts++;
    man->lostsize += size;
    return -1; // cannot free
}

/**
 * @brief 4KB 단위 메모리 할당 함수
 * 
 * @param man: 메모리 관리자 구조체 포인터
 * @param size: 할당할 메모리 크기 (바이트 단위)
 * @return: 할당된 메모리의 시작 주소, 할당 실패 시 0 반환
 */
unsigned int memman_alloc_4k(struct MEMMAN *man, unsigned int size)
{
    unsigned int a;
    size = (size + 0xfff) & 0xfffff000; // round up to 4KB
    a = memman_alloc(man, size);
    return a;
}

/**
 * @brief 4KB 단위 메모리 해제 함수
 * 
 * @param man: 메모리 관리자 구조체 포인터
 * @param addr: 해제할 메모리의 시작 주소
 * @param size: 해제할 메모리 크기 (바이트 단위)
 * @return: 0: 성공, -1: 실패
 */
int memman_free_4k(struct MEMMAN *man, unsigned int addr, unsigned int size)
{
    int i;
    size = (size + 0xfff) & 0xfffff000; // round up to 4KB
    i = memman_free(man, addr, size);
    return i;
}
