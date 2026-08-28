/*
 * cat.c -- 파일 내용을 그대로 콘솔에 쏟는다
 *
 * 쓰임새: cat README.TXT
 */
#include <mowos.h>

int main(int argc, char **argv)
{
	char buf[128];
	int fd;
	long n;

	if (argc < 2) {
		/* 인자가 없으면 표준 입력을 되울린다. "." 한 줄이면 끝. */
		for (;;) {
			n = read(0, buf, sizeof(buf));
			if (n <= 0 || (n == 2 && buf[0] == '.')) {
				break;
			}
			write(1, buf, (size_t) n);
		}
		return 0;
	}
	fd = open(argv[1], 0);
	if (fd < 0) {
		puts("open failed");
		return 1;
	}
	for (;;) {
		n = read(fd, buf, sizeof(buf));
		if (n <= 0) {
			break;
		}
		write(1, buf, (size_t) n);
	}
	close(fd);
	return 0;
}
