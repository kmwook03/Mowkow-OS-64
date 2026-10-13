#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mpport_file64_mock.h"
#include "../src64/mpport/mpport_file_internal64.h"
#include <fd64.h>
#include <mpport64.h>
#include <utf864.h>

struct MPPORT_MOCK_STATE64 fixture_state64;
static int exception64;
static int allocation_limit64 = -1, allocations64, frees64;
static void *live64[16];
static size_t sizes64[16];
static size_t requested64;
static const char *file_data64;
static uint32_t file_size64;
static size_t read_result64;
static int open_result64, opens64, reads64, lexer_calls64, string_calls64;
static int qstr_fault64, string_fault64;

void *m_malloc(size_t size)
{
	requested64 = size;
	if (allocation_limit64 == 0) {
		exception64 = -12;
		nlr_jump(&exception64);
	}
	if (allocation_limit64 > 0) { allocation_limit64--; }
	assert(size < 1024 * 1024);
	void *ptr = malloc(size);

	assert(ptr != NULL);
	memset(ptr, 0xa5, size);
	for (size_t i = 0; i < sizeof(live64) / sizeof(live64[0]); i++) {
		if (live64[i] == NULL) {
			live64[i] = ptr;
			sizes64[i] = size;
			allocations64++;
			return ptr;
		}
	}
	abort();
}

void m_free(void *ptr, size_t size)
{
	assert(ptr != NULL);
	for (size_t i = 0; i < sizeof(live64) / sizeof(live64[0]); i++) {
		if (live64[i] == ptr) {
			assert(sizes64[i] == size);
			live64[i] = NULL;
			frees64++;
			free(ptr);
			return;
		}
	}
	abort();
}

_Noreturn void mp_raise_OSError(int error)
{
	exception64 = error;
	nlr_jump(&exception64);
}

_Noreturn void nlr_jump_fail(void *value)
{
	(void) value;
	abort();
}

int fd64_open(struct FDHANDLE64 *fh, const char *path)
{
	assert(path != NULL && strcmp(path, "fixture.py") == 0);
	opens64++;
	*fh = (struct FDHANDLE64) { .info.size = file_size64 };
	return open_result64;
}

size_t fd64_read(struct FDHANDLE64 *fh, void *dst, size_t size)
{
	assert(fh->info.size == file_size64 && size == file_size64);
	assert(dst != NULL && size != 0);
	reads64++;
	/* A broken backend count is injected without writing outside the request. */
	if (read_result64 <= size) { memcpy(dst, file_data64, read_result64); }
	return read_result64;
}

qstr qstr_from_str(const char *path)
{
	assert(strcmp(path, "fixture.py") == 0);
	if (qstr_fault64 != 0) {
		exception64 = -12;
		nlr_jump(&exception64);
	}
	return 1;
}

const char *qstr_str(qstr value)
{
	assert(value == 1);
	return "fixture.py";
}

mp_lexer_t *mp_lexer_new(qstr source, mp_reader_t reader)
{
	lexer_calls64++;
	mp_lexer_t *lex = m_new_obj(mp_lexer_t);

	lex->source_name = source;
	lex->reader = reader;
	return lex;
}

void mp_lexer_free(mp_lexer_t *lex)
{
	lex->reader.close(lex->reader.data);
	m_del_obj(mp_lexer_t, lex);
}

mp_obj_t mp_obj_new_str(const char *data, size_t size)
{
	string_calls64++;
	if (string_fault64 != 0) {
		exception64 = -84;
		nlr_jump(&exception64);
	}
	/* Simulate validation/copy and interned-return branches of the VM boundary. */
	for (size_t at = 0; at < size;) {
		uint32_t cp;
		size_t consumed;

		if (utf8_decode64(data + at, size - at, &cp, &consumed) != 0) {
			exception64 = -84;
			nlr_jump(&exception64);
		}
		at += consumed;
	}
	if (size == 0) { return (void *) ""; }
	char *copy = m_new(char, size + 1);

	memcpy(copy, data, size);
	copy[size] = '\0';
	return copy;
}

static void reset64(const char *data, size_t size)
{
	assert(fixture_state64.nlr_top == NULL);
	assert(fixture_state64.nlr_jump_callback_top == NULL);
	assert(allocations64 == frees64);
	for (size_t i = 0; i < sizeof(live64) / sizeof(live64[0]); i++) {
		assert(live64[i] == NULL);
	}
	file_data64 = data;
	file_size64 = (uint32_t) size;
	read_result64 = size;
	open_result64 = 1;
	opens64 = reads64 = lexer_calls64 = string_calls64 = 0;
	allocations64 = frees64 = 0;
	allocation_limit64 = -1;
	qstr_fault64 = string_fault64 = exception64 = 0;
}

/* Put the catcher on a separate frame, like a real interpreter call boundary. */
static void failure64(int operation, int expected)
{
	nlr_buf_t nlr;
	size_t size = 123;

	if (nlr_push(&nlr) == 0) {
		switch (operation) {
		case 0: (void) mpport_load_file("fixture.py", &size); break;
		case 1: (void) mpport_file_lexer64("fixture.py"); break;
		case 2: (void) mp_lexer_new_from_file(1); break;
		case 3: (void) mpport_file_text64("fixture.py"); break;
		case 4: (void) mpport_load_file(NULL, &size); break;
		case 5: (void) mpport_load_file("fixture.py", NULL); break;
		default: abort();
		}
		abort();
	} else {
		assert(nlr.ret_val == &exception64 && exception64 == expected);
	}
	/* size is written by the loader and may be clobbered by native NLR; inspect
	 * output reset separately below, outside the catcher frame.
	 */
	assert(allocations64 == frees64);
	assert(fixture_state64.nlr_top == NULL);
}

static size_t out_size64;

static void load_failure64(int expected)
{
	nlr_buf_t nlr;

	out_size64 = 123;
	if (nlr_push(&nlr) == 0) {
		(void) mpport_load_file("fixture.py", &out_size64);
		abort();
	} else {
		assert(exception64 == expected && out_size64 == 0);
	}
	assert(allocations64 == frees64);
}

static void exact_read64(void)
{
	static const char source[] = "print('가')\n";

	reset64(source, sizeof(source) - 1);
	size_t size = 123;
	uint8_t *buf = mpport_load_file("fixture.py", &size);

	assert(size == sizeof(source) - 1 && reads64 == 1 && opens64 == 1);
	assert(memcmp(buf, source, size) == 0 && allocations64 == 1 && frees64 == 0);
	m_del(uint8_t, buf, size);
	reset64("", 0);
	buf = mpport_load_file("fixture.py", &size);
	assert(buf != NULL && size == 0 && reads64 == 0 && requested64 == 1);
	m_del(uint8_t, buf, 1);
	reset64(source, sizeof(source) - 1);
	open_result64 = 0;
	size = 123;
	assert(mpport_load_file("fixture.py", &size) == NULL && size == 0);
	assert(allocations64 == 0 && reads64 == 0);
	static const size_t counts[] = { 0, 1, sizeof(source) - 2, sizeof(source), SIZE_MAX };

	for (size_t i = 0; i < sizeof(counts) / sizeof(counts[0]); i++) {
		reset64(source, sizeof(source) - 1);
		read_result64 = counts[i];
		load_failure64(MP_EIO);
		assert(reads64 == 1 && lexer_calls64 == 0 && string_calls64 == 0);
	}
	reset64(source, sizeof(source) - 1);
	allocation_limit64 = 0;
	load_failure64(-12);
	assert(reads64 == 0);
	reset64("", UINT32_MAX);
	allocation_limit64 = 0;
	load_failure64(-12);
	assert(requested64 == UINT32_MAX && reads64 == 0);
	reset64("", 0);
	failure64(4, MP_EINVAL);
	failure64(5, MP_EINVAL);
	assert(opens64 == 0);
}

static void consumers64(void)
{
	static const char source[] = "print('가')\n";

	for (int empty = 0; empty < 2; empty++) {
		size_t size = empty ? 0 : sizeof(source) - 1;

		reset64(source, size);
		mp_lexer_t *lex = mpport_file_lexer64("fixture.py");

		assert(lex != NULL && allocations64 == 3 && frees64 == 0);
		assert(fixture_state64.nlr_jump_callback_top == NULL);
		for (size_t i = 0; i < size; i++) {
			assert(lex->reader.readbyte(lex->reader.data) == (byte) source[i]);
		}
		assert(lex->reader.readbyte(lex->reader.data) == MP_READER_EOF);
		assert(lex->reader.readbyte(lex->reader.data) == MP_READER_EOF);
		mp_lexer_free(lex);
		assert(allocations64 == frees64);
		reset64(source, size);
		lex = mp_lexer_new_from_file(1);
		assert(lex != NULL);
		mp_lexer_free(lex);
		reset64(source, size);
		mp_obj_t text = mpport_file_text64("fixture.py");

		assert(string_calls64 == 1 && memcmp(text, source, size) == 0);
		assert(frees64 == 1);
		if (size != 0) { m_del(char, text, size + 1); }
		assert(allocations64 == frees64);
	}
	for (int operation = 1; operation <= 3; operation++) {
		for (int budget = 0; budget < (operation == 3 ? 1 : 3); budget++) {
			reset64("", 0);
			allocation_limit64 = budget;
			failure64(operation, -12);
			assert(fixture_state64.nlr_jump_callback_top == NULL);
		}
		reset64(source, sizeof(source) - 1);
		open_result64 = 0;
		if (operation == 1) {
			assert(mpport_file_lexer64("fixture.py") == NULL);
		} else { failure64(operation, MP_ENOENT); }
		for (int budget = 0; budget < (operation == 3 ? 2 : 3); budget++) {
			reset64(source, sizeof(source) - 1);
			allocation_limit64 = budget;
			failure64(operation, -12);
			assert(fixture_state64.nlr_jump_callback_top == NULL);
		}
		reset64(source, sizeof(source) - 1);
		read_result64 = 1;
		failure64(operation, MP_EIO);
		assert(lexer_calls64 == 0 && string_calls64 == 0);
	}
	reset64(source, sizeof(source) - 1);
	qstr_fault64 = 1;
	failure64(1, -12);
	assert(frees64 == 1 && lexer_calls64 == 0);
	reset64("\xe3\x80", 2);
	failure64(3, -84);
	assert(frees64 == 1);
	reset64(source, sizeof(source) - 1);
	string_fault64 = 1;
	failure64(3, -84);
	assert(frees64 == 1);
}

static int outer_cleanups64;

static void outer_cleanup64(void *context)
{
	(void) context;
	outer_cleanups64++;
}

static void nested64(void)
{
	reset64("test", 4);
	nlr_jump_callback_node_t outer;

	nlr_push_jump_callback(&outer, outer_cleanup64);
	allocation_limit64 = 2;
	failure64(1, -12);
	assert(fixture_state64.nlr_jump_callback_top == &outer && outer_cleanups64 == 0);
	nlr_pop_jump_callback(true);
	assert(outer_cleanups64 == 1 && fixture_state64.nlr_jump_callback_top == NULL);
}

int main(void)
{
	exact_read64();
	consumers64();
	nested64();
	reset64("", 0);
	puts("MicroPython file exact reads, empty ownership and NLR cleanup: PASS");
	return 0;
}
