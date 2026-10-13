/*
 * SYS_ALLOC retains each process's bump heap until exit. Track all CRT blocks
 * in address order so free never reads a header derived from an untrusted ptr.
 * Freed neighbours coalesce; gaps from direct alloc() calls remain separate.
 * Header corruption stops allocation and mutation until process exit. This is
 * an accidental-corruption check, not protection against a hostile application.
 * Apps have one execution context; this allocator is not IRQ/reentrant safe.
 */
#include <mowos.h>
#include <stdint.h>

#define ALIGN64 16U
#define CHUNK64 4096U
#define BLOCK_FREE64 0U
#define BLOCK_USED64 1U

struct BLOCK64 {
	size_t size;
	struct BLOCK64 *next;
	uintptr_t state;
	uintptr_t seal;
};

#define HEADER64 sizeof(struct BLOCK64)

_Static_assert(sizeof(uintptr_t) == 8, "CRT allocator requires 64-bit pointers");
_Static_assert(HEADER64 % ALIGN64 == 0, "CRT payload must remain 16-byte aligned");

static struct BLOCK64 *blocks64;
static int allocator_failed64;

/* Bind every field to its header address before following a stored link. */
static uintptr_t block_seal64(const struct BLOCK64 *block)
{
	uintptr_t seal = (uintptr_t) block ^ 0x4d6f776b6f773634ULL;

	seal = (seal ^ block->size) * 0x100000001b3ULL;
	seal = (seal ^ (uintptr_t) block->next) * 0x100000001b3ULL;
	return (seal ^ block->state) * 0x100000001b3ULL;
}

static int block_end64(const struct BLOCK64 *block, uintptr_t *end)
{
	uintptr_t address = (uintptr_t) block;

	if (address > UINTPTR_MAX - HEADER64 ||
		block->size > UINTPTR_MAX - address - HEADER64) { return -22; }
	*end = address + HEADER64 + block->size;
	return 0;
}

/* Strict address order also bounds traversal and rejects cycles/overlaps.
 * Validate the entire list before committing any allocator mutation.
 */
static int blocks_valid64(void)
{
	struct BLOCK64 *block = blocks64;
	uintptr_t end;

	if (allocator_failed64) { return 0; }
	while (block != NULL) {
		if (((uintptr_t) block & (ALIGN64 - 1U)) != 0 ||
			block->seal != block_seal64(block) || block->state > BLOCK_USED64 ||
			block->size < ALIGN64 || (block->size & (ALIGN64 - 1U)) != 0 ||
			block_end64(block, &end) != 0 ||
			(block->next != NULL && ((uintptr_t) block->next < end ||
				((uintptr_t) block->next & (ALIGN64 - 1U)) != 0))) {
			allocator_failed64 = 1;
			return 0;
		}
		block = block->next;
	}
	return 1;
}

static int merge_size64(size_t left, size_t right, size_t *size)
{
	if (left > SIZE_MAX - HEADER64 || right > SIZE_MAX - HEADER64 - left) {
		return -22;
	}
	*size = left + HEADER64 + right;
	return 0;
}

/* Both headers were validated before mutation; only adjacent free blocks merge. */
static void coalesce64(struct BLOCK64 *block)
{
	struct BLOCK64 *next = block->next;
	uintptr_t end;
	size_t size;

	if (next == NULL || block->state != BLOCK_FREE64 || next->state != BLOCK_FREE64 ||
		block_end64(block, &end) != 0 || end != (uintptr_t) next) { return; }
	if (merge_size64(block->size, next->size, &size) != 0) {
		allocator_failed64 = 1;
		return;
	}
	block->size = size;
	block->next = next->next;
	block->seal = block_seal64(block);
}

static int grow64(size_t want)
{
	struct BLOCK64 *block, *previous = NULL, *current = blocks64;
	size_t exact = want + HEADER64;
	size_t need = exact < CHUNK64 ? CHUNK64 : exact;
	uintptr_t address, end, previous_end;

	address = (uintptr_t) alloc(need);
	if (address == 0 && need != exact) {
		/* An exact request may still fit in the last partial syscall chunk. */
		need = exact;
		address = (uintptr_t) alloc(need);
	}
	if (address == 0) { return 0; }
	if ((address & (ALIGN64 - 1U)) != 0 || need > UINTPTR_MAX - address) { return 0; }
	end = address + need;
	while (current != NULL && (uintptr_t) current < address) {
		previous = current;
		current = current->next;
	}
	/* A bad syscall result must not overwrite a live header or payload. */
	if ((current != NULL && end > (uintptr_t) current) ||
		(previous != NULL && (block_end64(previous, &previous_end) != 0 ||
			previous_end > address))) { return 0; }
	block = (struct BLOCK64 *) address;
	*block = (struct BLOCK64) { need - HEADER64, current, BLOCK_FREE64, 0 };
	block->seal = block_seal64(block);
	if (previous == NULL) { blocks64 = block; }
	else {
		previous->next = block;
		previous->seal = block_seal64(previous);
	}
	coalesce64(block);
	if (previous != NULL) { coalesce64(previous); }
	return !allocator_failed64;
}

void *malloc(size_t size)
{
	struct BLOCK64 *block, *rest;
	int grown = 0;

	if (size == 0 || size > SIZE_MAX - (ALIGN64 - 1U)) { return NULL; }
	size = (size + ALIGN64 - 1U) & ~(size_t) (ALIGN64 - 1U);
	if (size > SIZE_MAX - HEADER64 || !blocks_valid64()) { return NULL; }
	for (;;) {
		for (block = blocks64; block != NULL; block = block->next) {
			if (block->state != BLOCK_FREE64 || block->size < size) { continue; }
			/* Subtract before testing the split threshold to avoid want+header wrap. */
			if (block->size - size >= HEADER64 + ALIGN64) {
				rest = (struct BLOCK64 *) ((uintptr_t) block + HEADER64 + size);
				*rest = (struct BLOCK64) {
					block->size - size - HEADER64, block->next, BLOCK_FREE64, 0
				};
				rest->seal = block_seal64(rest);
				block->size = size;
				block->next = rest;
			}
			block->state = BLOCK_USED64;
			block->seal = block_seal64(block);
			return (void *) ((uintptr_t) block + HEADER64);
		}
		if (grown || !grow64(size)) { return NULL; }
		grown = 1;
	}
}

void free(void *ptr)
{
	struct BLOCK64 *previous = NULL, *block;
	uintptr_t address = (uintptr_t) ptr;

	if (ptr == NULL || (address & (ALIGN64 - 1U)) != 0 || !blocks_valid64()) { return; }
	for (block = blocks64; block != NULL; block = block->next) {
		if ((uintptr_t) block + HEADER64 == address) {
			if (block->state != BLOCK_USED64) { return; }
			block->state = BLOCK_FREE64;
			block->seal = block_seal64(block);
			coalesce64(block);
			if (previous != NULL) { coalesce64(previous); }
			return;
		}
		previous = block;
	}
}
