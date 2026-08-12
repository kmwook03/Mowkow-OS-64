/*
 * app64/crt/malloc.c 자유 리스트 확인.
 *
 * 예전 범프 할당기라면 churn 시험에서 반드시 실패한다 - 힙의 여러 배를
 * 할당했다 해제하므로, 재사용이 안 되면 중간에 0이 나온다.
 */
#include <mowos.h>

static int failures;

static void put_dec(unsigned long v)
{
	char buf[20];
	int i;

	if (v == 0) {
		write(1, "0", 1);
		return;
	}
	i = 0;
	while (v != 0 && i < 20) {
		buf[i++] = (char) ('0' + v % 10);
		v /= 10;
	}
	while (i > 0) {
		write(1, &buf[--i], 1);
	}
}

static void check(const char *name, int ok)
{
	write(1, ok ? "ok   " : "FAIL ", 5);
	puts(name);
	if (ok == 0) {
		failures++;
	}
}

/* 블록을 값으로 채우고 나중에 그대로인지 본다. 이웃 할당이 남의 영역을
   덮어쓰면 여기서 걸린다. */
static void fill(unsigned char *p, size_t n, unsigned char v)
{
	size_t i;

	for (i = 0; i < n; i++) {
		p[i] = v;
	}
}

static int verify(const unsigned char *p, size_t n, unsigned char v)
{
	size_t i;

	for (i = 0; i < n; i++) {
		if (p[i] != v) {
			return 0;
		}
	}
	return 1;
}

static void test_basic(void)
{
	unsigned char *a;

	a = malloc(100);
	if (a == 0) {
		check("basic alloc", 0);
		return;
	}
	fill(a, 100, 0xa5);
	check("basic alloc", verify(a, 100, 0xa5));
	free(a);
	free(0);                /* NULL 해제는 무해해야 한다 */
	check("free(NULL)", 1);
}

/* 힙보다 훨씬 많이 할당/해제한다. 재사용이 되지 않으면 실패한다.
   힙 크기는 process64.c의 USER_HEAP_SIZE라 여기에 적지 않는다. */
static void test_churn(void)
{
	unsigned char *p;
	unsigned long i;
	unsigned long total;

	total = 0;
	for (i = 0; i < 4000; i++) {
		p = malloc(1024);
		if (p == 0) {
			write(1, "     churn stopped at ", 22);
			put_dec(i);
			puts("");
			check("churn 4 MiB through the process heap", 0);
			return;
		}
		fill(p, 1024, (unsigned char) i);
		if (verify(p, 1024, (unsigned char) i) == 0) {
			check("churn 4 MiB through the process heap", 0);
			return;
		}
		free(p);
		total += 1024;
	}
	write(1, "     reused ", 12);
	put_dec(total / 1024);
	puts(" KiB");
	check("churn 4 MiB through the process heap", 1);
}

/* 붙어 있는 블록 셋을 해제한 뒤, 어느 하나보다 큰 블록을 요구한다.
   합치기가 되지 않으면 새 메모리를 얻어와야 하고, 힙이 거의 다 찼다면
   실패한다. 여기서는 합쳐진 자리를 그대로 재사용하는지를 본다. */
static void test_coalesce(void)
{
	unsigned char *a;
	unsigned char *b;
	unsigned char *c;
	unsigned char *big;

	a = malloc(2048);
	b = malloc(2048);
	c = malloc(2048);
	if (a == 0 || b == 0 || c == 0) {
		check("coalesce setup", 0);
		return;
	}
	free(a);
	free(b);
	free(c);
	big = malloc(6000);
	if (big == 0) {
		check("coalesce 3 blocks into one", 0);
		return;
	}
	/* 합쳐졌다면 세 블록 중 가장 낮은 주소를 그대로 쓴다. */
	check("coalesce 3 blocks into one", big == a);
	fill(big, 6000, 0x5a);
	check("coalesced block is writable", verify(big, 6000, 0x5a));
	free(big);
}

/* 큰 블록을 해제한 자리에서 작은 것 여럿을 떼어 쓸 수 있어야 한다. */
static void test_split(void)
{
	unsigned char *big;
	unsigned char *x;
	unsigned char *y;

	big = malloc(4096);
	if (big == 0) {
		check("split setup", 0);
		return;
	}
	free(big);
	x = malloc(64);
	y = malloc(64);
	if (x == 0 || y == 0) {
		check("split a freed block", 0);
		return;
	}
	fill(x, 64, 1);
	fill(y, 64, 2);
	check("split a freed block", x == big && y > x);
	check("split blocks do not overlap", verify(x, 64, 1) && verify(y, 64, 2));
	free(x);
	free(y);
}

/* 여러 블록을 동시에 살려 둔 채 사이사이를 해제한다. 살아 있는 블록의
   내용이 그대로여야 한다. */
static void test_interleaved(void)
{
	unsigned char *keep[16];
	unsigned char *drop[16];
	int i;
	int ok;

	for (i = 0; i < 16; i++) {
		keep[i] = malloc(256);
		drop[i] = malloc(256);
		if (keep[i] == 0 || drop[i] == 0) {
			check("interleaved setup", 0);
			return;
		}
		fill(keep[i], 256, (unsigned char) (i + 1));
	}
	for (i = 0; i < 16; i++) {
		free(drop[i]);
	}
	for (i = 0; i < 8; i++) {
		if (malloc(512) == 0) {
			check("interleaved refill", 0);
			return;
		}
	}
	ok = 1;
	for (i = 0; i < 16; i++) {
		if (verify(keep[i], 256, (unsigned char) (i + 1)) == 0) {
			ok = 0;
		}
	}
	check("live blocks survive neighbours being freed", ok);
	for (i = 0; i < 16; i++) {
		free(keep[i]);
	}
}

int main(int argc, char **argv)
{
	(void) argc;
	(void) argv;
	puts("mtest: app64/crt free-list allocator");
	test_basic();
	test_churn();
	test_coalesce();
	test_split();
	test_interleaved();
	if (failures == 0) {
		puts("mtest: all passed");
		return 0;
	}
	write(1, "mtest: ", 7);
	put_dec((unsigned long) failures);
	puts(" failed");
	return 1;
}
