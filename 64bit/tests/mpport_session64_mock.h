#ifndef MOWKOW64_MPPORT_SESSION64_MOCK_H
#define MOWKOW64_MPPORT_SESSION64_MOCK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "py/nlr.h"

typedef size_t qstr;
typedef void *mp_obj_t;

/* Mirror only the VM boundary needed by the real port and upstream NLR code. */
typedef struct {
	nlr_buf_t *nlr_top;
	nlr_jump_callback_node_t *nlr_jump_callback_top;
	void *dict_locals;
	void *dict_globals;
	void *stack_top;
	volatile mp_obj_t mp_pending_exception;
	size_t stack_limit;
	unsigned int gc_lock_depth;
} mp_state_thread_t;

struct MPPORT_SESSION_VM64 { uintptr_t roots[8]; };
struct MPPORT_SESSION_MEM64 { uintptr_t roots[8]; };
struct MPPORT_SESSION_CTX64 {
	mp_state_thread_t thread;
	struct MPPORT_SESSION_VM64 vm;
	struct MPPORT_SESSION_MEM64 mem;
};
extern struct MPPORT_SESSION_CTX64 mp_state_ctx;
#define MP_STATE_THREAD(field) (mp_state_ctx.thread.field)
#define MP_OBJ_FROM_PTR(ptr) (ptr)

struct MPPORT_SESSION_TYPE64 { int kind; };
typedef struct MPPORT_SESSION_TYPE64 mp_obj_type_t;
extern const mp_obj_type_t mp_type_KeyboardInterrupt, mp_type_SystemExit;
extern int mp_plat_print;

struct MPPORT_SESSION_LEXER64 { qstr source_name; };
typedef struct MPPORT_SESSION_LEXER64 mp_lexer_t;
struct MPPORT_SESSION_PARSE64 { int root; };
typedef struct MPPORT_SESSION_PARSE64 mp_parse_tree_t;
typedef enum { MP_IMPORT_STAT_NO_EXIST, MP_IMPORT_STAT_FILE } mp_import_stat_t;
#define MP_PARSE_FILE_INPUT 1

void gc_init(void *start, void *end);
void mp_init(void);
void mp_cstack_init_with_top(void *top, size_t size);
int pyexec_friendly_repl(void);
qstr qstr_from_str(const char *source);
mp_lexer_t *mp_lexer_new_from_str_len(qstr source, const char *text,
	size_t size, size_t free_size);
mp_parse_tree_t mp_parse(mp_lexer_t *lex, int mode);
mp_obj_t mp_compile(mp_parse_tree_t *tree, qstr source, bool repl);
mp_obj_t mp_call_function_0(mp_obj_t function);
const mp_obj_type_t *mp_obj_get_type(mp_obj_t value);
void mp_obj_print_exception(const void *print, mp_obj_t value);
void gc_collect_start(void);
void gc_collect_end(void);
void gc_helper_collect_regs_and_stack(void);

#endif
