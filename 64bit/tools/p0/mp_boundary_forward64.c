/* Cross-architecture symbol fixture only: every operation runs its original.
 * The runtime provenance experiment uses the instrumented host wrappers.
 */
#include <stdarg.h>
#include "py/runtime.h"

mp_obj_t __real_mp_obj_exception_make_new(const mp_obj_type_t *, size_t, size_t,
	const mp_obj_t *);
mp_obj_t __real_mp_obj_new_exception(const mp_obj_type_t *);
mp_obj_t __real_mp_obj_new_exception_args(const mp_obj_type_t *, size_t, const mp_obj_t *);
mp_obj_t __real_mp_obj_new_exception_msg(const mp_obj_type_t *, mp_rom_error_text_t);
mp_obj_t __real_mp_obj_new_exception_msg_vlist(const mp_obj_type_t *,
	mp_rom_error_text_t, va_list);
MP_NORETURN void __real_mp_raise_type_arg(const mp_obj_type_t *, mp_obj_t);
MP_NORETURN void __real_mp_raise_OSError_with_filename(int, const char *);

mp_obj_t mp_obj_exception_make_new(const mp_obj_type_t *type, size_t count,
	size_t keywords, const mp_obj_t *args)
{
	return __real_mp_obj_exception_make_new(type, count, keywords, args);
}

mp_obj_t mp_obj_new_exception(const mp_obj_type_t *type)
{
	return __real_mp_obj_new_exception(type);
}

mp_obj_t mp_obj_new_exception_args(const mp_obj_type_t *type, size_t count,
	const mp_obj_t *args)
{
	return __real_mp_obj_new_exception_args(type, count, args);
}

mp_obj_t mp_obj_new_exception_msg(const mp_obj_type_t *type, mp_rom_error_text_t msg)
{
	return __real_mp_obj_new_exception_msg(type, msg);
}

mp_obj_t mp_obj_new_exception_msg_vlist(const mp_obj_type_t *type,
	mp_rom_error_text_t fmt, va_list args)
{
	return __real_mp_obj_new_exception_msg_vlist(type, fmt, args);
}

mp_obj_t mp_obj_new_exception_msg_varg(const mp_obj_type_t *type, mp_rom_error_text_t fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	mp_obj_t exception = __real_mp_obj_new_exception_msg_vlist(type, fmt, args);

	va_end(args);
	return exception;
}

MP_NORETURN void mp_raise_type_arg(const mp_obj_type_t *type, mp_obj_t arg)
{
	__real_mp_raise_type_arg(type, arg);
}

MP_NORETURN void mp_raise_OSError_with_filename(int error, const char *filename)
{
	__real_mp_raise_OSError_with_filename(error, filename);
}
