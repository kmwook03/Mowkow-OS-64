/* Test real table mutations and cache walks; only privileged CPU operations are hooks. */
#define AARCH64_MMU_HOST_TEST
#define AARCH64_CACHE_HOST_TEST
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../src64/arch/aarch64/mmu64.c"

static unsigned int tlbs64, lines64, syncs64;
static uint64_t ctr64 = (4ULL << 16) | 3U; /* D=64, I=32 bytes. */
static uintptr_t addresses64[32];
static unsigned int operations64[32];

void a64_test_tlb64(void) { tlbs64++; }
void arch64_panic_blink(int code) { (void) code; assert(0); }
uint64_t a64_cache_ctr64(void) { return ctr64; }
void a64_cache_line64(uintptr_t address, unsigned int operation)
{
	assert(lines64 < 32);
	addresses64[lines64] = address; operations64[lines64++] = operation;
}
void a64_cache_sync64(unsigned int instruction)
{
	assert(instruction <= 1); syncs64++;
	if (instruction) { assert(lines64 && operations64[lines64 - 1] == 3); }
}
static void tables64(void)
{
	static uint64_t before[USER_L3_TABLES][TABLE_ENTRIES];
	static uint64_t before_l2[TABLE_ENTRIES];
	unsigned int tlbs;

	arch64_user_unmap_all();
	assert(arch64_user_map_range(4095, 2, 1) == 0);
	assert((user_l3[0][0] & DESC_UXN) == 0 && user_l3[0][1] != 0);
	assert(arch64_user_map_range(0, 1, 0) == 0 && (user_l3[0][0] & DESC_UXN));
	for (unsigned int i = 1; i < USER_L3_TABLES; i++) {
		assert(arch64_user_map_range(i * L2_SPAN, 1, 0) == 0);
	}
	memcpy(before, user_l3, sizeof(before)); memcpy(before_l2, user_l2, sizeof(before_l2));
	tlbs = tlbs64;
	assert(arch64_user_map_range(8 * L2_SPAN - 1U, 2, 1) == -12);
	assert(memcmp(before, user_l3, sizeof(before)) == 0);
	assert(memcmp(before_l2, user_l2, sizeof(before_l2)) == 0 && tlbs64 == tlbs);
	assert(arch64_user_map_range(UINTPTR_MAX - 4U, 8, 0) == -22);
	assert(arch64_user_map_range(LOW_RAM_SIZE - 1U, 2, 0) == -22);
	assert(arch64_user_map_range(LOW_RAM_SIZE, SIZE_MAX, 0) == -22);
	assert(arch64_user_map_range(0, 0, 0) == -22);
	assert(arch64_user_map_range(0, 1, 2) == -22);
	assert(tlbs64 == tlbs && memcmp(before, user_l3, sizeof(before)) == 0);
	user_l3_index[0] = 512;
	assert(arch64_user_map_range(0, 1, 0) == -22 && tlbs64 == tlbs);
	arch64_user_unmap_all();
	assert(arch64_user_map_range(LOW_RAM_SIZE - 1U, 1, 1) == 0);
	assert(user_l3_index[0] == 511 && user_l3[0][511] != 0);
	arch64_user_unmap_all();
	assert(arch64_user_map_range(0, 9 * L2_SPAN, 1) == -12);
	assert(user_l1[0] == 0 && user_l3_index[0] == 0xffffU);
}
static void cache64(void)
{
	assert(a64_cache_walk64(63, 2, A64_CACHE_CLEAN64) == 0);
	assert(lines64 == 2 && addresses64[0] == 0 && addresses64[1] == 64 && syncs64 == 0);
	lines64 = 0;
	assert(a64_cache_dma64((const void *) (UINTPTR_MAX - 1U), 1, 1) == 0);
	assert(lines64 == 1 && addresses64[0] == (UINTPTR_MAX & ~(uintptr_t) 63U));
	assert(operations64[0] == A64_CACHE_INVALIDATE64 && syncs64 == 1);
	lines64 = syncs64 = 0;
	assert(a64_cache_walk64(UINTPTR_MAX - 1U, 2, 0) == -22);
	assert(a64_cache_walk64(64, 0, 0) == -22);
	assert(a64_cache_walk64(64, 1, 4) == -22 && lines64 == 0);
	arch64_sync_user_code(63, 2);
	assert(lines64 == 4 && syncs64 == 2);
	assert(addresses64[0] == ARCH64_KERNEL_VA_BASE &&
		addresses64[1] == ARCH64_KERNEL_VA_BASE + 64U);
	assert(addresses64[2] == ARCH64_KERNEL_VA_BASE + 32U &&
		addresses64[3] == ARCH64_KERNEL_VA_BASE + 64U);
	assert(operations64[0] == 2 && operations64[2] == 3);
	lines64 = syncs64 = 0;
	arch64_sync_user_code(UINTPTR_MAX, SIZE_MAX);
	arch64_sync_user_code(LOW_RAM_SIZE - 1U, 2);
	arch64_sync_user_code(0, 0);
	assert(!lines64 && !syncs64);
	ctr64 = (15ULL << 16) | 15U;
	assert(a64_cache_walk64(UINTPTR_MAX - 1U, 1, 0) == 0 && lines64 == 1);
}
int main(void)
{
	tables64(); cache64();
	arch64_mmu_init();
	/* Frozen boot PTE: last first-GiB page, AF/shareable/UXN, executable at EL1. */
	assert(low_l3[511][511] == UINT64_C(0x004000003ffff703));
	assert(map_device_window64(MAILBOX_BASE, 5) == -22);
	assert(map_device_window64(MAILBOX_BASE, 1) == -22);
	assert(map_device_window64(UINT64_MAX, 0) == -22);
	assert(map_device_window64(0, 0) == -22);
	puts("aarch64 MMU/cache: PASS");
	return 0;
}
