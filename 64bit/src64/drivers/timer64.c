/*
 * timer64.c -- PIT(8254) 타이머, IRQ0
 *
 * 100Hz로 맞춘다(1193182 / 11932 = 약 100).
 * 매 틱마다 이벤트를 큐에 넣고태스크를 바꾼다.
 * 커널의 시간 단위 10ms 산출 근거
 */
#include <asmfunc64.h>
#include <int64.h>
#include <mtask64.h>
#include <timer64.h>

#define PIT_CTRL 0x0043
#define PIT_CNT0 0x0040
#define PIT_CNT2 0x0042
#define PIT_SPEAKER 0x0061
#define PIT_FREQUENCY 1193182ULL
#define PIT_CALIBRATION_COUNT 59659U
#define PIT_CALIBRATION_POLLS 10000000U
#define DEFAULT_TSC_FREQUENCY 1000000000ULL

struct TIMERCTL64 timerctl64;
static uint64_t tsc_frequency;

static uint64_t read_tsc64(void)
{
	uint32_t high;
	uint32_t low;

	__asm__ volatile ("lfence; rdtsc" : "=a" (low), "=d" (high));
	return ((uint64_t) high << 32) | low;
}

static void cpuid64(uint32_t leaf, uint32_t registers[4])
{
	__asm__ volatile ("cpuid"
		: "=a" (registers[0]), "=b" (registers[1]),
		  "=c" (registers[2]), "=d" (registers[3])
		: "a" (leaf), "c" (0));
}

static uint64_t cpuid_tsc_frequency64(void)
{
	uint32_t registers[4];
	uint32_t max_leaf;

	cpuid64(0, registers);
	max_leaf = registers[0];
	if (max_leaf >= 0x15U) {
		cpuid64(0x15U, registers);
		if (registers[0] != 0 && registers[1] != 0 && registers[2] != 0) {
			return (uint64_t) registers[2] * registers[1] / registers[0];
		}
	}
	if (max_leaf >= 0x16U) {
		cpuid64(0x16U, registers);
		if (registers[0] != 0) {
			return (uint64_t) registers[0] * 1000000ULL;
		}
	}
	return 0;
}

/* 채널 2 one-shot으로 TSC를 50ms 동안 보정한다. 채널 0과 IRQ가 아직
   준비되지 않은 storage 초기화 중에도 실제 시간 기준 timeout을 만들 수 있다. */
static uint64_t calibrate_tsc64(void)
{
	uint64_t elapsed;
	uint64_t start;
	uint64_t end;
	uint32_t polls;
	uint8_t speaker;

	speaker = io_in8(PIT_SPEAKER);
	io_out8(PIT_SPEAKER, speaker & (uint8_t) ~0x03U);
	io_out8(PIT_CTRL, 0xb0);
	io_out8(PIT_CNT2, (uint8_t) PIT_CALIBRATION_COUNT);
	io_out8(PIT_CNT2, (uint8_t) (PIT_CALIBRATION_COUNT >> 8));
	start = read_tsc64();
	io_out8(PIT_SPEAKER, (speaker & (uint8_t) ~0x02U) | 0x01U);
	for (polls = 0; polls < PIT_CALIBRATION_POLLS; polls++) {
		if ((io_in8(PIT_SPEAKER) & 0x20U) != 0) {
			break;
		}
	}
	end = read_tsc64();
	io_out8(PIT_SPEAKER, speaker);
	if (polls == PIT_CALIBRATION_POLLS || end <= start) {
		return 0;
	}
	elapsed = end - start;
	if (elapsed > UINT64_MAX / PIT_FREQUENCY) {
		return 0;
	}
	return elapsed * PIT_FREQUENCY / PIT_CALIBRATION_COUNT;
}

static uint64_t tsc_frequency64(void)
{
	if (tsc_frequency != 0) {
		return tsc_frequency;
	}
	tsc_frequency = cpuid_tsc_frequency64();
	if (tsc_frequency == 0) {
		tsc_frequency = calibrate_tsc64();
	}
	if (tsc_frequency == 0) {
		/* 고장 난 PIT에서도 각 호출부의 poll 상한이 최종 종료를 보장한다. */
		tsc_frequency = DEFAULT_TSC_FREQUENCY;
	}
	return tsc_frequency;
}

uint64_t poll_deadline64(uint32_t milliseconds)
{
	uint64_t frequency;
	uint64_t per_millisecond;
	uint64_t ticks;

	frequency = tsc_frequency64();
	per_millisecond = frequency / 1000U;
	if (milliseconds != 0 &&
			per_millisecond > (uint64_t) INT64_MAX / milliseconds) {
		ticks = INT64_MAX;
	} else {
		ticks = per_millisecond * milliseconds;
	}
	return read_tsc64() + ticks;
}

int poll_deadline_expired64(uint64_t deadline)
{
	return (int64_t) (read_tsc64() - deadline) >= 0;
}

void init_pit64(struct FIFO64 *fifo)
{
	io_out8(PIT_CTRL, 0x34);        /* 채널 0, 낮은 바이트부터, 모드 2 */
	io_out8(PIT_CNT0, 0x9c);        /* 분주비 11932 = 0x2e9c, 낮은 바이트 */
	io_out8(PIT_CNT0, 0x2e);        /* 높은 바이트 */
	timerctl64.count = 0;
	timerctl64.fifo = fifo;
}

void inthandler20_64(void)
{
	struct EVENT64 event;

	io_out8(PIC0_OCW2, 0x60);
	timerctl64.count++;
	event.type = EVENT64_TIMER;
	event.data = (uint32_t) timerctl64.count;
	fifo64_put(timerctl64.fifo, event);
	task_switch64();
}
