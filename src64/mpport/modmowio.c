/*
 * modmowio.c -- 커널 콘솔과 파일 시스템으로 이어 주는 mowio 내장 모듈
 *
 * 머꼬(mowkow_porting.md)가 CPython 표준 라이브러리 대신 쓰는 창구다. 지금은
 * readline 하나뿐이고, readfile/argv는 계획 5단계에서 붙인다.
 */
#include "py/runtime.h"

#include <console64.h>

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

static const mp_rom_map_elem_t mowio_module_globals_table[] = {
	{ MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_mowio) },
	{ MP_ROM_QSTR(MP_QSTR_readline), MP_ROM_PTR(&mowio_readline_obj) },
};
static MP_DEFINE_CONST_DICT(mowio_module_globals, mowio_module_globals_table);

const mp_obj_module_t mp_module_mowio = {
	.base = { &mp_type_module },
	.globals = (mp_obj_dict_t *) &mowio_module_globals,
};

MP_REGISTER_MODULE(MP_QSTR_mowio, mp_module_mowio);
