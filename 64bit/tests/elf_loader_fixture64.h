#ifndef MOWKOW64_ELF_LOADER_FIXTURE64_H
#define MOWKOW64_ELF_LOADER_FIXTURE64_H

/* The parser/storage fixtures exercise real snapshot reads and validation.
 * Private page tables and mapping ownership are covered by process_memory64.
 */
int user_backing_alloc64(struct USER_BACKING64 *backing, size_t size)
{
	uintptr_t address = memman64_alloc_4k(&memman64, size);

	if (address == 0) { return -12; }
	*backing = (struct USER_BACKING64) { address, address, size };
	memset((void *) address, 0, size);
	return 0;
}

int user_backing_free64(struct USER_BACKING64 *backing)
{
	if (backing->size == 0) { return 0; }
	int status = memman64_free_4k(&memman64, backing->kernel_address, backing->size);

	if (status == 0) { *backing = (struct USER_BACKING64) {0}; }
	return status;
}

int user_backing_sync_code64(const struct USER_BACKING64 *backing,
	size_t offset, size_t size)
{
	(void) backing; (void) offset; (void) size;
	return 0;
}

int address_space_map64(struct ADDRESS_SPACE64 *space, uintptr_t va,
	uintptr_t pa, size_t size, unsigned int permissions)
{
	(void) space; (void) va; (void) pa; (void) size; (void) permissions;
	return 0;
}

static int load_fixture64(const char *path, struct PROCESS64 *process)
{
	static unsigned char space_token;
	int status;

	process->space = (struct ADDRESS_SPACE64 *) (void *) &space_token;
	status = elf64_prepare_image(path, process);
	(void) user_backing_free64(&process->image_storage);
	(void) user_backing_free64(&process->elf_snapshot);
	return status;
}

#endif
