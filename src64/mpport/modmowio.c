/*
 * modmowio.c -- 커널 콘솔과 파일 시스템으로 이어 주는 mowio 내장 모듈
 *
 * 머꼬(mowkow_porting.md)가 CPython 표준 라이브러리 대신 쓰는 창구다.
 * readline은 input()을, readfile은 io.open()을, argv는 sys.argv를 대신한다.
 */
#include "py/mperrno.h"
#include "py/runtime.h"

#include <console64.h>
#include <mpport64.h>
#include <timer64.h>
#include <string.h>

/*
 * mowio.readline(prompt="") -> str
 *
 * MicroPython의 input()을 대신한다. input()은 repl 큐에서 바이트를 받아
 * 스스로 에코하는데, 그 길에는 한글 오토마타가 없고 백스페이스도 바이트
 * 단위라 3바이트 음절을 깨뜨린다. 대신 커널 콘솔의 줄 편집기를 그대로
 * 부른다 - 조합 중인 음절이 화면에서 자라는 것도 거기서 이미 한다
 * (mowkow_porting.md 결정 5).
 *
 * Ctrl-C는 KeyboardInterrupt, Ctrl-D는 EOFError로 올라간다(결정 8).
 */
static mp_obj_t mowio_readline(size_t n_args, const mp_obj_t *args)
{
	char buf[CONSOLE64_LINE_MAX];
	int64_t n;

	if (n_args > 0) {
		console64_puts(mp_obj_str_get_str(args[0]));
	}
	n = console64_read_line(buf, sizeof(buf));
	if (n == -1) {
		mp_raise_type(&mp_type_KeyboardInterrupt);
	}
	if (n == -2) {
		mp_raise_type(&mp_type_EOFError);
	}
	return mp_obj_new_str(buf, (size_t) n);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(mowio_readline_obj, 0, 1, mowio_readline);

/*
 * mowio.readfile(path) -> str
 *
 * io.open() 대신이다(MICROPY_PY_IO는 꺼져 있다). 머꼬의 slurp가 소스와
 * library_kor.scm을 이걸로 읽는다.
 *
 * UTF-8 검사는 mp_obj_new_str이 이미 한다(MICROPY_PY_BUILTINS_STR_UNICODE가
 * 켜져 있으면 STR_UNICODE_CHECK도 따라 켜진다, mpconfig.h:1361). 잘린 파일은
 * UnicodeError -- ValueError의 하위 클래스다 -- 로 걸리지, 인덱싱이 어긋난
 * str이 되어 조용히 잘못 파싱되지 않는다.
 */
static mp_obj_t mowio_readfile(mp_obj_t path_in)
{
	const char *path = mp_obj_str_get_str(path_in);
	uint8_t *buf;
	size_t size;

	buf = mpport_load_file(path, &size);
	if (buf == NULL) {
		mp_raise_OSError(MP_ENOENT);
	}
	return mp_obj_new_str((const char *) buf, size);
}
static MP_DEFINE_CONST_FUN_OBJ_1(mowio_readfile_obj, mowio_readfile);

/*
 * mowio.argv() -> str | None
 *
 * 콘솔이 `머꼬 add.mk`로 넘긴 파일 이름. 인자가 없으면(그냥 `머꼬`) None이고,
 * MOWKOW.PY는 그걸 보고 파일 모드와 REPL을 가른다(결정 7).
 */
static mp_obj_t mowio_argv(void)
{
	const char *arg = mpport_argv();

	if (arg == NULL) {
		return mp_const_none;
	}
	return mp_obj_new_str(arg, strlen(arg));
}
static MP_DEFINE_CONST_FUN_OBJ_0(mowio_argv_obj, mowio_argv);

/*
 * mowio.ticks() -> int
 *
 * PIT 틱(100Hz, 한 틱 10ms). 머꼬 시작 시간을 재는 데 쓴다
 * (mowkow_porting.md 결정 11). time 모듈은 켜지 않았다 - 셀 것이 이것뿐이다.
 */
static mp_obj_t mowio_ticks(void)
{
	return mp_obj_new_int((mp_int_t) timerctl64.count);
}
static MP_DEFINE_CONST_FUN_OBJ_0(mowio_ticks_obj, mowio_ticks);

static const mp_rom_map_elem_t mowio_module_globals_table[] = {
	{ MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_mowio) },
	{ MP_ROM_QSTR(MP_QSTR_readline), MP_ROM_PTR(&mowio_readline_obj) },
	{ MP_ROM_QSTR(MP_QSTR_readfile), MP_ROM_PTR(&mowio_readfile_obj) },
	{ MP_ROM_QSTR(MP_QSTR_argv), MP_ROM_PTR(&mowio_argv_obj) },
	{ MP_ROM_QSTR(MP_QSTR_ticks), MP_ROM_PTR(&mowio_ticks_obj) },
};
static MP_DEFINE_CONST_DICT(mowio_module_globals, mowio_module_globals_table);

const mp_obj_module_t mp_module_mowio = {
	.base = { &mp_type_module },
	.globals = (mp_obj_dict_t *) &mowio_module_globals,
};

MP_REGISTER_MODULE(MP_QSTR_mowio, mp_module_mowio);
