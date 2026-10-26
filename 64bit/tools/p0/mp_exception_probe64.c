/* P0 experiment: real MicroPython objects and REPL, host I/O only.
 * Not linked into the kernel. GNU ld --wrap preserves vendor sources.
 * The bounded registry is explicitly rooted and rejects overflow; it is not
 * the lifetime/allocation policy for the eventual production translator.
 */
#include <assert.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "py/compile.h"
#include "py/builtin.h"
#include "py/cstack.h"
#include "py/gc.h"
#include "py/mperrno.h"
#include "py/mphal.h"
#include "py/objexcept.h"
#include "py/objmodule.h"
#include "py/objstr.h"
#include "py/runtime.h"
#include "shared/runtime/gchelper.h"
#include "shared/runtime/pyexec.h"

mp_obj_t __real_mp_obj_new_exception_msg(const mp_obj_type_t *, mp_rom_error_text_t);
mp_obj_t __real_mp_obj_new_exception_msg_vlist(const mp_obj_type_t *,
	mp_rom_error_text_t, va_list);
mp_obj_t __real_mp_obj_new_exception_args(const mp_obj_type_t *, size_t, const mp_obj_t *);
void __real_mp_obj_print_exception(const mp_print_t *, mp_obj_t);
mp_obj_t __wrap_mp_obj_new_exception_msg(const mp_obj_type_t *, mp_rom_error_text_t);
mp_obj_t __wrap_mp_obj_new_exception_msg_varg(const mp_obj_type_t *, mp_rom_error_text_t, ...);
mp_obj_t __wrap_mp_obj_new_exception_msg_vlist(const mp_obj_type_t *,
	mp_rom_error_text_t, va_list);
mp_obj_t __wrap_mp_obj_new_exception_args(const mp_obj_type_t *, size_t, const mp_obj_t *);
MP_NORETURN void __real_mp_raise_OSError(int);
MP_NORETURN void __wrap_mp_raise_OSError(int);
void __wrap_mp_obj_print_exception(const mp_print_t *, mp_obj_t);
int signbit(double value);
void nlr_jump_fail(void *value);

struct ORIGIN64 {
	mp_obj_t exception;
	mp_obj_tuple_t *args;
	mp_obj_t translated;
};

static struct ORIGIN64 origins64[64];
static size_t origin_count64;
static unsigned char heap64[2 * 1024 * 1024];
static char output64[65536];
static size_t output_size64;
static const char *input64;
static int korean64;
static unsigned reports64;
static unsigned translated64;

static const mp_rom_map_elem_t empty_module_table64[] = {
	{ MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_mowio) },
};
static MP_DEFINE_CONST_DICT(empty_module_dict64, empty_module_table64);
/* No mowio calls in this experiment. An empty module must not claim success
 * for any unsupported OS operation: normal attribute lookup raises an error.
 */
const mp_obj_module_t mp_module_mowio = {
	.base = { &mp_type_module },
	.globals = (mp_obj_dict_t *) &empty_module_dict64,
};

static const char *catalog64(const char *format)
{
	static const char *const messages[][2] = {
		{ "name '%q' isn't defined", "이름 '%q'이 정의되지 않았습니다" },
		{ "'%s' object isn't callable", "'%s' 객체는 호출할 수 없습니다" },
		{ "invalid syntax", "잘못된 문법입니다" },
		{ "memory allocation failed, heap is locked", "힙이 잠겨 메모리를 할당할 수 없습니다" },
		{ "memory allocation failed, allocating " SIZE_FMT " bytes",
			SIZE_FMT " 바이트 메모리를 할당할 수 없습니다" },
	};

	for (size_t i = 0; i < sizeof(messages) / sizeof(messages[0]); i++) {
		if (strcmp(format, messages[i][0]) == 0)
			return messages[i][1];
	}
	return NULL;
}

static void remember64(mp_obj_t exception, mp_obj_t translated)
{
	mp_obj_exception_t *native = MP_OBJ_TO_PTR(exception);

	/* Overflow is a failed experiment, never an unnoticed English fallback. */
	assert(origin_count64 < sizeof(origins64) / sizeof(origins64[0]));
	origins64[origin_count64++] = (struct ORIGIN64) {
		.exception = exception,
		.args = native->args,
		.translated = translated,
	};
}

mp_obj_t __wrap_mp_obj_new_exception_msg(const mp_obj_type_t *type, mp_rom_error_text_t msg)
{
	mp_obj_t exception = __real_mp_obj_new_exception_msg(type, msg);
	const char *translation = msg == NULL ? NULL : catalog64((const char *) msg);

	if (translation != NULL)
		remember64(exception, __real_mp_obj_new_exception_msg(type,
			(mp_rom_error_text_t) translation));
	return exception;
}

mp_obj_t __wrap_mp_obj_new_exception_msg_vlist(const mp_obj_type_t *type,
	mp_rom_error_text_t format, va_list args)
{
	va_list copy;
	const char *translation = catalog64((const char *) format);
	mp_obj_t exception;

	va_copy(copy, args);
	exception = __real_mp_obj_new_exception_msg_vlist(type, format, args);
	if (translation != NULL)
		remember64(exception, __real_mp_obj_new_exception_msg_vlist(type,
			(mp_rom_error_text_t) translation, copy));
	va_end(copy);
	return exception;
}

mp_obj_t __wrap_mp_obj_new_exception_msg_varg(const mp_obj_type_t *type,
	mp_rom_error_text_t format, ...)
{
	va_list args;
	mp_obj_t exception;

	va_start(args, format);
	exception = __wrap_mp_obj_new_exception_msg_vlist(type, format, args);
	va_end(args);
	return exception;
}

mp_obj_t __wrap_mp_obj_new_exception_args(const mp_obj_type_t *type, size_t count,
	const mp_obj_t *args)
{
	mp_obj_t exception = __real_mp_obj_new_exception_args(type, count, args);

	/* Internal errno constructor, distinct from Python OSError.make_new.
 * Only EIO is in the P0 catalog. Other numbers are not claimed as covered.
 */
	if (type == &mp_type_OSError && count == 1 && args[0] == MP_OBJ_NEW_SMALL_INT(MP_EIO))
		remember64(exception, __real_mp_obj_new_exception_msg(type,
			(mp_rom_error_text_t) "[Errno 5] 입출력 오류입니다"));
	return exception;
}

MP_NORETURN void __wrap_mp_raise_OSError(int error)
{
	nlr_buf_t nlr;

	if (nlr_push(&nlr) == 0) {
		__real_mp_raise_OSError(error);
	} else {
		mp_obj_t exception = MP_OBJ_FROM_PTR(nlr.ret_val);

		if (error == MP_EIO)
			remember64(exception, __real_mp_obj_new_exception_msg(&mp_type_OSError,
				(mp_rom_error_text_t) "[Errno 5] 입출력 오류입니다"));
		nlr_raise(exception);
	}
}

void __wrap_mp_obj_print_exception(const mp_print_t *print, mp_obj_t exception)
{
	/* Capture once; filenames, identifiers, and traceback values are emitted
 * from structured fields. The user's stdout never traverses this wrapper.
 */
	const int korean = korean64;
	mp_obj_t translated = MP_OBJ_NULL;

	reports64++;
	if (!korean || !mp_obj_is_native_exception_instance(exception)) {
		__real_mp_obj_print_exception(print, exception);
		return;
	}
	const mp_obj_exception_t *native = MP_OBJ_TO_PTR(exception);
	for (size_t i = 0; i < origin_count64; i++) {
		if (origins64[i].exception == exception && origins64[i].args == native->args) {
			translated = origins64[i].translated;
			break;
		}
	}
	size_t count;
	size_t *trace;

	mp_obj_exception_get_traceback(exception, &count, &trace);
	if (count != 0) {
		mp_print_str(print, "호출 추적 (최근 호출이 마지막):\n");
		for (size_t i = count; i != 0; i -= 3) {
			mp_printf(print, "  파일 \"%q\", %d행", trace[i - 3], (int) trace[i - 2]);
			if (trace[i - 1] != MP_QSTRnull)
				mp_printf(print, ", %q에서", trace[i - 1]);
			mp_print_str(print, "\n");
		}
	}
	if (translated != MP_OBJ_NULL)
		translated64++;
	mp_obj_print_helper(print, translated == MP_OBJ_NULL ? exception : translated, PRINT_EXC);
	mp_print_str(print, "\n");
}

mp_uint_t mp_hal_stdout_tx_strn(const char *str, size_t len)
{
	assert(len < sizeof(output64) - output_size64);
	memcpy(output64 + output_size64, str, len);
	output_size64 += len;
	output64[output_size64] = '\0';
	return len;
}

void mp_hal_stdout_tx_str(const char *str)
{
	(void) mp_hal_stdout_tx_strn(str, strlen(str));
}

void mp_hal_stdout_tx_strn_cooked(const char *str, size_t len)
{
	(void) mp_hal_stdout_tx_strn(str, len);
}

int mp_hal_stdin_rx_chr(void)
{
	assert(input64 != NULL && *input64 != '\0');
	return (unsigned char) *input64++;
}

void mp_hal_move_cursor_back(unsigned int count)
{
	while (count-- != 0)
		mp_hal_stdout_tx_str("\b");
}

void mp_hal_erase_line_from_cursor(unsigned int count)
{
	while (count-- != 0)
		mp_hal_stdout_tx_str(" \b");
}

void gc_collect(void)
{
	gc_collect_start();
	gc_collect_root((void **) origins64, sizeof(origins64) / (sizeof(void *)));
	gc_helper_collect_regs_and_stack();
	gc_collect_end();
}

int signbit(double value)
{
	return __builtin_signbit(value);
}

void nlr_jump_fail(void *value)
{
	(void) value;
	abort();
}

mp_import_stat_t mp_import_stat(const char *path)
{
	struct stat info;

	if (stat(path, &info) != 0)
		return MP_IMPORT_STAT_NO_EXIST;
	return S_ISDIR(info.st_mode) ? MP_IMPORT_STAT_DIR : MP_IMPORT_STAT_FILE;
}

mp_lexer_t *mp_lexer_new_from_file(qstr name)
{
	FILE *file = fopen(qstr_str(name), "rb");
	char source[8192];
	size_t len;

	if (file == NULL)
		mp_raise_OSError(MP_EIO);
	len = fread(source, 1, sizeof(source), file);
	assert(!ferror(file) && feof(file));
	assert(fclose(file) == 0);
	/* The returned lexer must outlive this host loader's stack buffer. */
	char *owned = m_new(char, len + 1);

	memcpy(owned, source, len);
	owned[len] = '\0';
	return mp_lexer_new_from_str_len(name, owned, len, len + 1);
}

static void execute64(const char *source)
{
	nlr_buf_t nlr;

	if (nlr_push(&nlr) == 0) {
		mp_lexer_t *lexer = mp_lexer_new_from_str_len(qstr_from_str("사용자.py"),
			source, strlen(source), 0);
		mp_parse_tree_t tree = mp_parse(lexer, MP_PARSE_FILE_INPUT);
		mp_obj_t fun = mp_compile(&tree, qstr_from_str("사용자.py"), false);

		mp_call_function_0(fun);
		nlr_pop();
	} else {
		__wrap_mp_obj_print_exception(&mp_plat_print, MP_OBJ_FROM_PTR(nlr.ret_val));
	}
}

static void check64(const char *source, const char *expected, int should_translate)
{
	unsigned before = translated64;

	output_size64 = 0;
	output64[0] = '\0';
	execute64(source);
	if (strstr(output64, expected) == NULL) {
		fprintf(stderr, "expected: %s\nactual: %s\n", expected, output64);
		abort();
	}
	assert(translated64 == before + (unsigned) should_translate);
}

int main(int argc, char **argv)
{
	volatile uintptr_t anchor;
	nlr_buf_t session_nlr;
	nlr_buf_t nlr;

	mp_cstack_init_with_top((void *) (&anchor + 1), 512 * 1024);
	gc_init(heap64, heap64 + sizeof(heap64));
	if (nlr_push(&session_nlr) != 0)
		abort();
	mp_init();
	assert(argc == 2);
	korean64 = 1;
	check64("missing_identifier\n", "이름 'missing_identifier'이 정의되지 않았습니다", 1);
	check64("1()\n", "'int' 객체는 호출할 수 없습니다", 1);
	check64("if :\n", "잘못된 문법입니다", 1);
	check64("import missing_file\n", "no module named 'missing_file'", 0);
	check64("raise ValueError('사용자 문자열')\n", "ValueError: 사용자 문자열", 0);
	check64("raise SyntaxError('invalid syntax')\n", "SyntaxError: invalid syntax", 0);
	check64("raise NameError(\"name 'x' isn't defined\")\n", "name 'x' isn't defined", 0);
	check64("raise TypeError(\"'int' object isn't callable\")\n", "'int' object isn't callable", 0);
	check64("raise OSError(5)\n", "OSError: 5", 0);
	check64("print('invalid syntax')\n", "invalid syntax\n", 0);
	check64("try:\n missing_again\nexcept NameError as e:\n"
		" print(str(e))\n print(e.args[0])\n print(type(e) == NameError)\n",
		"name 'missing_again' isn't defined\nname 'missing_again' isn't defined\nTrue\n", 0);
	check64("try:\n retained_missing\nexcept NameError as e:\n"
		" saved = e\n saved_text = str(e)\n saved_args = e.args\n raise e\n",
		"이름 'retained_missing'이 정의되지 않았습니다", 1);
	check64("print(str(saved) == saved_text, saved.args is saved_args, type(saved) == NameError)\n",
		"True True True\n", 0);
	/* Internal OS errno and allocation-failure constructors, not user raises. */
	output_size64 = 0;
	if (nlr_push(&nlr) == 0) {
		mp_raise_OSError(MP_EIO);
	} else {
		__wrap_mp_obj_print_exception(&mp_plat_print, MP_OBJ_FROM_PTR(nlr.ret_val));
	}
	assert(strstr(output64, "OSError: [Errno 5] 입출력 오류입니다") != NULL);
	output_size64 = 0;
	if (nlr_push(&nlr) == 0) {
		/* Oversized allocation failure, with memory left for the P0 renderer.
		 * Exhausted-heap/emergency-buffer rendering remains a production gate.
		 */
		(void) m_malloc(3 * 1024 * 1024);
		abort();
	} else {
		__wrap_mp_obj_print_exception(&mp_plat_print, MP_OBJ_FROM_PTR(nlr.ret_val));
	}
	assert(strstr(output64, "3145728 바이트 메모리를 할당할 수 없습니다") != NULL);
	korean64 = 0;
	check64("1()\n", "TypeError: 'int' object isn't callable", 0);
	korean64 = 1;
	output_size64 = 0;
	unsigned before = reports64;
	(void) pyexec_file(argv[1]);
	assert(reports64 == before + 1);
	assert(strstr(output64, "이름 'file_missing_identifier'이 정의되지 않았습니다") != NULL);
	assert(strstr(output64, argv[1]) != NULL);
	input64 = "friendly_missing\r\x04";
	output_size64 = 0;
	before = reports64;
	(void) pyexec_friendly_repl();
	assert(reports64 == before + 1);
	assert(strstr(output64, "이름 'friendly_missing'이 정의되지 않았습니다") != NULL);
	input64 = "raw_missing\x04\x02";
	output_size64 = 0;
	before = reports64;
	(void) pyexec_raw_repl();
	assert(reports64 == before + 1);
	assert(strstr(output64, "이름 'raw_missing'이 정의되지 않았습니다") != NULL);
	/* Retained provenance must survive collection and re-reporting. */
	gc_collect();
	__wrap_mp_obj_print_exception(&mp_plat_print, origins64[0].exception);
	mp_deinit();
	nlr_pop();
	puts("P0 MicroPython: PASS (real core, file/friendly/raw, provenance, user data)");
	return 0;
}
