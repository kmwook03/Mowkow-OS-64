#ifndef MOWKOW64_MPPORT_FILE64_MOCK_H
#define MOWKOW64_MPPORT_FILE64_MOCK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Keep the real reader and NLR implementation; mock allocation/VM objects only. */
typedef uint8_t byte;
typedef uintptr_t mp_uint_t;
typedef size_t qstr;
typedef void *mp_obj_t;

#include "py/nlr.h"
#include "py/reader.h"

#define MP_QSTRnull 0
#define MP_EINVAL 22
#define MP_EIO 5
#define MP_ENOENT 2

#define m_new(type, count) ((type *) m_malloc(sizeof(type) * (count)))
#define m_new_obj(type) m_new(type, 1)
#define m_del(type, ptr, count) m_free(ptr, sizeof(type) * (count))
#define m_del_obj(type, ptr) m_del(type, ptr, 1)

struct MPPORT_MOCK_STATE64 {
	nlr_buf_t *nlr_top;
	nlr_jump_callback_node_t *nlr_jump_callback_top;
};
extern struct MPPORT_MOCK_STATE64 fixture_state64;
#define MP_STATE_THREAD(field) (fixture_state64.field)

/* A minimal VM lexer object, not a substitute parser. */
struct MPPORT_MOCK_LEXER64 {
	qstr source_name;
	mp_reader_t reader;
};
typedef struct MPPORT_MOCK_LEXER64 mp_lexer_t;

void *m_malloc(size_t size);
void m_free(void *ptr, size_t size);
_Noreturn void mp_raise_OSError(int error);
qstr qstr_from_str(const char *path);
const char *qstr_str(qstr value);
mp_lexer_t *mp_lexer_new(qstr source, mp_reader_t reader);
mp_lexer_t *mp_lexer_new_from_file(qstr filename);
void mp_lexer_free(mp_lexer_t *lex);
mp_obj_t mp_obj_new_str(const char *data, size_t size);

#endif
