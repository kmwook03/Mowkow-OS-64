#include <address_space64.h>
#include <arch/address_space64.h>
#include <arch/platform64.h>
#include <kstring64.h>

enum AS_STATE64 { AS_FREE64, AS_BUSY64, AS_READY64, AS_REAP64 };

struct ADDRESS_SPACE64 {
	struct AS_TABLES64 tables;
	enum AS_STATE64 state;
};

static struct ADDRESS_SPACE64 spaces64[USER_SPACE_LIMIT64];
static struct ADDRESS_SPACE64 kernel_space64;
static struct ADDRESS_SPACE64 *active_space64;

static int owned_space64(const struct ADDRESS_SPACE64 *space)
{
	for (unsigned int i = 0; i < USER_SPACE_LIMIT64; i++) {
		if (space == &spaces64[i]) { return 1; }
	}
	return 0;
}

static int page_span64(uintptr_t base, size_t size)
{
	return size != 0 && ((base | size) & (USER_PAGE_SIZE64 - 1)) == 0 &&
		size <= UINTPTR_MAX - base;
}

static int pool_span64(uintptr_t physical, size_t size)
{
	return page_span64(physical, size) && physical >= MEMMAN64_EARLY_START &&
		physical < MEMMAN64_EARLY_END && size <= MEMMAN64_EARLY_END - physical;
}

static int user_span64(uintptr_t base, size_t size)
{
	if (size == 0 || size > UINTPTR_MAX - base) { return 0; }
	return (base >= USER_IMAGE_BASE64 && base < USER_IMAGE_END64 &&
		size <= USER_IMAGE_END64 - base) ||
		(base >= USER_HEAP_BASE64 && base < USER_HEAP_END64 &&
		size <= USER_HEAP_END64 - base) ||
		(base >= USER_STACK_BASE64 && base < USER_STACK_END64 &&
		size <= USER_STACK_END64 - base);
}

static void state_set64(struct ADDRESS_SPACE64 *space, enum AS_STATE64 state)
{
	uint64_t irq = platform_irq_save64();

	space->state = state;
	platform_irq_restore64(irq);
}

/* The root is freed last. On error the unreleased table and all ancestors
 * remain recorded; the space stays unpublished and cannot be activated.
 */
static int tables_free64(struct ADDRESS_SPACE64 *space)
{
	for (unsigned int i = AS_TABLES_MAX64; i > 0; i--) {
		uintptr_t alias = space->tables.page[i - 1];
		int status;

		if (alias == 0) { continue; }
		status = memman64_free_4k(&memman64, alias, USER_PAGE_SIZE64);
		if (status != 0) { return status; }
		space->tables.page[i - 1] = 0;
	}
	return 0;
}

static int tables_create64(struct ADDRESS_SPACE64 *space)
{
	unsigned int count = as_arch_table_count64();
	int status = -12;

	if (count == 0 || count > AS_TABLES_MAX64) { return -22; }
	for (unsigned int i = 0; i < count; i++) {
		uintptr_t alias = memman64_alloc_4k(&memman64, USER_PAGE_SIZE64);

		if (alias == 0) { goto failed; }
		space->tables.page[i] = alias;
		if (!pool_span64(as_arch_physical64(alias), USER_PAGE_SIZE64) ||
			as_arch_alias64(as_arch_physical64(alias)) != alias) {
			status = -22;
			goto failed;
		}
		memset((void *) alias, 0, USER_PAGE_SIZE64);
	}
	status = as_arch_build64(&space->tables);
	if (status == 0) { return 0; }
failed:
	/* Preserve the original failure; the retained handle exposes failed unwind. */
	(void) tables_free64(space);
	return status;
}

int address_space_init64(void)
{
	uint64_t irq = platform_irq_save64();
	int status;

	if (kernel_space64.state == AS_READY64) {
		platform_irq_restore64(irq);
		return 0;
	}
	if (kernel_space64.state == AS_BUSY64) {
		platform_irq_restore64(irq);
		return -16;
	}
	kernel_space64.state = AS_BUSY64;
	platform_irq_restore64(irq);
	status = tables_free64(&kernel_space64);
	if (status == 0) { status = as_arch_supported64(); }
	if (status == 0) { status = tables_create64(&kernel_space64); }
	state_set64(&kernel_space64, status == 0 ? AS_READY64 : AS_REAP64);
	return status;
}

int address_space_create64(struct ADDRESS_SPACE64 **result)
{
	struct ADDRESS_SPACE64 *space = NULL;
	uint64_t irq;
	int status;

	if (result == NULL || *result != NULL) { return -22; }
	status = address_space_init64();
	if (status != 0) { return status; }
	irq = platform_irq_save64();
	for (unsigned int i = 0; i < USER_SPACE_LIMIT64; i++) {
		if (spaces64[i].state != AS_FREE64) { continue; }
		space = &spaces64[i];
		space->state = AS_BUSY64;
		break;
	}
	platform_irq_restore64(irq);
	if (space == NULL) { return -12; }
	*result = space;
	status = tables_create64(space);
	if (status == 0) { state_set64(space, AS_READY64); return 0; }
	/* tables_create already attempted unwind once; do not spin on a failed free. */
	if (space->tables.page[0] == 0) {
		*result = NULL;
		state_set64(space, AS_FREE64);
	} else { state_set64(space, AS_REAP64); }
	return status;
}

int address_space_destroy64(struct ADDRESS_SPACE64 **result)
{
	struct ADDRESS_SPACE64 *space;
	uint64_t irq;
	int status;

	if (result == NULL) { return -22; }
	space = *result;
	if (space == NULL) { return 0; }
	if (!owned_space64(space)) { return -22; }
	irq = platform_irq_save64();
	if (active_space64 == space || space->state == AS_BUSY64) {
		platform_irq_restore64(irq);
		return -16;
	}
	if (space->state != AS_READY64 && space->state != AS_REAP64) {
		platform_irq_restore64(irq);
		return -22;
	}
	space->state = AS_BUSY64;
	platform_irq_restore64(irq);
	status = tables_free64(space);
	if (status == 0) { *result = NULL; }
	state_set64(space, status == 0 ? AS_FREE64 : AS_REAP64);
	return status;
}

int address_space_map64(struct ADDRESS_SPACE64 *space, uintptr_t address,
	uintptr_t physical, size_t size, unsigned int permissions)
{
	uint64_t irq;
	int status = 0;

	if (!owned_space64(space) || !page_span64(address, size) ||
		!user_span64(address, size) || !pool_span64(physical, size) ||
		(permissions != USER_READ64 && permissions != (USER_READ64 | USER_WRITE64) &&
		permissions != (USER_READ64 | USER_EXEC64)) ||
		((permissions & USER_EXEC64) && address >= USER_IMAGE_END64)) { return -22; }
	irq = platform_irq_save64();
	if (space->state != AS_READY64 || active_space64 == space) {
		platform_irq_restore64(irq);
		return -16;
	}
	space->state = AS_BUSY64;
	platform_irq_restore64(irq);
	for (size_t offset = 0; offset < size; offset += USER_PAGE_SIZE64) {
		uint64_t *leaf = as_arch_leaf64(&space->tables, address + offset);

		if (leaf == NULL) { status = -22; break; }
		if (*leaf != 0) { status = -17; break; }
	}
	if (status == 0) {
		for (size_t offset = 0; offset < size; offset += USER_PAGE_SIZE64) {
			uint64_t *leaf = as_arch_leaf64(&space->tables, address + offset);

			*leaf = as_arch_encode64(physical + offset, permissions);
		}
	}
	state_set64(space, AS_READY64);
	return status;
}

int address_space_query64(const struct ADDRESS_SPACE64 *space, uintptr_t address,
	struct USER_PAGE64 *page)
{
	uint64_t *leaf;
	struct USER_PAGE64 found;
	int status;

	if (!owned_space64(space) || page == NULL) { return -22; }
	if (space->state != AS_READY64) { return -16; }
	if (!user_span64(address, 1)) { return -14; }
	leaf = as_arch_leaf64(&space->tables, address);
	if (leaf == NULL) { return -14; }
	status = as_arch_decode64(*leaf, &found.physical, &found.permissions);
	if (status != 0 || !pool_span64(found.physical, USER_PAGE_SIZE64)) { return -14; }
	found.physical += address & (USER_PAGE_SIZE64 - 1);
	found.kernel_address = as_arch_alias64(found.physical);
	*page = found;
	return 0;
}

int address_space_validate64(const struct ADDRESS_SPACE64 *space, uintptr_t address,
	size_t size, unsigned int access)
{
	uintptr_t end;

	if (size == 0 || access == 0 || (access & ~7U) != 0) { return -22; }
	if (!user_span64(address, size)) { return -14; }
	end = address + size;
	while (address < end) {
		struct USER_PAGE64 page;
		int status = address_space_query64(space, address, &page);

		if (status != 0) { return status; }
		if ((page.permissions & access) != access) { return -14; }
		address = (address & ~(uintptr_t) (USER_PAGE_SIZE64 - 1)) + USER_PAGE_SIZE64;
	}
	return 0;
}

int address_space_activate64(struct ADDRESS_SPACE64 *space)
{
	if (!as_arch_irq_off64()) { return -1; }
	if (space == NULL) { space = &kernel_space64; }
	else if (!owned_space64(space)) { return -22; }
	if (space->state != AS_READY64) { return -16; }
	as_arch_install64(as_arch_physical64(space->tables.page[0]));
	active_space64 = space;
	return 0;
}

int user_backing_alloc64(struct USER_BACKING64 *backing, size_t size)
{
	uintptr_t alias;

	if (backing == NULL || backing->kernel_address != 0 || backing->physical != 0 ||
		backing->size != 0 || !page_span64(0, size) ||
		size > MEMMAN64_EARLY_END - MEMMAN64_EARLY_START) { return -22; }
	alias = memman64_alloc_4k(&memman64, size);
	if (alias == 0) { return -12; }
	backing->kernel_address = alias;
	backing->physical = as_arch_physical64(alias);
	backing->size = size;
	if (!pool_span64(backing->physical, size) ||
		as_arch_alias64(backing->physical) != alias) {
		/* Keep ownership even when an invalid allocator result cannot be freed. */
		if (memman64_free_4k(&memman64, alias, size) == 0) {
			memset(backing, 0, sizeof(*backing));
		}
		return -22;
	}
	memset((void *) alias, 0, size);
	return 0;
}

int user_backing_free64(struct USER_BACKING64 *backing)
{
	int status;

	if (backing == NULL) { return -22; }
	if (backing->kernel_address == 0 && backing->physical == 0 && backing->size == 0) {
		return 0;
	}
	if (!pool_span64(backing->physical, backing->size) ||
		backing->kernel_address != as_arch_alias64(backing->physical)) { return -22; }
	status = memman64_free_4k(&memman64, backing->kernel_address, backing->size);
	if (status == 0) { memset(backing, 0, sizeof(*backing)); }
	return status;
}

int user_backing_sync_code64(const struct USER_BACKING64 *backing,
	size_t offset, size_t size)
{
	if (backing == NULL || !pool_span64(backing->physical, backing->size) ||
		backing->kernel_address != as_arch_alias64(backing->physical) || size == 0 ||
		offset >= backing->size || size > backing->size - offset) { return -22; }
	return as_arch_sync_code64(backing->kernel_address + offset, size);
}
