#ifndef MOWKOW64_MPPORT64_H
#define MOWKOW64_MPPORT64_H

#include <stddef.h>
#include <stdint.h>

void mpport_repl(void);
void mpport_run_file(const char *path);

/* `머꼬` 명령. arg가 NULL이면 REPL, 아니면 그 파일을 돌린다. */
void mpport_run_mowkow(const char *arg);

/* 파일 하나를 GC 힙으로. 없으면 NULL. NLR 문맥 안에서만 부른다.
   (modmowio.c의 readfile과 mp_lexer_new_from_file이 함께 쓴다) */
uint8_t *mpport_load_file(const char *path, size_t *out_size);

/* 콘솔 명령이 넘긴 인자를 넣고 뺀다. mowio.argv()가 이것을 읽는다. 
   NULL이면 인자가 없다는 뜻이다. */
void mpport_set_argv(const char *arg);
const char *mpport_argv(void);

#endif
