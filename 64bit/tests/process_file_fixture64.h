#ifndef MOWKOW64_PROCESS_FILE_FIXTURE64_H
#define MOWKOW64_PROCESS_FILE_FIXTURE64_H

static void file_duplicates64(void)
{
	struct PROCESS64 process = {0};
	struct FDHANDLE64 *handle;
	int fd = -1;

	process.files[3].used = process.files[3].cloexec = 1;
	process.files[3].fh.pos = 7;
	assert(process_file_dup64(&process, 3, 4, 0, &fd) == 0 && fd == 4);
	struct UNIX_FILE64 *shared = process.files[3].shared;

	assert(shared && shared->references == 2);
	assert(process.files[4].shared == shared && !process.files[4].cloexec);
	assert(process_file_lock64(&process.files[4], &handle) == 0);
	assert(handle->pos == 7);
	handle->pos = 19; process_file_unlock64(&process.files[4]);
	assert(process_file_lock64(&process.files[3], &handle) == 0);
	assert(handle->pos == 19); process_file_unlock64(&process.files[3]);
	assert(process_file_dup264(&process, 3, 3) == 0);
	assert(process.files[3].cloexec && shared->references == 2);
	assert(process_file_dup264(&process, 3, 4) == 0 && shared->references == 2);
	assert(process_file_dup64(&process, 3, 6, 1, &fd) == 0 && fd == 6);
	assert(process.files[6].cloexec);
	assert(process_file_dup64(&process, 3, 3, 0, &fd) == 0 && fd == 5);
	assert(process_file_dup64(&process, 3, 3, 0, &fd) == 0 && fd == 7);
	fd = 99;
	assert(process_file_dup64(&process, 3, 3, 0, &fd) == -24 && fd == 99);
	assert(shared->references == 5);
	process_files_close64(&process, 1);
	assert(!process.files[3].used && !process.files[6].used);
	assert(shared->references == 3 && process.files[4].used);

	/* Replace a different OFD and leave another alias of the old one alive. */
	process.files[3].used = 1; process.files[3].fh.pos = 41;
	assert(process_file_dup264(&process, 3, 6) == 0);
	struct UNIX_FILE64 *other = process.files[3].shared;

	assert(other != shared && other->references == 2);
	process.files[6].cloexec = 1;
	assert(process_file_dup264(&process, 4, 6) == 0);
	assert(!process.files[6].cloexec && process.files[6].shared == shared);
	assert(other->references == 1 && other->handle.pos == 41);
	assert(shared->references == 4 && shared->handle.pos == 19);
	assert(process_file_dup264(&process, 4, 3) == 0);
	assert(other->references == 0 && shared->references == 5);

	/* Bad source and exhausted reference count cannot close the target. */
	assert(process_file_dup264(&process, -1, 3) == -9);
	assert(process_file_dup264(&process, 4, PROCESS64_MAX_FILES) == -9);
	shared->references = ~0U;
	assert(process_file_dup264(&process, 4, 3) == -75);
	assert(process.files[3].shared == shared && shared->references == ~0U);
	shared->references = 5;
	process_files_close64(&process, 0);
	assert(shared->references == 0);
	assert(process_file_dup264(&process, 3, 3) == -9);
	fd = 99;
	assert(process_file_dup64(&process, 3, 3, 0, &fd) == -9 && fd == 99);
	assert(process_file_dup64(&process, 3, 2, 0, &fd) == -22);
	assert(process_file_dup64(&process, 3, 3, 2, &fd) == -22);
	assert(process_file_dup64(&process, 3, 3, 0, NULL) == -22);
	assert(process_file_dup264(NULL, 3, 4) == -22);
	assert(!irq_off64);
}

static void file_pool_exhaustion64(void)
{
	struct PROCESS64 process = {0};
	struct PROCESS64_FILE held[PROCESS64_MAX * (PROCESS64_MAX_FILES - 3)] = {0};
	int fd = 99;

	/* Fill the actual fixed OFD pool, keeping each reference for cleanup. */
	uint64_t irq = platform_irq_save64();

	for (size_t i = 0; i < sizeof(held) / sizeof(held[0]); i++) {
		held[i].used = 1;
		assert(promote_file64(&held[i]) != NULL);
	}
	platform_irq_restore64(irq);
	process.files[3].used = process.files[4].used = 1;
	process.files[3].fh.pos = 17; process.files[4].fh.pos = 23;
	process.files[4].cloexec = 1;
	assert(process_file_dup264(&process, 3, 4) == -23);
	assert(process.files[4].used && process.files[4].cloexec);
	assert(process.files[4].fh.pos == 23 && !process.files[4].shared);
	assert(!process.files[3].shared && process.files[3].fh.pos == 17);
	assert(process_file_dup64(&process, 3, 5, 0, &fd) == -23 && fd == 99);
	assert(!process.files[5].used);
	process_file_close64(&held[0]);
	assert(process_file_dup264(&process, 3, 4) == 0);
	assert(process.files[4].shared->handle.pos == 17 && !process.files[4].cloexec);
	process_files_close64(&process, 0);
	for (size_t i = 0; i < sizeof(held) / sizeof(held[0]); i++) {
		process_file_close64(&held[i]);
	}
	for (size_t i = 0; i < sizeof(open_files64) / sizeof(open_files64[0]); i++) {
		assert(open_files64[i].references == 0);
	}
	assert(!irq_off64);
}

#endif
