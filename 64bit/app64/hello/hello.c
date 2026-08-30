/*
 * hello.c -- 가장 작은 앱. 앱 실행 경로가 살아 있는지 보는 용도다.
 */
#include <mowos.h>

int main(int argc, char **argv)
{
	(void) argc;
	(void) argv;
	puts("hello from app64");
	return 0;
}
