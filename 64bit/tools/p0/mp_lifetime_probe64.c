/* Host-only second P0 experiment. No surrogate objects or registry GC roots.
 * The per-block table deliberately trades static RAM for a capacity proof:
 * every exception in the fixed heap has a slot, plus one emergency slot.
 * This is not a production memory budget or a complete message catalog.
 */
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "py/compile.h"
#include "py/gc.h"
#include "py/objexcept.h"
#include "py/objstr.h"
#include "mp_lifetime_probe64.h"
#if P0_COMPACT64
#include "mp_origin_store64.h"
#endif

#define HEAP_SIZE64 (2 * 1024 * 1024)
#define SLOT_COUNT64 (HEAP_SIZE64 / MICROPY_BYTES_PER_GC_BLOCK)

enum MESSAGE64 {
	MESSAGE_NONE64, MESSAGE_SYNTAX64, MESSAGE_NAME64, MESSAGE_CALL64,
	MESSAGE_LOCK64, MESSAGE_MEMORY64, MESSAGE_EIO64,
	MESSAGE_FILENAME64, MESSAGE_RECURSION64,
	MESSAGE_CALL_ARGS64, MESSAGE_FILENAME_QSTR64,
	MESSAGE_REJECT64 = 255,
};

static const struct MESSAGE_ENTRY64 {
	const char *format;
	enum MESSAGE64 message;
} messages64[] = {
	{ "invalid syntax", MESSAGE_SYNTAX64 },
	{ "name '%q' isn't defined", MESSAGE_NAME64 },
	{ "'%s' object isn't callable", MESSAGE_CALL64 },
	{ "memory allocation failed, heap is locked", MESSAGE_LOCK64 },
	{ "memory allocation failed, allocating " SIZE_FMT " bytes", MESSAGE_MEMORY64 },
};

struct WEAK_ORIGIN64 {
	const mp_obj_tuple_t *args;
	size_t value;
	enum MESSAGE64 message;
};

bool lifetime_enabled64;
#if P0_COMPACT64
static uint8_t origin_ids64[SLOT_COUNT64 + 1];
static uint64_t origin_values64[SLOT_COUNT64 + 1];
static struct MP_ORIGIN_STORE64 origin_store64;
_Static_assert(P0_BOUNDARY64, "compact provenance requires complete constructor interposition");
_Static_assert(sizeof(origin_ids64) + sizeof(origin_values64) + sizeof(origin_store64) <=
	HEAP_SIZE64 / 10 * 3, "provenance metadata exceeds the candidate 30-percent budget");
#else
static struct WEAK_ORIGIN64 weak64[SLOT_COUNT64 + 1];
#endif
static uintptr_t heap_start64;
static mp_obj_t roots64[256];
static void *fill64[SLOT_COUNT64];
static size_t fill_count64;
static unsigned allocations64;
static unsigned make_new_calls64;
static unsigned missed64;
static bool korean64 = true;
static char text64[8192];
static size_t text_size64;
static bool nested64;
static mp_obj_t nested_exception64;
static char nested_text64[256];
static size_t nested_size64;

void *__real_gc_alloc(size_t size, unsigned flags);
void *__wrap_gc_alloc(size_t size, unsigned flags);
mp_obj_t __real_mp_obj_exception_make_new(const mp_obj_type_t *type, size_t count,
	size_t keywords, const mp_obj_t *args);
mp_obj_t __wrap_mp_obj_exception_make_new(const mp_obj_type_t *type, size_t count,
	size_t keywords, const mp_obj_t *args);
mp_obj_t __real_mp_obj_new_exception_msg(const mp_obj_type_t *type, mp_rom_error_text_t msg);
mp_obj_t __real_mp_obj_new_exception_msg_vlist(const mp_obj_type_t *type,
	mp_rom_error_text_t fmt, va_list args);
mp_obj_t __real_mp_obj_new_exception_args(const mp_obj_type_t *type, size_t count,
	const mp_obj_t *args);
void __real_mp_obj_print_exception(const mp_print_t *print, mp_obj_t exception);

#if P0_BOUNDARY64
mp_obj_t __real_mp_obj_new_exception(const mp_obj_type_t *type);
MP_NORETURN void __real_mp_raise_type_arg(const mp_obj_type_t *type, mp_obj_t arg);
MP_NORETURN void __real_mp_raise_OSError_with_filename(int error, const char *filename);
static unsigned type_arg_calls64;
static unsigned filename_calls64;
#endif

#if !P0_COMPACT64
static struct WEAK_ORIGIN64 *slot64(mp_obj_t exception)
{
	uintptr_t address = (uintptr_t) MP_OBJ_TO_PTR(exception);

	if (address == (uintptr_t) &MP_STATE_VM(mp_emergency_exception_obj))
		return &weak64[SLOT_COUNT64];
	assert(address >= heap_start64 && address - heap_start64 < HEAP_SIZE64);
	return &weak64[(address - heap_start64) / MICROPY_BYTES_PER_GC_BLOCK];
}
#endif

static struct WEAK_ORIGIN64 read_origin64(mp_obj_t exception)
{
#if P0_COMPACT64
	struct MP_ORIGIN_VALUE64 value;

	assert(mp_origin_get64(&origin_store64, (uintptr_t) MP_OBJ_TO_PTR(exception), &value) == 0);
	return (struct WEAK_ORIGIN64) { .message = value.id, .value = value.value };
#else
	return *slot64(exception);
#endif
}

static void drop_origin64(mp_obj_t exception)
{
#if P0_COMPACT64
	assert(mp_origin_drop64(&origin_store64, (uintptr_t) MP_OBJ_TO_PTR(exception)) == 0);
#else
	memset(slot64(exception), 0, sizeof(struct WEAK_ORIGIN64));
#endif
}

static size_t storage_bytes64(void)
{
#if P0_COMPACT64
	return sizeof(origin_ids64) + sizeof(origin_values64) + sizeof(origin_store64);
#else
	return sizeof(weak64);
#endif
}

void *__wrap_gc_alloc(size_t size, unsigned flags)
{
	void *result = __real_gc_alloc(size, flags);

	if (lifetime_enabled64) {
		allocations64++;
		/* Successful address reuse invalidates old provenance before use.
		 * No object or DMA ownership is inferred from a failed allocation.
		 */
		if (result != NULL)
			drop_origin64(MP_OBJ_FROM_PTR(result));
	}
	return result;
}

mp_obj_t __wrap_mp_obj_exception_make_new(const mp_obj_type_t *type, size_t count,
	size_t keywords, const mp_obj_t *args)
{
	mp_obj_t exception = __real_mp_obj_exception_make_new(type, count, keywords, args);

	if (lifetime_enabled64) {
		make_new_calls64++;
		drop_origin64(exception);
	}
	return exception;
}

static enum MESSAGE64 message64(const char *format)
{
	if (format == NULL)
		return MESSAGE_NONE64;
	for (size_t i = 0; i < sizeof(messages64) / sizeof(messages64[0]); i++) {
		if (strcmp(format, messages64[i].format) == 0)
			return messages64[i].message;
	}
	return MESSAGE_NONE64;
}

static void remember64(mp_obj_t exception, enum MESSAGE64 message, size_t value)
{
#if P0_COMPACT64
	assert(mp_origin_put64(&origin_store64, (uintptr_t) MP_OBJ_TO_PTR(exception),
		(struct MP_ORIGIN_VALUE64) { .id = (uint8_t) message, .value = value }) == 0);
#else
	mp_obj_exception_t *native = MP_OBJ_TO_PTR(exception);

	*slot64(exception) = (struct WEAK_ORIGIN64) {
		.args = native->args, .message = message, .value = value,
	};
#endif
}

#if P0_BOUNDARY64
mp_obj_t mp_obj_new_exception(const mp_obj_type_t *type)
{
	return __real_mp_obj_new_exception(type);
}

static void builtin_args64(mp_obj_t exception, const mp_obj_type_t *type,
	size_t count, const mp_obj_t *args)
{
	if (type == &mp_type_OSError && count == 1 && args[0] == MP_OBJ_NEW_SMALL_INT(5))
		remember64(exception, MESSAGE_EIO64, 0);
	else if (type == &mp_type_RuntimeError && count == 1 && args[0] ==
		MP_OBJ_NEW_QSTR(MP_QSTR_maximum_space_recursion_space_depth_space_exceeded))
		remember64(exception, MESSAGE_RECURSION64, 0);
}

MP_NORETURN void mp_raise_type_arg(const mp_obj_type_t *type, mp_obj_t arg)
{
	nlr_buf_t nlr;

	type_arg_calls64++;
	if (nlr_push(&nlr) == 0) {
		__real_mp_raise_type_arg(type, arg);
	} else {
		mp_obj_t exception = MP_OBJ_FROM_PTR(nlr.ret_val);

		/* A secondary constructor failure belongs to its actual exception. */
		if (lifetime_enabled64 && mp_obj_get_type(exception) == type)
			builtin_args64(exception, type, 1, &arg);
		nlr_raise(exception);
	}
}

MP_NORETURN void mp_raise_OSError_with_filename(int error, const char *filename)
{
	nlr_buf_t nlr;
#if P0_COMPACT64
	qstr name = filename == NULL ? MP_QSTRnull : qstr_find_strn(filename, strlen(filename));
#endif

	filename_calls64++;
	if (nlr_push(&nlr) == 0) {
		__real_mp_raise_OSError_with_filename(error, filename);
	} else {
		mp_obj_t exception = MP_OBJ_FROM_PTR(nlr.ret_val);

		if (lifetime_enabled64 && mp_obj_get_type(exception) == &mp_type_OSError) {
			const mp_obj_exception_t *native = MP_OBJ_TO_PTR(exception);

			if (native->args->len == 2) {
				remember64(exception, MESSAGE_FILENAME64, (size_t) error);
			}
#if P0_COMPACT64
			else if (name != MP_QSTRnull && name <= UINT32_MAX) {
				remember64(exception, MESSAGE_FILENAME_QSTR64,
					((uint64_t) (uint32_t) error << 32) | (uint32_t) name);
			} else {
				/* The original discarded its tuple. No unowned filename
				 * pointer may survive as if it still had an owner.
				 */
				remember64(exception, MESSAGE_REJECT64, 61);
			}
#endif
		}
		nlr_raise(exception);
	}
}
#endif

mp_obj_t lifetime_msg64(const mp_obj_type_t *type, mp_rom_error_text_t msg)
{
	mp_obj_t exception = __real_mp_obj_new_exception_msg(type, msg);

	remember64(exception, message64((const char *) msg), 0);
	return exception;
}

mp_obj_t lifetime_vlist64(const mp_obj_type_t *type, mp_rom_error_text_t fmt, va_list args)
{
	enum MESSAGE64 message = message64((const char *) fmt);
	size_t value = 0;
	va_list copy;
#if P0_COMPACT64
	const char *type_name = NULL;
	size_t type_len = 0;
#endif

	va_copy(copy, args);
	if (message == MESSAGE_MEMORY64)
		value = va_arg(copy, size_t);
	else if (message == MESSAGE_NAME64)
		value = va_arg(copy, qstr);
#if P0_COMPACT64
	else if (message == MESSAGE_CALL64) {
		type_name = va_arg(copy, const char *);
		if (type_name != NULL) {
			type_len = strlen(type_name);
			value = qstr_find_strn(type_name, type_len);
		}
	}
#endif
	va_end(copy);
	mp_obj_t exception = __real_mp_obj_new_exception_msg_vlist(type, fmt, args);

#if P0_COMPACT64
	if (message == MESSAGE_CALL64 && value == MP_QSTRnull) {
		const mp_obj_exception_t *native = MP_OBJ_TO_PTR(exception);
		bool owned = false;

		if (type_name != NULL && native->args->len == 1 &&
			mp_obj_is_str(native->args->items[0])) {
			size_t len;
			const char *data = mp_obj_str_get_data(native->args->items[0], &len);
			const size_t suffix = sizeof("' object isn't callable") - 1;

			owned = len >= suffix + 1 && len - suffix - 1 == type_len &&
				data[0] == '\'' && memcmp(data + 1, type_name, type_len) == 0 &&
				memcmp(data + len - suffix, "' object isn't callable", suffix) == 0;
		}
		message = owned ? MESSAGE_CALL_ARGS64 : MESSAGE_REJECT64;
		value = owned ? 0 : 61;
	}
#endif
	remember64(exception, message, value);
	return exception;
}

mp_obj_t lifetime_args64(const mp_obj_type_t *type, size_t count, const mp_obj_t *args)
{
	mp_obj_t exception = __real_mp_obj_new_exception_args(type, count, args);

	remember64(exception, type == &mp_type_OSError && count == 1 &&
		args[0] == MP_OBJ_NEW_SMALL_INT(5) ? MESSAGE_EIO64 : MESSAGE_NONE64, 0);
#if P0_BOUNDARY64
	builtin_args64(exception, type, count, args);
#endif
	return exception;
}

void lifetime_roots64(void)
{
	/* Only test-owned live objects and exhaustion blocks are roots. */
	gc_collect_root((void **) roots64, sizeof(roots64) / sizeof(void *));
	gc_collect_root(fill64, fill_count64);
}

void lifetime_sweep64(void)
{
	/* Runs after the actual sweep, before any subsequent allocation. The
	 * single-threaded fixture disables finalisers and split heaps. Scanning
	 * offsets avoids putting temporary object addresses on the root stack.
	 */
	for (size_t i = 0; i < SLOT_COUNT64; i++) {
		uintptr_t address = heap_start64 + i * MICROPY_BYTES_PER_GC_BLOCK;

		if (read_origin64(MP_OBJ_FROM_PTR(address)).message != MESSAGE_NONE64 &&
			gc_nbytes((void *) address) == 0)
			drop_origin64(MP_OBJ_FROM_PTR(address));
	}
}

bool lifetime_print64(const mp_print_t *print, mp_obj_t exception)
{
	if (!korean64 || !mp_obj_is_native_exception_instance(exception))
		return false;
	/* Copy provenance before output: a sink can reenter the reporter. */
	struct WEAK_ORIGIN64 origin = read_origin64(exception);
	const mp_obj_exception_t *native = MP_OBJ_TO_PTR(exception);

	if (origin.message == MESSAGE_NONE64 || origin.message == MESSAGE_REJECT64 ||
		(!P0_COMPACT64 && origin.args != native->args)) {
		missed64++;
		return false;
	}
	mp_printf(print, "%q: ", native->base.type->name);
	switch (origin.message) {
	case MESSAGE_SYNTAX64:
		mp_print_str(print, "잘못된 문법입니다");
		break;
	case MESSAGE_NAME64:
		mp_printf(print, "이름 '%q'이 정의되지 않았습니다", (qstr) origin.value);
		break;
	case MESSAGE_CALL64: {
#if P0_COMPACT64
		mp_printf(print, "'%q' 객체는 호출할 수 없습니다", (qstr) origin.value);
		break;
#endif
		/* fall through to a string owned by the original exception */
	}
	case MESSAGE_CALL_ARGS64: {
		/* Borrow the original formatted argument only while its owner lives.
		 * No fixed-size copy truncates a user's arbitrarily long type name.
		 */
		assert(native->args->len == 1);
		size_t len;
		const char *data = mp_obj_str_get_data(native->args->items[0], &len);
		const size_t suffix = sizeof("' object isn't callable") - 1;

		assert(len >= suffix + 1 && data[0] == '\'');
		print->print_strn(print->data, data, len - suffix + 1);
		mp_print_str(print, " 객체는 호출할 수 없습니다");
		break;
	}
	case MESSAGE_LOCK64:
		mp_print_str(print, "힙이 잠겨 메모리를 할당할 수 없습니다");
		break;
	case MESSAGE_MEMORY64:
		mp_printf(print, SIZE_FMT " 바이트 메모리를 할당할 수 없습니다", origin.value);
		break;
	case MESSAGE_EIO64:
		mp_print_str(print, "[Errno 5] 입출력 오류입니다");
		break;
	case MESSAGE_RECURSION64:
		mp_print_str(print, "최대 재귀 깊이를 초과했습니다");
		break;
	case MESSAGE_FILENAME64: {
		assert(native->args->len == 2);
		size_t len;
		const char *data = mp_obj_str_get_data(native->args->items[1], &len);
		const size_t prefix = sizeof("can't open ") - 1;

		assert(len >= prefix && memcmp(data, "can't open ", prefix) == 0);
		mp_print_str(print, "파일 '");
		print->print_strn(print->data, data + prefix, len - prefix);
		mp_printf(print, "'을 열 수 없습니다 [Errno %d]", (int) origin.value);
		break;
	}
	case MESSAGE_FILENAME_QSTR64:
		mp_printf(print, "파일 '%q'을 열 수 없습니다 [Errno %d]",
			(qstr) (uint32_t) origin.value, (int32_t) (origin.value >> 32));
		break;
	default:
		assert(false);
	}
	mp_print_str(print, "\n");
	return true;
}

static void nested_sink64(void *data, const char *str, size_t len)
{
	(void) data;
	assert(len < sizeof(nested_text64) - nested_size64);
	memcpy(nested_text64 + nested_size64, str, len);
	nested_size64 += len;
	nested_text64[nested_size64] = '\0';
}

static void sink64(void *data, const char *str, size_t len)
{
	(void) data;
	assert(len < sizeof(text64) - text_size64);
	memcpy(text64 + text_size64, str, len);
	text_size64 += len;
	text64[text_size64] = '\0';
	if (nested64) {
		nested64 = false;
		nested_size64 = 0;
		const mp_print_t inner = { NULL, nested_sink64 };

		assert(lifetime_print64(&inner, nested_exception64));
		assert(strcmp(nested_text64,
			"NameError: 이름 'missing_identifier'이 정의되지 않았습니다\n") == 0);
		/* Change the next report's language during this report. */
		korean64 = false;
	}
}

static void report64(mp_obj_t exception, const char *expected)
{
	const mp_print_t print = { NULL, sink64 };
	gc_info_t before;
	gc_info_t after;
	unsigned calls = allocations64;
	mp_obj_tuple_t *args = ((mp_obj_exception_t *) MP_OBJ_TO_PTR(exception))->args;

	text_size64 = 0;
	text64[0] = '\0';
	gc_info(&before);
	assert(lifetime_print64(&print, exception));
	gc_info(&after);
	assert(calls == allocations64 && before.used == after.used);
	assert(((mp_obj_exception_t *) MP_OBJ_TO_PTR(exception))->args == args);
	assert(strcmp(text64, expected) == 0);
}

static size_t count64(void)
{
	size_t count = 0;

	for (size_t i = 0; i < SLOT_COUNT64; i++)
		count += read_origin64(MP_OBJ_FROM_PTR(heap_start64 +
			i * MICROPY_BYTES_PER_GC_BLOCK)).message != MESSAGE_NONE64;
	count += read_origin64(MP_OBJ_FROM_PTR(&MP_STATE_VM(mp_emergency_exception_obj))).message !=
		MESSAGE_NONE64;
	return count;
}

static mp_obj_t compile64(const char *source)
{
	mp_lexer_t *lexer = mp_lexer_new_from_str_len(MP_QSTR__lt_stdin_gt_,
		source, strlen(source), 0);
	mp_parse_tree_t tree = mp_parse(lexer, MP_PARSE_FILE_INPUT);

	return mp_compile(&tree, MP_QSTR__lt_stdin_gt_, false);
}

static void syntax64(void)
{
	for (size_t i = 0; i < 256; i++) {
		roots64[i] = mp_obj_new_exception_msg(&mp_type_SyntaxError,
			MP_ERROR_TEXT("invalid syntax"));
		report64(roots64[i], "SyntaxError: 잘못된 문법입니다\n");
	}
	gc_collect();
	assert(count64() == 256);
	for (size_t i = 0; i < 256; i++) {
		assert(gc_nbytes(MP_OBJ_TO_PTR(roots64[i])) >= sizeof(mp_obj_exception_t));
		report64(roots64[i], "SyntaxError: 잘못된 문법입니다\n");
	}
}

#if P0_BOUNDARY64
static mp_obj_t vlist64(const mp_obj_type_t *type, mp_rom_error_text_t fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	mp_obj_t exception = mp_obj_new_exception_msg_vlist(type, fmt, args);

	va_end(args);
	return exception;
}

static void boundary_identity64(void)
{
	const mp_obj_type_t *types[] = {
		&mp_type_MemoryError, &mp_type_OSError, &mp_type_SyntaxError,
		&mp_type_TypeError, &mp_type_NameError, &mp_type_BaseException,
	};

	for (size_t i = 0; i < sizeof(types) / sizeof(types[0]); i++) {
		assert(MP_OBJ_TYPE_GET_SLOT(types[i], make_new) == mp_obj_exception_make_new);
		assert(MP_OBJ_TYPE_GET_SLOT(types[i], make_new) != __real_mp_obj_exception_make_new);
	}
	unsigned before = make_new_calls64;

	roots64[0] = mp_obj_new_exception(&mp_type_MemoryError);
	roots64[1] = mp_obj_new_exception_args(&mp_type_SyntaxError, 0, NULL);
	roots64[2] = mp_obj_new_exception_arg1(&mp_type_MemoryError, MP_OBJ_NEW_SMALL_INT(1));
	roots64[3] = mp_obj_new_exception_msg(&mp_type_SyntaxError, MP_ERROR_TEXT("invalid syntax"));
	roots64[4] = mp_obj_new_exception_msg_varg(&mp_type_SyntaxError,
		MP_ERROR_TEXT("invalid syntax"));
	roots64[5] = vlist64(&mp_type_MemoryError,
		MP_ERROR_TEXT("memory allocation failed, allocating " SIZE_FMT " bytes"), (size_t) 17);
	assert(make_new_calls64 == before + 6);
	for (size_t i = 0; i < 6; i++)
		assert(mp_obj_is_native_exception_instance(roots64[i]));
	report64(roots64[3], "SyntaxError: 잘못된 문법입니다\n");
	report64(roots64[4], "SyntaxError: 잘못된 문법입니다\n");
	report64(roots64[5], "MemoryError: 17 바이트 메모리를 할당할 수 없습니다\n");
	/* Enter the original varg body to exercise its real same-TU vlist call. */
	mp_obj_t __real_mp_obj_new_exception_msg_varg(const mp_obj_type_t *,
		mp_rom_error_text_t, ...);

	roots64[6] = __real_mp_obj_new_exception_msg_varg(&mp_type_MemoryError,
		MP_ERROR_TEXT("memory allocation failed, allocating " SIZE_FMT " bytes"), (size_t) 29);
	assert(make_new_calls64 == before + 7);
	report64(roots64[6], "MemoryError: 29 바이트 메모리를 할당할 수 없습니다\n");
	puts("BOUNDARY identity: public slots/inline checks agree; all constructor families and original same-TU varg/vlist observed");
}

static void boundary_raises64(void)
{
	nlr_buf_t nlr;
	unsigned before = make_new_calls64;
	unsigned raised = type_arg_calls64;

	if (nlr_push(&nlr) == 0) {
		mp_raise_OSError(5);
		assert(false);
	} else {
		roots64[0] = MP_OBJ_FROM_PTR(nlr.ret_val);
		assert(make_new_calls64 == before + 1 && type_arg_calls64 == raised + 1);
		report64(roots64[0], "OSError: [Errno 5] 입출력 오류입니다\n");
	}
	before = make_new_calls64;
	if (nlr_push(&nlr) == 0) {
		mp_raise_OSError_with_filename(5, "사용자.py");
		assert(false);
	} else {
		roots64[1] = MP_OBJ_FROM_PTR(nlr.ret_val);
		assert(make_new_calls64 == before + 1 && filename_calls64 == 1);
		report64(roots64[1], "OSError: 파일 '사용자.py'을 열 수 없습니다 [Errno 5]\n");
		const mp_obj_exception_t *native = MP_OBJ_TO_PTR(roots64[1]);

		assert(native->args->len == 2 && native->args->items[0] == MP_OBJ_NEW_SMALL_INT(5));
		assert(strcmp(mp_obj_str_get_str(native->args->items[1]), "can't open 사용자.py") == 0);
	}
	before = make_new_calls64;
	raised = type_arg_calls64;
	if (nlr_push(&nlr) == 0) {
		mp_raise_recursion_depth();
		assert(false);
	} else {
		roots64[2] = MP_OBJ_FROM_PTR(nlr.ret_val);
		assert(make_new_calls64 == before + 1 && type_arg_calls64 == raised + 1);
		report64(roots64[2], "RuntimeError: 최대 재귀 깊이를 초과했습니다\n");
	}
	gc_collect();
	report64(roots64[0], "OSError: [Errno 5] 입출력 오류입니다\n");
	report64(roots64[1], "OSError: 파일 '사용자.py'을 열 수 없습니다 [Errno 5]\n");
	report64(roots64[2], "RuntimeError: 최대 재귀 깊이를 초과했습니다\n");
	puts("BOUNDARY raises: real OSError/filename/recursion, constructor counts, original args, GC and alloc-free reports PASS");
}

static void boundary_users64(void)
{
	static const char *const sources[] = {
		"raise SyntaxError('invalid syntax')\n",
		"raise OSError(5)\n",
		"raise RuntimeError('maximum recursion depth exceeded')\n",
		"class Custom(ValueError):\n pass\nraise Custom('사용자 문자열')\n",
	};
	static const char *const expected[] = {
		"SyntaxError: invalid syntax\n", "OSError: 5\n",
		"RuntimeError: maximum recursion depth exceeded\n", "Custom: 사용자 문자열\n",
	};
	const mp_print_t print = { NULL, sink64 };
	nlr_buf_t nlr;

	for (size_t i = 0; i < sizeof(sources) / sizeof(sources[0]); i++) {
		roots64[0] = compile64(sources[i]);
		unsigned before = make_new_calls64;

		if (nlr_push(&nlr) == 0) {
			mp_call_function_0(roots64[0]);
			assert(false);
		} else {
			roots64[i + 1] = MP_OBJ_FROM_PTR(nlr.ret_val);
			assert(make_new_calls64 == before + 1);
			assert(!lifetime_print64(&print, roots64[i + 1]));
			text_size64 = 0;
			__real_mp_obj_print_exception(&print, roots64[i + 1]);
			assert(strstr(text64, expected[i]) != NULL);
		}
	}
	/* Re-raising an existing builtin must retain provenance without a new
	 * constructor generation, including after real GC and Python checks.
	 */
	roots64[5] = mp_obj_new_exception_args(&mp_type_OSError, 1,
		(mp_obj_t[]) { MP_OBJ_NEW_SMALL_INT(5) });
	mp_store_global(qstr_from_str("builtin_saved"), roots64[5]);
	roots64[0] = compile64("assert str(builtin_saved) == '5'\n"
		"assert builtin_saved.args == (5,)\nraise builtin_saved\n");
	gc_collect();
	unsigned before = make_new_calls64;

	if (nlr_push(&nlr) == 0) {
		mp_call_function_0(roots64[0]);
		assert(false);
	} else {
		assert(make_new_calls64 == before && MP_OBJ_FROM_PTR(nlr.ret_val) == roots64[5]);
		report64(roots64[5], "OSError: [Errno 5] 입출력 오류입니다\n");
	}
	puts("BOUNDARY users: real Python constructors/subclass preserve text; reraised builtin preserves identity/str/args/provenance");
}

static void boundary_secondary64(void)
{
	nlr_buf_t nlr;

	gc_lock();
	if (nlr_push(&nlr) == 0) {
		mp_raise_OSError_with_filename(5, "사용자.py");
		assert(false);
	} else {
		roots64[0] = MP_OBJ_FROM_PTR(nlr.ret_val);
		assert(mp_obj_get_type(roots64[0]) == &mp_type_MemoryError);
		report64(roots64[0], "MemoryError: 힙이 잠겨 메모리를 할당할 수 없습니다\n");
	}
	if (nlr_push(&nlr) == 0) {
		mp_raise_OSError(5);
		assert(false);
	} else {
		roots64[0] = MP_OBJ_FROM_PTR(nlr.ret_val);
		assert(mp_obj_get_type(roots64[0]) == &mp_type_OSError);
		report64(roots64[0], "OSError: [Errno 5] 입출력 오류입니다\n");
	}
	gc_unlock();
	/* Wrappers must leave the caller's NLR chain usable after both paths. */
	if (nlr_push(&nlr) == 0) {
		mp_raise_recursion_depth();
		assert(false);
	} else {
		report64(MP_OBJ_FROM_PTR(nlr.ret_val), "RuntimeError: 최대 재귀 깊이를 초과했습니다\n");
	}
	puts("BOUNDARY secondary: filename OOM preserves actual MemoryError; locked EIO and subsequent NLR recovery PASS");
}
#endif

#if P0_COMPACT64
static void compact_fill64(size_t leave)
{
	for (;;) {
		void *block = gc_alloc(1, 0);

		if (block == NULL)
			break;
		assert(fill_count64 < SLOT_COUNT64);
		fill64[fill_count64++] = block;
	}
	gc_info_t info;

	gc_info(&info);
	assert(info.free == 0 && fill_count64 >= leave);
	for (size_t i = 0; i < leave; i++) {
		size_t index = fill_count64 - 1 - i;

		gc_free(fill64[index]);
		fill64[index] = NULL;
	}
	gc_info(&info);
	assert(info.free == leave * MICROPY_BYTES_PER_GC_BLOCK);
}

static void compact_partial64(size_t leave, bool fragmented)
{
	compact_fill64(fragmented ? 0 : leave);
	if (fragmented) {
		/* One string block, a two-block formatter buffer, then isolated
		 * native/tuple blocks. There is no room to grow or move the buffer.
		 */
		static const size_t offsets[] = { 12, 10, 9, 6, 3 };

		assert(fill_count64 >= 12 && leave == 5);
		for (size_t i = 0; i < sizeof(offsets) / sizeof(offsets[0]); i++) {
			size_t index = fill_count64 - offsets[i];

			gc_free(fill64[index]);
			fill64[index] = NULL;
		}
		gc_info_t info;

		gc_info(&info);
		assert(info.free == 5 * MICROPY_BYTES_PER_GC_BLOCK);
	}
	roots64[0] = mp_obj_new_exception_msg_varg(&mp_type_MemoryError,
		MP_ERROR_TEXT("memory allocation failed, allocating " SIZE_FMT " bytes"), SIZE_MAX);
	const mp_obj_exception_t *native = MP_OBJ_TO_PTR(roots64[0]);
	bool truncated = native->args->len == 1 && strcmp(
		mp_obj_str_get_str(native->args->items[0]),
		"memory allocation failed, allocating 18446744073709551615 bytes") != 0;

	report64(roots64[0], "MemoryError: 18446744073709551615 바이트 메모리를 할당할 수 없습니다\n");
	gc_collect();
	report64(roots64[0], "MemoryError: 18446744073709551615 바이트 메모리를 할당할 수 없습니다\n");
	printf("COMPACT_PARTIAL64 free_blocks=%zu emergency=%u args=%zu truncated=%u fragmented=%u\n",
		leave, roots64[0] == MP_OBJ_FROM_PTR(&MP_STATE_VM(mp_emergency_exception_obj)),
		native->args->len, truncated, fragmented);
}

static void compact_typed64(void)
{
	qstr name = qstr_from_str("동적_식별자_79271593");
	qstr type = qstr_from_str("동적_클래스_79271593");

	gc_collect();
	gc_lock();
	roots64[0] = mp_obj_new_exception_msg_varg(&mp_type_NameError,
		MP_ERROR_TEXT("name '%q' isn't defined"), name);
	assert(((mp_obj_exception_t *) MP_OBJ_TO_PTR(roots64[0]))->args->len == 0);
	report64(roots64[0], "NameError: 이름 '동적_식별자_79271593'이 정의되지 않았습니다\n");
	roots64[0] = mp_obj_new_exception_msg_varg(&mp_type_TypeError,
		MP_ERROR_TEXT("'%s' object isn't callable"), qstr_str(type));
	assert(((mp_obj_exception_t *) MP_OBJ_TO_PTR(roots64[0]))->args->len == 0);
	report64(roots64[0], "TypeError: '동적_클래스_79271593' 객체는 호출할 수 없습니다\n");
	roots64[0] = mp_obj_new_exception_msg_varg(&mp_type_MemoryError,
		MP_ERROR_TEXT("memory allocation failed, allocating " SIZE_FMT " bytes"), (size_t) 0);
	report64(roots64[0], "MemoryError: 0 바이트 메모리를 할당할 수 없습니다\n");
	char unowned[64] = "unowned_type_79271593";

	assert(qstr_find_strn(unowned, strlen(unowned)) == MP_QSTRnull);
	roots64[0] = mp_obj_new_exception_msg_varg(&mp_type_TypeError,
		MP_ERROR_TEXT("'%s' object isn't callable"), unowned);
	memset(unowned, 'X', sizeof(unowned));
	assert(read_origin64(roots64[0]).message == MESSAGE_REJECT64);
	assert(read_origin64(roots64[0]).value == 61);
	const mp_print_t print = { NULL, sink64 };

	assert(!lifetime_print64(&print, roots64[0]));
	gc_unlock();
	puts("COMPACT typed: scalar zero/full-width and qstr values survive lost args; unowned lost string explicitly rejected");
}

static void compact_owned64(void)
{
	char transient[64] = "transient_type_79271593";

	assert(qstr_find_strn(transient, strlen(transient)) == MP_QSTRnull);
	roots64[0] = mp_obj_new_exception_msg_varg(&mp_type_TypeError,
		MP_ERROR_TEXT("'%s' object isn't callable"), transient);
	assert(read_origin64(roots64[0]).message == MESSAGE_CALL_ARGS64);
	assert(read_origin64(roots64[0]).value == 0);
	memset(transient, 'X', sizeof(transient));
	gc_collect();
	report64(roots64[0], "TypeError: 'transient_type_79271593' 객체는 호출할 수 없습니다\n");
	puts("COMPACT owned: transient source overwritten; original args own complete bytes through GC");
}

static void compact_filename64(size_t leave, bool interned)
{
	char path[64] = "transient_file_79271593.py";

	if (interned)
		(void) qstr_from_str(path);
	else
		assert(qstr_find_strn(path, strlen(path)) == MP_QSTRnull);
	compact_fill64(leave);
	nlr_buf_t nlr;

	if (nlr_push(&nlr) == 0) {
		mp_raise_OSError_with_filename(5, path);
		assert(false);
	} else {
		roots64[0] = MP_OBJ_FROM_PTR(nlr.ret_val);
	}
	memset(path, 'X', sizeof(path));
	gc_collect();
	const mp_obj_exception_t *native = MP_OBJ_TO_PTR(roots64[0]);
	bool rejected = read_origin64(roots64[0]).message == MESSAGE_REJECT64;

	if (mp_obj_get_type(roots64[0]) == &mp_type_OSError) {
		if (rejected) {
			const mp_print_t print = { NULL, sink64 };

			assert(!interned && native->args->len == 0);
			assert(read_origin64(roots64[0]).value == 61);
			assert(!lifetime_print64(&print, roots64[0]));
		} else {
			report64(roots64[0],
				"OSError: 파일 'transient_file_79271593.py'을 열 수 없습니다 [Errno 5]\n");
		}
	} else {
		assert(mp_obj_get_type(roots64[0]) == &mp_type_MemoryError);
		assert(!rejected && read_origin64(roots64[0]).message == MESSAGE_MEMORY64);
	}
	printf("COMPACT_FILENAME64 interned=%u free_blocks=%zu type=%s args=%zu rejected=%u\n",
		interned, leave, mp_obj_get_type_str(roots64[0]), native->args->len, rejected);
}

static bool compact_run64(const char *mode)
{
	if (strcmp(mode, "--compact-partial-fragment") == 0) {
		compact_partial64(5, true);
		return true;
	}
	if (strcmp(mode, "--compact-typed") == 0) {
		compact_typed64();
		return true;
	}
	if (strcmp(mode, "--compact-owned") == 0) {
		compact_owned64();
		return true;
	}
	for (size_t i = 0; i <= 8; i++) {
		char expected[64];

		assert(snprintf(expected, sizeof(expected), "--compact-partial-%zu", i) > 0);
		if (strcmp(mode, expected) == 0) {
			compact_partial64(i, false);
			return true;
		}
		for (unsigned interned = 0; interned <= 1; interned++) {
			assert(snprintf(expected, sizeof(expected), "--compact-filename-%u-%zu",
				interned, i) > 0);
			if (strcmp(mode, expected) == 0) {
				compact_filename64(i, interned != 0);
				return true;
			}
		}
	}
	return false;
}
#endif

void lifetime_run64(const char *mode, void *heap, size_t size)
{
	assert(size == HEAP_SIZE64);
	assert((uintptr_t) heap % MICROPY_BYTES_PER_GC_BLOCK == 0);
	heap_start64 = (uintptr_t) heap;
#if P0_COMPACT64
	assert(mp_origin_init64(&origin_store64, heap_start64, size, MICROPY_BYTES_PER_GC_BLOCK,
		(uintptr_t) &MP_STATE_VM(mp_emergency_exception_obj), origin_ids64, origin_values64,
		SLOT_COUNT64 + 1) == 0);
#endif
	lifetime_enabled64 = true;
#if P0_COMPACT64
	if (compact_run64(mode))
		return;
#endif
#if P0_BOUNDARY64
	if (strcmp(mode, "--boundary-identity") == 0) {
		boundary_identity64();
		return;
	}
	if (strcmp(mode, "--boundary-raises") == 0) {
		boundary_raises64();
		return;
	}
	if (strcmp(mode, "--boundary-users") == 0) {
		boundary_users64();
		return;
	}
	if (strcmp(mode, "--boundary-secondary") == 0) {
		boundary_secondary64();
		return;
	}
#endif
	if (strcmp(mode, "--lifetime-catalog") == 0) {
		for (size_t i = 0; i < sizeof(messages64) / sizeof(messages64[0]); i++)
			printf("FORMAT64 %s\n", messages64[i].format);
		return;
	}
	if (strcmp(mode, "--lifetime-live") == 0) {
		syntax64();
		roots64[0] = mp_obj_new_exception_args(&mp_type_OSError, 1,
			(mp_obj_t[]) { MP_OBJ_NEW_SMALL_INT(5) });
		report64(roots64[0], "OSError: [Errno 5] 입출력 오류입니다\n");
		/* Exercise real VM internal NameError and TypeError constructors. */
		roots64[0] = compile64("missing_identifier\n");
		nlr_buf_t nlr;

		if (nlr_push(&nlr) == 0) {
			mp_call_function_0(roots64[0]);
			assert(false);
		} else {
			roots64[1] = MP_OBJ_FROM_PTR(nlr.ret_val);
			report64(roots64[1], "NameError: 이름 'missing_identifier'이 정의되지 않았습니다\n");
		}
		mp_store_global(qstr_from_str("saved_error"), roots64[1]);
		roots64[0] = compile64("assert str(saved_error) == \"name 'missing_identifier' isn't defined\"\n"
			"assert saved_error.args[0] == str(saved_error)\nraise saved_error\n");
		if (nlr_push(&nlr) == 0) {
			mp_call_function_0(roots64[0]);
			assert(false);
		} else {
			assert(MP_OBJ_FROM_PTR(nlr.ret_val) == roots64[1]);
			report64(roots64[1], "NameError: 이름 'missing_identifier'이 정의되지 않았습니다\n");
		}
		roots64[0] = compile64("1()\n");
		if (nlr_push(&nlr) == 0) {
			mp_call_function_0(roots64[0]);
			assert(false);
		} else {
			report64(MP_OBJ_FROM_PTR(nlr.ret_val), "TypeError: 'int' 객체는 호출할 수 없습니다\n");
		}
		roots64[0] = compile64("raise SyntaxError('invalid syntax')\n");
		if (nlr_push(&nlr) == 0) {
			mp_call_function_0(roots64[0]);
			assert(false);
		} else {
			const mp_print_t print = { NULL, sink64 };
			unsigned missed = missed64;

			assert(!lifetime_print64(&print, MP_OBJ_FROM_PTR(nlr.ret_val)));
			assert(missed64 == missed + 1);
			text_size64 = 0;
			__real_mp_obj_print_exception(&print, MP_OBJ_FROM_PTR(nlr.ret_val));
			assert(strstr(text64, "SyntaxError: invalid syntax\n") != NULL);
		}
		nested_exception64 = roots64[1];
		nested64 = true;
		report64(roots64[1], "NameError: 이름 'missing_identifier'이 정의되지 않았습니다\n");
		const mp_print_t print = { NULL, sink64 };

		assert(!lifetime_print64(&print, roots64[1]));
		__real_mp_obj_print_exception(&print, roots64[1]);
		assert(strstr(text64, "name 'missing_identifier' isn't defined") != NULL);
		puts("LIFETIME live: 256 retained; VM NameError/TypeError; user str/args/raise; reentry/language; allocations=0");
		return;
	}
	if (strcmp(mode, "--lifetime-identity") == 0) {
		mp_make_new_fun_t native = MP_OBJ_TYPE_GET_SLOT(&mp_type_MemoryError, make_new);

		assert(native == __real_mp_obj_exception_make_new);
		assert(native != mp_obj_exception_make_new);
		/* Positive control: an external direct call does enter the wrapper. */
		unsigned before = make_new_calls64;

		roots64[0] = mp_obj_exception_make_new(&mp_type_MemoryError, 0, 0, NULL);
		assert(make_new_calls64 == before + 1);
		assert(mp_obj_is_native_exception_instance(roots64[0]));
		puts("LIFETIME LIMIT identity: native slot != wrapped symbol; inline native check fails before OSError");
		return;
	}
	if (strcmp(mode, "--lifetime-gc") == 0) {
		syntax64();
		gc_info_t before;
		gc_info_t after;

		gc_info(&before);
		memset(roots64, 0, sizeof(roots64));
		/* No register/stack roots in this deliberate isolated collection:
		 * the program has explicitly released every fixture-owned object.
		 */
		gc_collect_start();
		gc_collect_end();
		gc_info(&after);
		assert(before.used >= after.used + 256 * sizeof(mp_obj_exception_t));
		lifetime_sweep64();
		assert(count64() == 0);
		syntax64();
		puts("LIFETIME GC: all 256 released/swept; no registry roots; 256 reused successfully");
		return;
	}
	if (strcmp(mode, "--lifetime-reuse") == 0) {
		roots64[0] = mp_obj_new_exception_msg(&mp_type_SyntaxError,
			MP_ERROR_TEXT("invalid syntax"));
		uintptr_t old = (uintptr_t) MP_OBJ_TO_PTR(roots64[0]);

		gc_free(MP_OBJ_TO_PTR(roots64[0]));
		roots64[0] = MP_OBJ_NULL;
		void *reused = gc_alloc(sizeof(mp_obj_exception_t), 0);

		assert((uintptr_t) reused == old);
		assert(read_origin64(MP_OBJ_FROM_PTR(reused)).message == MESSAGE_NONE64);
		puts("LIFETIME reuse: successful real gc_alloc invalidates stale address before publication");
		return;
	}
	bool locked = strcmp(mode, "--lifetime-lock") == 0;

	assert(locked || strcmp(mode, "--lifetime-oom") == 0);
	if (locked) {
		gc_lock();
	} else {
		for (;;) {
			void *block = gc_alloc(1, 0);

			if (block == NULL)
				break;
			assert(fill_count64 < SLOT_COUNT64);
			fill64[fill_count64++] = block;
		}
		gc_info_t info;

		gc_info(&info);
		assert(info.free == 0);
	}
	nlr_buf_t nlr;

	if (nlr_push(&nlr) == 0) {
		(void) m_malloc(1);
		assert(false);
	} else {
		roots64[0] = MP_OBJ_FROM_PTR(nlr.ret_val);
		assert(roots64[0] == MP_OBJ_FROM_PTR(&MP_STATE_VM(mp_emergency_exception_obj)));
		report64(roots64[0], locked ?
			"MemoryError: 힙이 잠겨 메모리를 할당할 수 없습니다\n" :
			"MemoryError: 1 바이트 메모리를 할당할 수 없습니다\n");
	}
	/* Python invokes this exact production type slot. Unlike a direct call
	 * from this fixture, its relocation inside objexcept.o bypasses --wrap.
	 * Same type and empty args deliberately defeat identity/type/args guards.
	 */
	unsigned calls = make_new_calls64;
	mp_obj_t user = MP_OBJ_TYPE_GET_SLOT(&mp_type_MemoryError, make_new)(
		&mp_type_MemoryError, 0, 0, NULL);

	assert(make_new_calls64 == calls + (P0_BOUNDARY64 ? 1U : 0U));
	assert(user == roots64[0]);
#if !P0_BOUNDARY64
	assert(read_origin64(user).args == ((mp_obj_exception_t *) MP_OBJ_TO_PTR(user))->args);
	assert(read_origin64(user).message == (locked ? MESSAGE_LOCK64 : MESSAGE_MEMORY64));
#else
	assert(read_origin64(user).message == MESSAGE_NONE64);
#endif
	const mp_print_t print = { NULL, sink64 };

	text_size64 = 0;
	__real_mp_obj_print_exception(&print, user);
	assert(strcmp(text64, "MemoryError: \n") == 0);
#if P0_BOUNDARY64
	assert(!lifetime_print64(&print, user));
	for (size_t i = 0; i < 128; i++) {
		roots64[0] = mp_obj_new_exception_msg(&mp_type_SyntaxError,
			MP_ERROR_TEXT("invalid syntax"));
		report64(roots64[0], "SyntaxError: 잘못된 문법입니다\n");
		user = MP_OBJ_TYPE_GET_SLOT(&mp_type_SyntaxError, make_new)(
			&mp_type_SyntaxError, 0, 0, NULL);
		assert(user == roots64[0] && read_origin64(user).message == MESSAGE_NONE64);
		assert(!lifetime_print64(&print, user));
	}
	puts("BOUNDARY emergency: 128 builtin/user same-address/type/args generations; stale origins cleared");
#else
	/* Actually emit the incorrect translated report, then require its exact
	 * mismatch against the user's empty original. This is a failing design
	 * reproduced successfully, not an accepted user-visible behavior.
	 */
	report64(user, locked ? "MemoryError: 힙이 잠겨 메모리를 할당할 수 없습니다\n" :
		"MemoryError: 1 바이트 메모리를 할당할 수 없습니다\n");
	puts("LIFETIME LIMIT: user slot bypass; same emergency/type/args; stale Korean translation reproduced");
#endif
	if (locked)
		gc_unlock();
	printf("LIFETIME %s: real m_malloc failure; Korean report allocates 0; static table %zu bytes\n",
		locked ? "lock" : "free=0", storage_bytes64());
}
