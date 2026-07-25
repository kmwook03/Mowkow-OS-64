#include <mowos.h>

int main(int argc, char **argv)
{
	char buf[128];
	int fd;
	long n;

	if (argc < 2) {
		puts("usage: CAT FILE");
		return 1;
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
