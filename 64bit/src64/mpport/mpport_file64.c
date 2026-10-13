/* GC file buffers have one owner until a reader or Python string takes over. */
#include <fd64.h>
#include <mpport64.h>

#include "py/mperrno.h"
#include "py/reader.h"
#include "mpport_file_internal64.h"

struct MPPORT_FILE_OWNER64 {
	nlr_jump_callback_node_t callback;
	uint8_t *buf;
	size_t size;
	mp_reader_t reader;
	int reader_ready;
};

/* FDHANDLE64 is a value snapshot, with no close operation or acquired resource.
 * Its uint32_t size fits size_t on both 64-bit ports. No terminator is needed.
 */
uint8_t *mpport_load_file(const char *path, size_t *out_size)
{
	struct FDHANDLE64 fh;
	uint8_t *buf;
	size_t size;

	if (out_size == NULL) {
		mp_raise_OSError(MP_EINVAL);
	}
	*out_size = 0;
	if (path == NULL) {
		mp_raise_OSError(MP_EINVAL);
	}
	if (fd64_open(&fh, path) == 0) {
		return NULL;
	}
	size = fh.info.size;
	/* A non-NULL empty buffer distinguishes an empty module from a missing one. */
	buf = m_new(uint8_t, size == 0 ? 1 : size);
	if (size != 0 && fd64_read(&fh, buf, size) != size) {
		/* fd64_read has no signed error channel: any mismatch is failed I/O. */
		m_del(uint8_t, buf, size);
		mp_raise_OSError(MP_EIO);
	}
	*out_size = size;
	return buf;
}

static void mpport_file_cleanup64(void *context)
{
	struct MPPORT_FILE_OWNER64 *owner = context;

	if (owner->reader_ready != 0) {
		owner->reader.close(owner->reader.data);
	} else {
		m_del(uint8_t, owner->buf, owner->size == 0 ? 1 : owner->size);
	}
}

static mp_lexer_t *mpport_file_lexer_owned64(const char *path, qstr source_name)
{
	struct MPPORT_FILE_OWNER64 owner = { 0 };
	mp_lexer_t *lex;

	owner.buf = mpport_load_file(path, &owner.size);
	if (owner.buf == NULL) {
		return NULL;
	}
	/* Register before qstr/reader/lexer allocations can raise. A failed lexer
	 * constructor leaves its partial objects for GC, but closes the file reader
	 * and source immediately. After success mp_parse owns lexer cleanup.
	 */
	nlr_push_jump_callback(&owner.callback, mpport_file_cleanup64);
	if (source_name == MP_QSTRnull) {
		source_name = qstr_from_str(path);
	}
	mp_reader_new_mem(&owner.reader, owner.buf, owner.size,
		owner.size == 0 ? 1 : owner.size);
	owner.reader_ready = 1;
	lex = mp_lexer_new(source_name, owner.reader);
	nlr_pop_jump_callback(false);
	return lex;
}

mp_lexer_t *mpport_file_lexer64(const char *path)
{
	return mpport_file_lexer_owned64(path, MP_QSTRnull);
}

/* Core import hook; preserve its public signature and ENOENT convention. */
mp_lexer_t *mp_lexer_new_from_file(qstr filename)
{
	mp_lexer_t *lex = mpport_file_lexer_owned64(qstr_str(filename), filename);

	if (lex == NULL) {
		mp_raise_OSError(MP_ENOENT);
	}
	return lex;
}

mp_obj_t mpport_file_text64(const char *path)
{
	struct MPPORT_FILE_OWNER64 owner = { 0 };
	mp_obj_t text;

	owner.buf = mpport_load_file(path, &owner.size);
	if (owner.buf == NULL) {
		mp_raise_OSError(MP_ENOENT);
	}
	/* mp_obj_new_str copies the bytes (or returns an existing qstr); UTF or
	 * allocation exceptions must also release this temporary source buffer.
	 */
	nlr_push_jump_callback(&owner.callback, mpport_file_cleanup64);
	text = mp_obj_new_str((const char *) owner.buf, owner.size);
	nlr_pop_jump_callback(true);
	return text;
}
