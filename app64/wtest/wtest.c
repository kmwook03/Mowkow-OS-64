#include <mowos.h>

/* FAT12 write self-check: create, write, reopen, verify.
   Run it once ("run WTEST"), reboot, then "run CAT TEST.TXT" to confirm the
   contents reached the medium and not just the RAM image. */

#define BIG_SIZE 1500

static const char text[] = "phase0 fat12 write ok\n";
static char big[BIG_SIZE];

int main(int argc, char **argv)
{
	const char *path;
	char buf[64];
	int fd;
	long n;
	size_t len;
	size_t got;
	size_t i;

	path = argc >= 2 ? argv[1] : "TEST.TXT";
	len = strlen(text);

	fd = open(path, O_CREAT | O_TRUNC);
	if (fd < 0) {
		puts("wtest: FAIL create");
		return 1;
	}
	n = write(fd, text, len);
	close(fd);
	if (n != (long) len) {
		puts("wtest: FAIL write");
		return 1;
	}

	fd = open(path, 0);
	if (fd < 0) {
		puts("wtest: FAIL reopen");
		return 1;
	}
	n = read(fd, buf, sizeof(buf));
	close(fd);
	if (n != (long) len || memcmp(buf, text, len) != 0) {
		puts("wtest: FAIL readback");
		return 1;
	}

	/* multi-cluster: 1500 bytes spans three 512-byte clusters, so this
	   covers free-cluster allocation and chain extension */
	fd = open("BIG.TXT", O_CREAT | O_TRUNC);
	if (fd < 0) {
		puts("wtest: FAIL big create");
		return 1;
	}
	for (i = 0; i < BIG_SIZE; i++) {
		big[i] = (char) ('a' + i % 26);
	}
	n = write(fd, big, BIG_SIZE);
	close(fd);
	if (n != BIG_SIZE) {
		puts("wtest: FAIL big write");
		return 1;
	}
	memset(big, 0, BIG_SIZE);
	fd = open("BIG.TXT", 0);
	if (fd < 0) {
		puts("wtest: FAIL big reopen");
		return 1;
	}
	for (got = 0; got < BIG_SIZE; got += (size_t) n) {
		n = read(fd, big + got, BIG_SIZE - got);
		if (n <= 0) {
			break;
		}
	}
	close(fd);
	if (got != BIG_SIZE) {
		puts("wtest: FAIL big readback size");
		return 1;
	}
	for (i = 0; i < BIG_SIZE; i++) {
		if (big[i] != (char) ('a' + i % 26)) {
			puts("wtest: FAIL big readback data");
			return 1;
		}
	}

	puts("wtest: ok");
	return 0;
}
