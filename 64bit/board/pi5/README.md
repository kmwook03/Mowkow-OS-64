# Raspberry Pi 5 boot files

아직 전용 SD 카드 이미지를 만들지 않는다. FAT32 부트 파티션에 다음 파일을
복사한다.

- `config.txt`: 이 디렉터리의 파일
- `kernel_2712.img`: `make aarch64`가 만든
  `build64/aarch64/kernel_2712.img`
- `HELLO.ELF`: M7k부터 `make aarch64`가 함께 만드는
  `build64/aarch64-app/hello.elf`
- `CAT.ELF`: M7l부터 `make aarch64`가 함께 만드는
  `build64/aarch64-app/cat.elf`
- `KTEST.ELF`: M7m부터 `make aarch64`가 함께 만드는
  `build64/aarch64-app/ktest.elf`
- `MTEST.ELF`: M7n부터 `make aarch64`가 함께 만드는
  `build64/aarch64-app/mtest.elf`
- `WTEST.ELF`: M7o부터 `make aarch64`가 함께 만드는
  `build64/aarch64-app/wtest.elf`
- `나노.ELF`: M7p부터 `make aarch64`가 함께 만드는
  `build64/aarch64-app/나노.elf`

전원을 넣으면 정상 부팅 시 녹색 ACT LED가 0.5초 간격으로 계속 켜지고 꺼진다.
초기 exception level이 예상과 다르면 panic code 1(0.15초 한 번 점멸 후 1.2초
정지)을 반복한다. M1에는 화면이나 시리얼 출력이 없다.

M2 이미지는 MMU를 켠 뒤 mailbox property channel로 1920x1080 32bpp
framebuffer를 요청한다. 성공하면 HDMI에 다음 문구가 나타나며 ACT LED의 M1
heartbeat도 계속된다.

```text
Mowkow OS
Raspberry Pi 5 / AArch64
M2: MMU + mailbox framebuffer OK

한글 화면 출력 성공
```

M2 조기 실패는 ACT LED panic code로 구분한다.

- 2회 점멸: mailbox 전송 timeout
- 3회 점멸: framebuffer 응답 또는 응답 값 검증 실패
- 아무 패턴도 없음: MMU 활성화 전후의 예외 가능성

M3a 이미지는 EL1 exception vector, GIC-400 CPU/distributor interface와 EL1
physical generic timer를 초기화한다. M2 화면 아래에 다음 문구를 표시한 뒤,
100회의 timer IRQ마다 점 하나를 추가한다.

```text
M3a: vectors + GIC + timer IRQ
IRQ: ........
```

점이 초당 하나씩 계속 늘어나고 ACT LED heartbeat도 유지되면 vector 진입,
GIC acknowledge/EOI, PPI 30 전달 및 `CNTP_TVAL_EL0` 재장전 경로가 정상이다.
처리하지 못한 synchronous exception은 화면에 vector, ESR, ELR, FAR를 출력하고
panic code 4를 반복한다.

M3b 이미지는 timer IRQ가 저장한 exception frame을 10 tick(100ms)마다 바꾸어
boot 태스크와 독립된 64KiB 스택의 worker 태스크를 round-robin으로 선점한다.
두 태스크가 모두 실행되고 두 번 이상 전환된 뒤에는 다음과 같이 초당 `S`를
하나씩 추가한다.

```text
M3b: exception-frame scheduler
SCH: SSSSSSSS
```

`?`가 계속 나오면 timer IRQ 자체는 동작하지만 두 실행 문맥 중 하나가 진행되지
않는 상태다. `S`가 계속 늘어나면서 ACT LED heartbeat가 유지되면 전체 레지스터,
`ELR_EL1`, `SPSR_EL1`, 스택 포인터의 저장/복원과 선점 전환이 정상이다.

M3c 이미지는 검증용 고정 2태스크 배열을 제거하고 공용 `memory64`와 `mtask64`의
태스크 할당, 레벨 선택 및 round-robin 큐를 사용한다. 정상 동작하면 다음 줄의
`M`이 초당 하나씩 추가된다.

```text
M3c: shared mtask64 scheduler
MTASK: MMMMMMMM
```

`M` 증가와 ACT LED heartbeat가 함께 유지되면 공용 `TASK64`의 AArch64 exception
frame 문맥, DAIF 기반 메모리 관리자 잠금, 동적 worker/idle 스택 할당 및 공용
`task_switch_prepare64()` 경로가 정상이다. worker는 실행 중 스스로
`task_sleep64()`로 잠들고 boot 태스크가 다시 깨우므로, `M`은 sleep/wakeup 큐
제거·재등록까지 한 번 이상 성공해야만 출력된다.

M3d 이미지는 기존 1GiB block identity map을 4KiB level-3 page 체계로
교체한다. TTBR0에는 부팅 중인 저주소 1GiB RAM과 BCM2712 mailbox, GIO_AON,
GIC Device 영역을 두고, TTBR1에는 `0xffffff8000000000`에서 시작하는 커널
high-half 별칭을 별도 루트로 설치한다. framebuffer 초기화 뒤 `AT S1E1R`로
저주소 커널, TTBR1 high-half 별칭, mailbox Device 주소와 의도적으로 비운
1GiB 경계를 검사하고 `AT S1E0R`로 커널 페이지의 EL0 접근 차단도 확인한다.
모두 통과하면 다음 문구가 나타난다.

```text
M3d: 4 KiB TTBR0 + TTBR1 paging
MTASK: MMMMMMMM
```

M3d 문구와 초당 `M`, ACT LED heartbeat가 모두 유지되면 4KiB 페이지 워크,
TTBR0/TTBR1 선택, 40-bit 물리 MMIO 변환 및 기존 framebuffer/IRQ/scheduler
회귀가 정상이다. 주소 변환 자체 검증에 실패하면 panic code 5를 반복한다.

M3e 이미지는 MMU를 켠 저주소 bootstrap 문맥에서
`0xffffff8000000000`의 TTBR1 high-half 별칭으로 분기하고 커널 SP도 high-half로
교체한다. 이후 TTBR0를 빈 user translation root로 교체하고 전체 EL1 TLB를
무효화한다. mailbox에는 high 주소를 물리주소로 변환해서 전달하며, framebuffer,
GIO_AON, GIC, 공용 메모리 관리자와 동적 태스크 스택은 모두 TTBR1 주소로
접근한다. 저주소 커널 주소가 더 이상 변환되지 않고 high-half 커널과 MMIO만
기대한 물리주소로 변환되는 자체 검사를 통과하면 다음 문구가 나타난다.

```text
M3e: TTBR1 high-half kernel + empty user TTBR0
MTASK: MMMMMMMM
```

M3e 문구와 초당 `M`, ACT LED heartbeat가 함께 유지되고 exception, `?`, panic
code 5가 없으면 high-half 실행, high SP 및 태스크 스택, 빈 user TTBR0 전환과
기존 장치/스케줄러 회귀 검증이 완료된 것이다.

## Verification status

- 2026-09-19: Raspberry Pi 5 실기에서 M1 부팅 검증 완료.
- FAT32 부트 파티션의 `config.txt`와 `kernel_2712.img`로 부팅했다.
- 녹색 ACT LED가 0.5초 켜짐/0.5초 꺼짐을 지속해서 반복하는 정상
  heartbeat를 확인했다.
- 이 결과로 펌웨어의 flat image 로드, `0x80000` 진입, EL2에서 EL1으로의
  전환, 스택/BSS 초기화, 시스템 카운터 및 GIO_AON LED 제어 경로를 확인했다.

- 2026-09-19: Raspberry Pi 5 실기에서 M2 framebuffer bring-up 검증 완료.
- HDMI에 청록색 배경과 M2 상태 문구, `한글 화면 출력 성공` 문구가
  정상 표시되는 것을 확인했다.
- 화면 출력 중에도 녹색 ACT LED의 0.5초 heartbeat가 지속되는 것을 확인했다.
- 이 결과로 39-bit identity map, EL1 MMU/캐시 활성화, BCM2712 MMIO Device
  매핑, mailbox property channel 8, 1920x1080 32bpp framebuffer 할당,
  framebuffer 캐시 정리 및 내장 영문/한글 글꼴 렌더링 경로를 확인했다.

- 2026-09-19: Raspberry Pi 5 실기에서 M3a timer IRQ 검증 완료.
- HDMI의 `IRQ:` 뒤에 점이 초당 하나씩 계속 추가되는 것을 확인했다.
- timer IRQ가 동작하는 동안에도 녹색 ACT LED의 0.5초 heartbeat가 유지되고,
  exception 보고나 panic code 4가 발생하지 않는 것을 확인했다.
- 이 결과로 2KiB 정렬 EL1 exception vector 진입, GIC-400의 PPI 30 전달과
  acknowledge/EOI, `CNTP_TVAL_EL0` 재장전 및 100Hz generic timer IRQ 경로를
  확인했다.

- 2026-09-19: Raspberry Pi 5 실기에서 M3b exception-frame 선점 전환 검증
  완료.
- HDMI의 `SCH:` 뒤에 `S`가 초당 하나씩 계속 추가되고, `?`, exception 보고,
  panic code 4가 발생하지 않는 것을 확인했다.
- 선점 전환 중에도 녹색 ACT LED의 0.5초 heartbeat가 유지되는 것을 확인했다.
- 이 결과로 boot 태스크와 별도 64KiB 스택의 worker 태스크가 100ms quantum으로
  round-robin 실행되며, `x0`-`x30`, `ELR_EL1`, `SPSR_EL1`, 스택 포인터를
  포함한 IRQ exception frame 저장/복원 및 다른 frame으로의 `eret` 경로를
  확인했다.

- 2026-09-19: Raspberry Pi 5 실기에서 M3c 공용 `mtask64` 통합 검증 완료.
- HDMI의 `MTASK:` 뒤에 `M`이 초당 하나씩 계속 추가되고, 선점 및 sleep/wakeup
  동작 중에도 녹색 ACT LED의 0.5초 heartbeat가 유지되는 것을 확인했다.
- `?`, exception 보고, panic code 4가 발생하지 않는 것을 확인했다.
- 이 결과로 공용 `TASK64` 할당, `memory64` 기반 idle/worker 스택 할당,
  `task_switch_prepare64()`의 레벨 선택과 round-robin, AArch64 exception frame
  문맥 저장, worker의 `task_sleep64()` run queue 제거 및 boot 태스크의 wakeup
  재등록 경로를 확인했다.

- 2026-09-19: Raspberry Pi 5 실기에서 M3d 4KiB TTBR0/TTBR1 페이징 검증 완료.
- HDMI에 `M3d: 4 KiB TTBR0 + TTBR1 paging`이 표시되고, `MTASK:` 뒤의
  `M`이 초당 하나씩 계속 증가하는 것을 확인했다.
- 선점 및 페이징 동작 중에도 ACT LED heartbeat가 유지되고, `?`, exception,
  panic code 5가 발생하지 않는 것을 확인했다.
- 이 결과로 4KiB level-3 페이지 워크, 저주소 TTBR0 bootstrap 매핑,
  TTBR1 high-half 별칭, 40-bit mailbox Device 주소 변환, EL0의 커널 페이지
  접근 차단 및 기존 framebuffer/IRQ/scheduler 경로의 회귀 없음을 확인했다.

- 2026-09-19: Raspberry Pi 5 실기에서 M3e TTBR1 high-half 커널 전환 검증
  완료.
- HDMI에 `M3e: TTBR1 high-half kernel + empty user TTBR0`가 표시되고,
  `MTASK:` 뒤의 `M`이 초당 하나씩 계속 증가하는 것을 확인했다.
- high-half 커널과 선점 태스크가 실행되는 동안 ACT LED heartbeat가 유지되고,
  `?`, exception, panic code 5가 발생하지 않는 것을 확인했다.
- 이 결과로 TTBR1 high-half 코드 실행, high-half 커널 및 동적 태스크 스택,
  저주소 bootstrap TTBR0 제거와 빈 user TTBR0 전환, TLB 무효화, high-half
  framebuffer/MMIO 접근 및 mailbox 물리주소 변환 경로를 확인했다.

M3의 exception vector, timer IRQ, 선점형 공용 스케줄러, 4KiB 페이지 테이블,
TTBR1 high-half 커널과 user TTBR0 분리까지 Raspberry Pi 5 실기 검증을 모두
완료했다.

## M4 SDHCI/FAT32 verification

M4는 BCM2712 `sdio1`을 polling PIO 방식으로 초기화하고 공용 `block64`,
`cache64`, `fd64`를 통해 부팅 FAT32 파티션을 마운트한다. 정상적인 첫 부팅은
다음 문구를 출력하고 루트에 `M4TEST.TXT`를 생성한 뒤 `fd64_sync()`로 내보낸다.

```text
M4a: SDHCI card ready
M4a: FAT32 mounted from boot SD
M4b: write smoke created; reboot to verify persistence
```

이 문구가 나온 뒤 전원을 정상적으로 껐다 켠 두 번째 부팅에서 다음 문구가
나오면 CMD24, 캐시 writeback, FAT/디렉터리 metadata 기록과 재부팅 후 읽기를
모두 통과한 것이다.

```text
M4b: write smoke persisted across reboot
```

실패 panic code는 다음과 같다.

- 6회: SDHCI 초기화 또는 카드 명령 실패
- 7회: FAT32 마운트 실패
- 8회: 파일 생성, CMD24 또는 `fd64_sync()` 실패
- 9회: 이전 부팅의 `M4TEST.TXT` 크기나 내용이 예상과 다름
- 10회: `H04.FNT`가 없거나 크기/읽기 결과가 올바르지 않음

### M4 troubleshooting

#### SDHCI 첫 MMIO 접근에서 level-1 data abort

- 증상: `ESR=0x96000005`, `ELR=0xffffff8000081c2c`,
  `FAR=0xffffff9000fff0fe`.
- 해석: `FAR`는 SDHCI `HOST_VERSION`(`0x1000fff0fe`)의 high-half 주소이며
  FSC `0x05`는 level-1 translation fault다.
- 원인: mailbox/GIC는 L1 슬롯 `0x41`, SDHCI는 `0x40`인데 기존 MMU가
  mailbox 슬롯만 `mmio_l2`에 연결했다.
- 해결: bootstrap TTBR0와 high-half TTBR1 양쪽의 SDHCI L1 슬롯을
  `mmio_l2`에 연결하고 `arch64_mmu_self_test()`에 SDHCI 주소 변환 검사를
  추가했다.

#### `BLOCK64_OPS.read` 호출에서 저주소 instruction abort

- 증상: `ESR=0x86000005`, `ELR=FAR=0x0000000000081b00`.
- 해석: 해당 주소는 `sdhci64_read()`의 link-time 물리 주소이며 FSC `0x05`는
  instruction level-1 translation fault다.
- 원인: 커널은 저주소로 링크된 뒤 TTBR1 high-half 별칭에서 실행된다. 직접
  호출은 PC-relative라 정상이나, 정적으로 초기화한 `sdhci64_ops` 내부의 함수와
  문자열 포인터는 저주소 값으로 남았다. 빈 TTBR0 상태에서 그 함수 포인터를
  간접 호출해 fault가 발생했다.
- 해결: AArch64가 `sdhci64_ops`를 선택할 때 모든 내장 포인터를
  `ARCH64_KERNEL_VA_BASE`의 high-half 주소로 rebase한 runtime ops 테이블을
  만들도록 했다.

## Verification status — M4

- 2026-09-20: Raspberry Pi 5 실기에서 SDHCI 초기화와 FAT32 mount 검증 완료.
- HDMI에 `M4a: SDHCI card ready`, `M4a: FAT32 mounted from boot SD`가 표시되고
  `MTASK:` 뒤의 `M`이 계속 증가하는 것을 확인했다.
- 이 결과로 SDHCI CMD0/CMD8/ACMD41/CMD2/CMD3/CMD9/CMD7 초기화,
  CMD17 PIO 읽기, MBR 파티션 탐색, 공용 cache64/fd64 FAT32 mount 및 기존
  timer/scheduler 회귀 없음을 확인했다.
- 2026-09-20: CMD24와 재부팅 후 파일 유지 검증 완료.
- 첫 부팅에서 `M4b: write smoke created; reboot to verify persistence`, 두 번째
  부팅에서 `M4b: write smoke persisted across reboot`와 계속 증가하는 `M`을
  확인했다.
- 이 결과로 CMD24 PIO 쓰기, cache64 writeback, FAT 두 사본과 디렉터리
  metadata 동기화 및 전원 재인가 뒤 CMD17 재읽기를 확인했다.

### M4c — SD 카드 한글 글꼴

M4c는 FAT32 루트의 `H04.FNT`를 정확히 11,520바이트 읽어 framebuffer 한글
렌더러의 활성 글꼴로 교체한다. 성공 기준은 다음 두 줄이 정상 한글 모양으로
표시되고 이후 `M`이 계속 증가하는 것이다.

```text
M4c: H04.FNT loaded from boot SD
M4c: SD 카드 한글 글꼴 적용 성공
```

- 2026-09-20: Raspberry Pi 5 실기에서 M4c 검증 완료.
- `H04.FNT`를 부팅 SD 카드의 FAT32 파티션에서 읽고, framebuffer 렌더러의
  활성 한글 글꼴로 교체한 뒤 `M4c: SD 카드 한글 글꼴 적용 성공`이 정상적인
  한글 모양으로 표시되는 것을 확인했다.
- 이후에도 `MTASK:` 뒤의 `M`과 ACT LED heartbeat가 계속되어 SD 읽기와
  framebuffer 글꼴 교체가 timer/scheduler에 회귀를 만들지 않았음을 확인했다.

M4a의 SDHCI/CMD17/FAT32 mount, M4b의 CMD24/동기화/재부팅 지속성, M4c의
부팅 SD 카드 한글 글꼴 로드를 모두 Raspberry Pi 5 실기에서 검증했다.

## M5 PCIe/RP1 verification

### M5a — firmware-preserved RP1 link와 config-space

M5a는 `config.txt`의 `pciex4_reset=0`으로 firmware가 보존한 BCM2712 PCIe x4
링크를 검사한다. 링크의 Data Link Active와 PHY Link Up 비트를 먼저 확인하고,
root bridge의 primary/secondary/subordinate bus를 `0/1/1`로 배정한 뒤에만 bus 1,
device 0, function 0의 config-space를 읽는다. 링크가 내려간 상태에서 downstream
window에 접근하면 CPU abort가 발생할 수 있기 때문이다.

RP1의 예상 vendor/device ID는 `0x00011de4`다. 성공 시 다음 형식으로 ID,
class/revision과 BAR0을 표시하고 `M`이 계속 증가해야 한다.

```text
M5a: RP1 PCIe link up, id=0x00011de4 class/rev=0x........ bar0=0x........
MTASK: MMMMM...
```

링크 또는 config-space 검증 실패는 panic code 11을 반복한다.

초기 구현은 firmware가 root bridge bus 번호도 설정했다고 가정해 link-up 뒤
곧바로 bus 1을 읽었고, 실기에서 `RP1 config-space probe failed`가 발생했다.
`pciex4_reset=0`은 link 보존에는 필요하지만 PCI enumeration을 대신하지 않는다.
따라서 root bridge의 type-1 bus-number register를 명시적으로 설정하도록 고쳤고,
추가 실패 시 raw ID와 `root-buses` 값을 함께 표시한다.

- 2026-09-20: Raspberry Pi 5 실기에서 M5a 검증 완료.
- `id=0x00011de4`, `class/rev=0x02000000`, `bar0=0x80410000`을 확인했고,
  뒤이어 `M`이 계속 출력되어 config-space 접근 후에도 scheduler가 정상 동작했다.

### M5b — BAR1 RP1 peripheral window

M5b는 RP1 config-space의 BAR1과 root bridge의 type-1 memory base를 읽어 BCM2712
PCIe2 outbound CPU 주소
`0x1f00000000 + ((BAR1 & ~0xf) - root_memory_base)`로 변환한다. firmware가
downstream PCI memory window와 BAR를 높은 PCI 주소에 배치할 수 있으므로 둘 다
0이라고 가정하지 않는다. 이 주소에서 RP1 SYSINFO의 chip ID와 platform
register를 읽는다. 예상 chip ID는 RP1 C0의 `0x20001927`이다.

성공 시 다음 줄과 이후 계속 증가하는 `M`을 확인한다. `bar1`과 `platform` 값은
실기에서 출력된 값을 그대로 기록한다.

```text
M5b: outbound win0 pci-base=0x........ base/limit=0x........ high=0x......../0x........ root-command=0x........
M5b: RP1 BAR1 MMIO ready, bar1=0x........ root-mem-base=0x........ root-mem-limit=0x........ chip-id=0x20001927 platform=0x........
MTASK: MMMMM...
```

PCI command의 Memory Space Enable이 꺼져 있거나, BAR1이 I/O BAR이거나, chip ID가
다르면 진단값을 출력하고 panic code 12를 반복한다.

#### BAR1 PCI 주소를 CPU outbound offset으로 오해

- 증상: `status=0xfffffffd`, `command/status=0x00100146`,
  `bar1=0x80000000`, `chip-id=0xdeaddead`와 panic code 12가 표시됐다.
- 해석: Memory Space Enable은 켜져 있었지만 `0xdeaddead`는 잘못된 downstream
  MMIO 주소에서 반환된 오류 값이다.
- 원인: 초기 구현은 BAR1의 PCI 주소 `0x80000000` 전체를 CPU outbound base에
  더했다. firmware는 root bridge memory base 역시 `0x80000000`으로 설정하므로,
  BAR1이 가리키는 실제 outbound offset은 0이다.
- 해결: root bridge type-1 memory base/limit register에서 PCI memory base를
  추출하고, BAR1에서 이를 뺀 값을 CPU outbound base에 더하도록 수정했다. 실패
  진단과 성공 문구에도 `root-mem-base`를 추가했다.

위 주소 보정만 적용한 두 번째 실기에서도 같은 `chip-id=0xdeaddead`가 반환됐다.
이는 주소 계산과 별개로 BCM2712의 CPU-to-PCIe outbound window register가
설정되지 않았음을 뜻한다. `pciex4_reset=0`으로 link와 endpoint config-space는
보존됐지만 CPU MMIO translation은 사용할 수 없는 상태였다. Linux
`pcie-brcmstb` 드라이버와 같은 방식으로 window 0의 PCI base, CPU base/limit
low/high register를 root bridge memory aperture에 맞춰 설정하고, root bridge의
Memory Space Enable과 Bus Master Enable도 켜도록 보완했다. 성공/실패 진단에는
aperture 전체를 확인할 수 있도록 `root-mem-limit`도 추가했다.

- 2026-09-20: Raspberry Pi 5 실기에서 M5b 검증 완료.
- outbound window 0은 `pci-base=0x80000000`, `base/limit=0x3ff00000`,
  `high=0x0000001f/0x0000001f`, root command `0x00000146`으로 설정됐다.
- root bridge aperture `0x80000000-0xbfffffff`와 BAR1 `0x80000000`을 변환해
  RP1 SYSINFO에서 `chip-id=0x20001927`, `platform=0x00000002`를 읽었다.
- 이후에도 `M`이 계속 출력되어 outbound MMIO 접근 뒤 scheduler 동작을 확인했다.

### M5c — RP1 UART0 register window

M5c는 BAR1 peripheral window의 `+0x30000`에 있는 RP1 UART0 PL011-AXI에서
`FR`, `IBRD`, `FBRD`, `LCR_H`, `CR`을 읽는다. 아직 clock, pinmux, baud rate 또는
UART enable 상태를 변경하지 않는 읽기 전용 검사다. RP1 device tree는 UART0을
`arm,pl011-axi`, peripheral ID `0x00341011`로 정의한다.

성공 시 다음 형식의 줄과 계속 증가하는 `M`을 확인한다. register 값은 firmware가
남긴 상태에 따라 달라질 수 있다.

```text
M5c: RP1 UART0 registers accessible, fr=0x........ ibrd=0x........ fbrd=0x........ lcrh=0x........ cr=0x........
MTASK: MMMMM...
```

downstream 오류 값 `0xdeaddead` 또는 `0xffffffff`가 반환되면 panic code 13을
반복한다.

- 2026-09-20: Raspberry Pi 5 실기에서 M5c 검증 완료.
- UART0에서 `FR=0x00000197`, `IBRD=0`, `FBRD=0`, `LCR_H=0`, `CR=0x300`을
  읽었다. UART는 enable되지 않았지만 TX/RX 기능 비트가 남아 있는 초기 상태였다.

### M5d — RP1 UART0 internal loopback

M5d는 외부 배선 없이 PL011의 internal loopback으로 송수신 datapath를 검사한다.
firmware가 제공한 48 MHz `clk_uart`를 기준으로 115200 baud, 8 data bits, FIFO를
설정하고 `0x4d` 한 바이트를 송신한다. RX FIFO에서 같은 값과 오류 비트 0을
확인한 뒤 M5c에서 읽은 기존 divisor, line-control, control 값을 복원한다.

```text
M5d: RP1 UART0 internal loopback echoed 0x0000004d
MTASK: MMMMM...
```

TX FIFO 준비 또는 RX loopback에 timeout이 발생하거나 수신 값이 다르면 진단값을
출력하고 panic code 14를 반복한다. 이 단계는 GPIO14/15 pinmux나 외부 UART
어댑터를 요구하지 않는다.

- 2026-09-20: Raspberry Pi 5 실기에서 M5d 검증 완료.
- UART0 internal loopback에서 송신한 `0x4d`가 오류 없이 그대로 수신됐고,
  이후 `M` 출력도 계속됐다.

M5a의 PCI config-space, M5b의 outbound BAR1 MMIO, M5c의 UART0 register window,
M5d의 UART 송수신 datapath를 모두 실기에서 검증했다.

## M6 xHCI/USB HID verification

### M6a — RP1 dual xHCI capability probe

RP1의 USB0/USB1은 BAR1의 `+0x200000`, `+0x300000`에 각각 1 MiB DWC3 host
register window를 제공한다. M6a는 상태를 변경하지 않고 두 window의 xHCI
`CAPLENGTH/HCIVERSION`과 `HCSPARAMS1`을 읽는다. capability length는 0x20 이상인
4-byte 정렬값, interface version은 1.x여야 한다.

```text
M6a: RP1 xHCI0 cap/hcs1=0x......../0x........ xHCI1 cap/hcs1=0x......../0x........
MTASK: MMMMM...
```

오류 응답 또는 잘못된 capability header가 나오면 원시 register 값을 표시하고
panic code 15를 반복한다.

- 2026-09-20: Raspberry Pi 5 실기에서 M6a 검증 완료.
- 두 controller 모두 `CAPLENGTH/HCIVERSION=0x01100020`,
  `HCSPARAMS1=0x03000440`을 반환했다. 각각 xHCI 1.1, capability length 0x20,
  64 device slots, 4 interrupters, 3 ports를 제공한다.

### M6b — controller halt/reset lifecycle

M6b는 두 controller의 Run/Stop을 내리고 `USBSTS.HCH`를 확인한 뒤
`USBCMD.HCRST`를 수행한다. 각 단계는 1초 timeout을 두며 reset bit와
`USBSTS.CNR`이 모두 해제되고 controller가 halted 상태이면 성공이다. 이 단계부터
USB controller 상태를 변경하므로 연결된 장치는 일시적으로 reset된다.

```text
M6b: RP1 xHCI reset complete, xhci0 cmd/sts=0x......../0x........ xhci1 cmd/sts=0x......../0x........
MTASK: MMMMM...
```

halt, host-controller reset 또는 Controller Not Ready 해제에 실패하면 초기 command와
status를 표시하고 panic code 16을 반복한다.

- 2026-09-20: Raspberry Pi 5 실기에서 M6b 검증 완료.
- 두 controller 모두 reset 뒤 `USBCMD=0x00000000`,
  `USBSTS=0x00000001(HCH)`인 정상 halted 상태가 됐다.

### M6c — 연결된 xHCI controller의 DMA structures와 Run

M6c는 xHCI0과 xHCI1의 root port를 검사해 장치가 연결된 controller를 선택한다.
둘 다 연결됐거나 둘 다 비어 있으면 xHCI0을 우선한다. 선택된 controller에 64-slot
DCBAA, 256-entry command ring, 256-entry event ring과 단일 ERST entry를 설치한다.
HCSPARAMS2가 요구하면 최대 4개의 scratchpad도 제공한다.
RP1 bus master가 system RAM을 보는 inbound alias `0x10_00000000 + physical`을 DMA
주소로 사용하며 controller를 Run 상태로 전환해 `USBSTS.HCH` 해제를 확인한다.
선택되지 않은 controller는 M6b의 halted 상태로 남겨 둔다.

```text
M6c: RP1 xHCI running, controller=0x........ cmd/sts=0x......../0x........
MTASK: MMMMM...
```

DMA 구조 설치 또는 Run 전환이 실패하면 `HCSPARAMS2`와 command/status를 표시하고
panic code 17을 반복한다.

- 2026-09-20: Raspberry Pi 5 실기에서 M6c 검증 완료.
- xHCI0은 `USBCMD=0x00000001`, `USBSTS=0`으로 Run 상태가 됐고, 세 포트는
  모두 `PORTSC=0x000002a0`이었다. 이후에도 `M` 출력이 계속됐다.

### M6d — command/event ring DMA round-trip

M6d는 command ring에 cycle bit가 설정된 No-op Command TRB를 넣고 doorbell 0을
울린다. event ring을 cache invalidate하며 polling해 Command Completion Event,
Success completion code와 원래 command TRB의 RP1 DMA 주소를 모두 확인한다. 이를
통해 command fetch와 event write 양방향 DMA 및 cache maintenance를 검증한다.

```text
M6d: RP1 xHCI0 No-op completion, event=0x......../0x........ ptr-lo=0x........
MTASK: MMMMM...
```

1초 안에 event cycle bit가 바뀌지 않거나 event type, completion code, command
pointer가 다르면 원시 event와 `USBSTS`를 표시하고 panic code 18을 반복한다.

#### No-op command의 event가 생성되지 않음

- 증상: `status=0xfffffffe`, event와 pointer가 모두 0이고 `USBSTS=0`인 채로
  timeout이 발생했다.
- 해석: controller는 Run 상태를 유지했지만 command ring을 DMA로 가져오지
  못했다. 따라서 xHCI register/ring 설정 문제가 아니라 RP1에서 system RAM으로
  향하는 PCIe inbound translation 문제다.
- 원인: command/event buffer에는 RP1의 system-RAM alias
  `0x10_00000000 + physical`을 사용했지만, BCM2712 root complex의 해당 64 GiB
  inbound aperture를 RC BAR4에 설정하지 않았다.
- 해결: RC BAR4를 PCIe `0x10_00000000`, 64 GiB 크기로 설정하고 CPU address 0으로
  UBUS remap한다. `MISC_CTRL.SCB_ACCESS_EN`도 명시적으로 켠 뒤 xHCI DMA 구조를
  설치한다.

- 2026-09-20: Raspberry Pi 5 실기에서 M6d 검증 완료.
- Command Completion Event는 `status=0x01000000`, `control=0x00008401`,
  command pointer low `0x002a1000`을 반환했고 이후에도 `M` 출력이 계속됐다.
- 이 결과로 RC BAR4 inbound translation, RP1의 command TRB fetch, event TRB write와
  양방향 cache maintenance가 정상 동작함을 확인했다.

### M6e — 연결 포트 감지와 root-port reset

M6e는 M6c에서 선택한 xHCI controller의 Supported Protocol extended capability를
따라 각 root port가 USB 2.x 또는 USB 3.x인지 판별한다. 3초 안에 연결된 포트를
찾은 뒤 USB 2.x에는 Port Reset,
USB 3.x에는 Warm Port Reset을 요청하고 50ms recovery delay 뒤 `CCS`, `PED`, port
power와 negotiated speed를 확인한다.

키보드를 부팅 전부터 연결하면 No-op Command Completion보다 Port Status Change
Event가 먼저 올 수 있다. M6d는 command event가 나올 때까지 앞선 비동기 event를
소비하고 ERDP를 전진시키므로 이 경우에도 그대로 통과한다.

실기 검증 전 Raspberry Pi 5의 파란색 USB 3 Type-A 포트에 USB 키보드를 꽂고
전원을 인가한다. 성공하면 다음 줄과 계속 증가하는 `M`을 확인한다.

```text
M6e: RP1 xHCI port reset complete, port/protocol=0x......../0x........ portsc=0x......../0x........
MTASK: MMMMM...
```

연결된 포트가 없거나 protocol capability를 찾지 못하거나 reset/enable이 완료되지
않으면 raw port와 `PORTSC` 전후 값을 표시하고 panic code 19를 반복한다.

#### Port Reset 요청과 함께 port power가 꺼짐

- 증상: USB 2.x port 1을 찾았지만 `status=0xfffffffb(-5)`, reset 전후
  `PORTSC=0x000206e1/0x00020080`로 완료 change bit가 설정되지 않았다.
- 해석: reset 전 값에는 `PP(bit 9)`가 있었지만 이후 값에서는 사라졌으며 port link
  state도 Polling에서 Disabled로 바뀌었다. 즉 reset 자체가 실패한 것이 아니라
  reset 요청을 기록할 때 root-port 전원을 함께 껐다.
- 원인: `PORTSC`를 변경할 때 RO/RWS 필드만 보존하고 RW1C 필드는 0으로 만드는
  neutral mask를 `0x0000fde9`로 잘못 계산해 `PP=0x200`을 빠뜨렸다.
- 해결: port power까지 보존하는 `0x0000ffe9`로 수정했다. 기존 change bit는
  기록하지 않으므로 의도치 않게 clear하지 않으면서 `PP`, link state와 protocol
  speed 필드를 유지한 채 PR/WPR만 요청한다.

- 2026-09-20: Raspberry Pi 5 실기에서 M6e 검증 완료.
- xHCI0의 USB 2.x port 1이 `PORTSC=0x000206e1`로 연결 감지됐고, reset 후
  `0x00220603`에서 `CCS`, `PED`, `PP`, speed ID 1과 `PRC`를 확인했다.
- 이후에도 `M` 출력이 계속되어 port reset과 50ms recovery delay 뒤에도 기존
  timer/scheduler가 정상 동작함을 확인했다.

### M6f — Enable Slot command

M6f는 command ring의 다음 TRB에 Enable Slot Command를 넣고 doorbell 0을 울린다.
M6e의 port reset이 만든 Port Status Change Event가 먼저 있으면 이를 소비한 뒤
Command Completion Event를 찾아 success completion code, command pointer와 할당된
slot ID를 검증한다. command/event ring의 producer/consumer 위치와 cycle state는
이 단계부터 다음 명령에서도 이어서 사용할 수 있도록 유지한다.

M6e와 동일하게 USB 키보드를 연결한 채 새 이미지를 부팅한다. 추가 조작은 필요
없다. 성공 시 다음 줄과 계속 증가하는 `M`을 확인한다.

```text
M6f: RP1 xHCI0 slot enabled, slot=0x........ event=0x01000000/0x........ ptr-lo=0x........
MTASK: MMMMM...
```

Enable Slot completion이 timeout, 오류 completion code, 잘못된 command pointer 또는
범위를 벗어난 slot ID를 반환하면 원시 event 값을 표시하고 panic code 20을 반복한다.

- 2026-09-20: Raspberry Pi 5 실기에서 M6f 검증 완료.
- Enable Slot은 slot 1을 할당했고 Command Completion Event는
  `status=0x01000000`, `control=0x01008401`, command pointer low
  `0x002a1010`이었다. 이후에도 `M` 출력이 계속됐다.

### M6g — Device/EP0 context와 Address Device

M6g는 controller가 알린 32/64-byte context 크기에 맞춰 Input Context, Device
Context와 EP0 transfer ring을 만든다. slot context에는 M6e의 root-port 번호와
negotiated speed를, EP0 context에는 속도에 맞는 max packet size와 dequeue pointer를
설정하고 DCBAA의 M6f slot에 Device Context를 연결한다. Address Device Command가
완료되면 controller가 기록한 USB device address가 0이 아니고 slot state가
Addressed(2)인지 확인한다.

키보드는 같은 포트에 그대로 연결한다. 성공 시 다음 줄과 계속 증가하는 `M`을
확인한다.

```text
M6g: RP1 xHCI0 device addressed, address/state=0x......../0x00000002 ctx/mps=0x......../0x........
MTASK: MMMMM...
```

context 구성, Address Device completion 또는 output slot context 검증이 실패하면
event와 context 진단값을 표시하고 panic code 21을 반복한다.

- 2026-09-20: Raspberry Pi 5 실기에서 M6g 검증 완료.
- controller는 64-byte context 형식을 사용했고 EP0 초기 max packet은 8이었다.
  Address Device 뒤 USB address 1과 Addressed slot state 2를 확인했다.
- 이후에도 `M` 출력이 계속되어 Device Context와 EP0 ring을 설치한 뒤에도 기존
  timer/scheduler가 정상 동작함을 확인했다.

### M6h — EP0 GET_DESCRIPTOR 8-byte transfer

M6h는 EP0 transfer ring에 Setup/Data/Status Stage TRB로 구성한 표준
`GET_DESCRIPTOR(Device)` control transfer를 넣고 slot doorbell의 endpoint target 1을
울린다. Status Stage의 Transfer Event가 success인지, residual length가 0인지,
slot/endpoint/TRB pointer가 모두 일치하는지 확인한 뒤 device descriptor 첫 8바이트의
length, type, USB version, device class/subclass/protocol과 `bMaxPacketSize0`을 검증한다.

키보드는 같은 포트에 그대로 연결한다. 성공 시 다음 줄과 계속 증가하는 `M`을
확인한다.

```text
M6h: USB device descriptor8, usb/class=0x......../0x........ mps=0x........ event=0x01000000/0x........
MTASK: MMMMM...
```

control transfer timeout, 오류 completion, 잘못된 event routing 또는 descriptor
header/max-packet 검증 실패 시 원시 event 값을 표시하고 panic code 22를 반복한다.

- 2026-09-20: Raspberry Pi 5 실기에서 M6h 검증 완료.
- descriptor 첫 8바이트에서 USB version `0x0110`, device class/subclass/protocol 0,
  `bMaxPacketSize0=8`을 확인했다.
- Transfer Event는 `status=0x01000000`, `control=0x01018001`이었고 이후에도
  `M` 출력이 계속됐다.

### M6i — 전체 descriptor와 HID boot keyboard 탐색

M6i는 18-byte Device Descriptor, 9-byte Configuration Descriptor header, 그리고
header의 `wTotalLength`만큼 전체 configuration을 차례로 EP0에서 읽는다. descriptor
chain을 경계 검사하며 순회해 class/subclass/protocol이 `3/1/1`인 HID boot keyboard
interface와 interrupt-IN endpoint를 찾고 configuration/interface/endpoint 번호,
max packet size와 interval을 보존한다.

키보드는 같은 포트에 그대로 연결한다. 성공 시 다음 줄과 계속 증가하는 `M`을
확인한다. `vid/pid`는 little-endian으로 vendor ID가 하위 16비트에 표시된다.

```text
M6i: USB boot keyboard found, vid/pid=0x........ cfg/intf/ep=0x......../0x......../0x........ mps/interval=0x......../0x........
MTASK: MMMMM...
```

descriptor 전송/형식/길이가 잘못됐거나 boot-keyboard interrupt-IN endpoint가 없으면
부분 파싱 결과와 마지막 event를 표시하고 panic code 23을 반복한다.

- 2026-09-20: Raspberry Pi 5 실기에서 M6i 검증 완료.
- keyboard는 VID `0x040b`, PID `0x0a67`, configuration 1, interface 0의 HID boot
  keyboard이며 interrupt-IN endpoint `0x81`, max packet 8, interval 1ms였다.
- 이후에도 `M` 출력이 계속되어 연속 EP0 transfer와 descriptor parser 동작을
  확인했다.

### M6j — SET_CONFIGURATION과 interrupt endpoint 구성

M6j는 EP0에서 표준 `SET_CONFIGURATION` 요청을 완료한 뒤 interrupt-IN endpoint의
DCI를 endpoint address에서 계산한다. output slot context를 input context로 복사해
Context Entries를 확장하고, full-speed interval을 xHCI microframe exponent로 변환해
Interrupt-IN Endpoint Context와 전용 transfer ring을 설치한다. Configure Endpoint
Command 완료 후 output endpoint state가 Running(1)인지 확인한다.

키보드는 같은 포트에 그대로 연결한다. 성공 시 다음 줄과 계속 증가하는 `M`을
확인한다.

```text
M6j: USB keyboard configured, ep/state/interval=0x......../0x00000001/0x........ event=0x01000000/0x........
MTASK: MMMMM...
```

SET_CONFIGURATION, Configure Endpoint completion 또는 endpoint state 검증이 실패하면
부분 상태와 command event를 표시하고 panic code 24를 반복한다.

- 2026-09-20: Raspberry Pi 5 실기에서 M6j 검증 완료.
- endpoint `0x81`은 DCI 3, Running state 1이 됐고 full-speed 1ms polling
  interval은 xHCI interval 3으로 설정됐다. Configure Endpoint event는
  `status=0x01000000`, `control=0x01008401`이었다.
- 이후에도 `M` 출력이 계속되어 SET_CONFIGURATION과 endpoint context 설치를
  확인했다.

### M6k — HID boot protocol과 첫 interrupt report

M6k는 HID class `SET_PROTOCOL(boot)` 요청을 interface 0에 보내고 interrupt-IN
transfer ring에 report buffer를 가리키는 Normal TRB를 제출한다. 화면에 안내를 먼저
표시하고 10초 동안 Transfer Event를 기다린다. 8-byte boot keyboard report에서
modifier와 첫 non-zero keycode를 확인한다.

새 이미지로 부팅해 다음 안내가 나오면 10초 안에 `A` 키를 한 번 누른다. HID usage
ID에서 `A`는 `0x04`이므로 modifier 없이 눌렀다면 key 값이 4여야 한다.

```text
M6k: press A on the USB keyboard within 10 seconds
M6k: USB keyboard report received, ep/mod/key=0x00000003/0x00000000/0x00000004 event=0x01000000/0x........
MTASK: MMMMM...
```

SET_PROTOCOL, interrupt transfer, event routing 또는 non-zero report 검증이 실패하면
원시 report/event 값을 표시하고 panic code 25를 반복한다.

- 2026-09-20: Raspberry Pi 5 실기에서 M6k 검증 완료.
- `A` 입력은 DCI 3에서 modifier 0, HID usage `0x04`로 수신됐고 Transfer Event는
  `status=0x01000000`, `control=0x01038001`이었다.
- 이후에도 `M` 출력이 계속되어 HID boot protocol과 실제 interrupt-IN DMA 경로를
  확인했다.

### M6l — key release와 KEY64 make/break 변환

M6l는 M6k의 `A` 입력 뒤 interrupt-IN TRB를 하나 더 제출해 modifier와 모든 key
usage가 0인 release report를 실제 장치에서 받는다. 새 `usbhid64.c`의 usage table은
HID usage를 공용 console이 쓰는 Set-1/`KEY64_EXT` 표현으로 변환하며 release에는
하위 바이트의 break bit를 붙인다.

M6k 안내에서 `A`를 누른 뒤 손을 뗀다. 이미 짧게 눌렀다면 release report가 endpoint에
대기하므로 M6l 안내 직후 완료될 수 있다.

```text
M6l: release A on the USB keyboard within 10 seconds
M6l: USB HID A make/break translated=0x0000001e/0x0000009e event=0x01000000/0x........
MTASK: MMMMM...
```

release report, Transfer Event 또는 usage 변환이 실패하면 원시 값을 표시하고 panic
code 26을 반복한다.

- 2026-09-20: Raspberry Pi 5 실기에서 M6l 검증 완료.
- HID usage `0x04`의 make/break가 공용 key 표현 `0x001e/0x009e`로 변환됐고,
  release Transfer Event는 `status=0x01000000`, `control=0x01038001`이었다.
- 이후에도 `M` 출력이 계속됐다.

### M6 정상 출력 축약

이 단계에서 M6a부터 M6i까지의 정상 출력을 먼저 한 줄로 합쳤으며, M6u 검증 완료
뒤에는 아래 `화면 출력 축약` 절처럼 M6a부터 M6u 전체를 최종 한 줄로 합쳤다.
각 단계의 상세 실패 진단과 panic code는 계속 유지한다.

### M6m — 공용 event FIFO 전달

M6m는 M6l에서 물리적으로 확인한 `A` make/break를 각각 `EVENT64_KEYBOARD`로 감싸
공용 `FIFO64`에 넣는다. FIFO에서 두 이벤트를 다시 꺼내 순서, type, data와 최종
empty 상태를 확인한다. 다음 단계에서 지속적인 USB report producer를 기존 console
consumer에 연결할 때 사용하는 이벤트 형식과 큐 경로를 검증하는 단계다.

M6k/M6l과 동일하게 `A`를 눌렀다 놓는다. 성공 시 다음 문구와 계속 증가하는 `M`을
확인한다.

```text
M6m: USB keyboard FIFO make/break OK
MTASK: MMMMM...
```

변환 또는 FIFO put/get 검증이 실패하면 잔여 이벤트 수와 마지막 type/data를 표시하고
panic code 27을 반복한다.

- 2026-09-20: Raspberry Pi 5 실기에서 M6m 검증 완료.
- 물리적으로 수신한 `A` make/break가 `EVENT64_KEYBOARD` type과 `0x1e/0x9e`
  data로 FIFO에 들어갔고 같은 순서로 소비된 뒤 queue가 비는 것을 확인했다.
- 이후에도 `M` 출력이 계속됐다.

### M6n — HID report 상태 차이와 modifier 순서

M6n은 이전 8-byte boot report 상태를 보존하고 새 report와 비교해 modifier와 최대
6-key rollover의 press/release만 FIFO event로 만든다. modifier press는 일반 key
press보다 먼저, 일반 key release는 modifier release보다 먼저 전달해 기존 console의
modifier 상태 추적 순서를 보장한다.

안내가 나오면 왼쪽 Shift를 누른 채 `A`를 누른 뒤 둘 다 뗀다. 성공 기준은 FIFO에
`0x2a, 0x1e, 0x9e, 0xaa`가 순서대로 들어가는 것이다.

```text
M6n: press and release Left Shift+A within 10 seconds
M6n: HID report diff Shift+A make/break OK
MTASK: MMMMM...
```

물리 report의 modifier/usage가 다르거나 report-diff/FIFO 순서가 틀리면 진단값을
표시하고 panic code 28을 반복한다.

- 2026-09-20: Raspberry Pi 5 실기에서 M6n 검증 완료.
- 왼쪽 Shift+A의 물리 report에서 `0x2a, 0x1e, 0x9e, 0xaa` 순서의
  `EVENT64_KEYBOARD` make/break가 생성됐고 이후에도 `M` 출력이 계속됐다.

### M6o — 상시 non-blocking interrupt-IN producer

M6o는 interrupt-IN Normal TRB 하나를 항상 endpoint에 대기시킨다. 메인 heartbeat
loop는 event cycle bit만 non-blocking으로 검사하며, 완료 report를 공용 FIFO로
변환한 직후 다음 TRB를 재등록한다. 따라서 NAK 중인 키보드를 기다리느라 scheduler나
ACT LED heartbeat를 멈추지 않는다.

M6n까지 완료된 뒤 `MTASK:`의 `M`이 증가하는 동안 `C`를 누른다. `C`의 KEY64 make
code `0x2e`가 live FIFO에서 나오면 다음 문구를 한 번 표시하고 heartbeat를 계속한다.

```text
M6o: press C while MTASK is running
MTASK: MMMM...
M6o: live USB FIFO C event OK
MTASK: MMMM...
```

TRB 등록, event 검증, report 변환 또는 재등록이 실패하면 panic code 29를 반복한다.

- 2026-09-20: Raspberry Pi 5 실기에서 M6o 검증 완료.
- `MTASK:` 뒤에 `M`이 네 번 이상 증가한 상태에서 `C`를 눌러 live FIFO make event
  수신을 확인했고, 이후에도 `M` 출력이 계속됐다.
- 초기 출력은 성공 문구를 별도 줄에 쓴 뒤 `MTASK:` 레이블을 다시 표시했기 때문에
  성공 뒤의 `M`이 1개부터 다시 시작한 것처럼 보였다. scheduler나 heartbeat
  counter가 reset된 것은 아니다. 성공 표식을 기존 `MTASK:` 줄 안에 삽입해 앞뒤의
  `M`이 한 흐름으로 보이도록 수정했다.

### M6p — live release와 연속 TRB 재등록

M6p는 M6o에서 `C` make를 받은 직후 재등록한 interrupt-IN TRB가 `C` release report를
받는지 확인한다. report-diff가 KEY64 break `0xae`를 FIFO에 넣어야 성공이다. 성공
표식은 `MTASK:` 줄 안에 출력하므로 scheduler 진행이 초기화된 것처럼 보이지 않는다.

```text
M6o/p: press and release C while MTASK is running
MTASK: MMMM [M6o: C make OK] [M6p: C break + rearm OK] MMMM...
```

별도의 추가 키는 필요 없다. `C`를 눌렀다 떼기만 하면 된다.

- 2026-09-20: Raspberry Pi 5 실기에서 M6p 검증 완료.
- 화면에 `MMMM [M6o: C make OK] M[M6p: C break + rearm OK] MMMM...`가
  표시됐다. make와 release 사이 및 release 이후에도 `M`이 증가해 상시 input
  polling이 scheduler/heartbeat를 막지 않음을 확인했다.

### M6q — 공용 keyboard modifier consumer 연결

M6q는 x86 PS/2 backend와 같은 `keyboard64_track_modifier()`, `keyboard64_shift()`,
`keyboard64_ctrl()`, `keyboard64_alt()` API를 AArch64 backend에 제공한다. M6n에서
만든 Shift+A FIFO 네 이벤트를 실제 공용 modifier consumer에 순서대로 전달해 Shift
make 직후 상태 1, A make/break 동안 상태 1, Shift break 직후 상태 0을 확인한다.
상시 M6o/p FIFO도 같은 consumer를 거치므로 이후 console 연결 시 modifier 상태를
별도로 변환하지 않는다.

정상 출력은 화면 절약을 위해 M6n과 한 줄로 합친다.

```text
M6n/q: HID report diff + shared modifier state OK
```

실기 절차는 M6n과 동일하다. 왼쪽 Shift+A를 눌렀다 놓고, 이후 M6o/p에서 `C`를
눌렀다 놓는다.

- 2026-09-20: Raspberry Pi 5 실기에서 M6q 검증 완료.
- M6n/q를 통과한 뒤 `MMMM [M6o: C make OK] MM[M6p: C break + rearm OK]
  MMMM...`가 계속 표시됐다. 이는 공용 modifier consumer 검증과 상시 입력 경로가
  함께 정상 동작했음을 뜻한다.

### M6r — 공용 console keymap 연결

M6r은 `console64.c`의 영문/Shift/한글-mode Set-1 keymap을 `lib/keymap64.c`로
분리한다. x86_64 console의 기존 변환 경로도 이 함수를 사용하고, AArch64 live FIFO
consumer 역시 같은 `keymap64_translate()`를 호출한다. `C` make `0x2e`가 modifier
상태 0에서 문자 `c`로 변환돼야 M6o 성공 표식을 표시한다.

화면 공간을 아끼기 위해 M6o와 한 표식으로 합친다.

```text
MTASK: MMMM [M6o/r: C make + keymap OK] M[M6p: C break + rearm OK] MMMM...
```

실기 절차는 M6o/p와 동일하게 `C`를 눌렀다 놓는 것이다.

- 2026-09-20: Raspberry Pi 5 실기에서 M6r 검증 완료.
- 화면에 `MMMM [M6o/r: C make OK + keymap OK] M[M6p: C break + rearm
  OK] MMMM...`가 표시되고 이후에도 heartbeat가 계속됐다. USB usage부터 공용
  KEY64와 공용 console keymap을 거쳐 문자 `c`까지 이어지는 경로를 확인했다.

### M6s — live 두벌식 자모와 공용 한글 composer

M6s는 상시 USB FIFO에서 `R`, `K` make를 받아 공용 keymap으로 소문자 `r`, `k`로
변환한다. `hangul64_key_to_cho('r')`의 초성 ㄱ과
`hangul64_key_to_jung('k')`의 중성 ㅏ를 공용 `HANGUL64` 상태에 넣고,
`hangul64_compose_utf8()` 결과가 `가(U+AC00)`인지 확인한다.

M6o/p의 `C`를 눌렀다 놓은 뒤 `R`, `K`를 차례로 눌렀다 놓는다. 성공 표식은 기존
`MTASK:` 흐름 안에 표시된다.

```text
M6o-s: press/release C, then R, then K during MTASK
MTASK: MMM [M6o/r: C make + keymap OK] M[M6p: C break + rearm OK] MM[M6s: live Hangul rk -> U+AC00 OK] MMM...
```

조합 결과가 U+AC00이 아니면 panic code 30을 반복한다.

- 2026-09-20: Raspberry Pi 5 실기에서 M6s 검증 완료.
- `C` release 뒤 사용자가 입력할 때까지 `M`이 계속 증가했고, `R`, `K` 입력 뒤
  `[M6s: live Hangul rk -> U+AC00 OK]`가 표시된 후에도 heartbeat가 지속됐다.
- M6p와 M6s 사이의 많은 `M`은 테스트 입력이 늦어진 동안 출력된 것으로, USB
  endpoint가 NAK 상태일 때도 scheduler가 막히지 않는다는 추가 확인 결과다.

### M6t — live 조합 UTF-8의 framebuffer 렌더링

M6t는 M6s가 공용 composer로 만든 UTF-8 세 바이트를 상수 문구로 대체하지 않고
그대로 `arch64_dbg_puts()`에 전달한다. SD 카드에서 읽은 H04.FNT renderer가 실제
`가` glyph를 표시하면 USB 입력부터 keymap, 두벌식 조합, UTF-8 decode와 framebuffer
한글 출력까지의 end-to-end 경로가 이어진 것이다.

출력 공간을 아끼기 위해 M6s와 같은 표식을 사용한다.

```text
[M6s/t: live Hangul rk -> 가 OK]
```

실기에서는 `가`가 깨진 바이트나 빈 칸이 아닌 정상 한글 모양인지 함께 확인한다.

- 2026-09-20: Raspberry Pi 5 실기에서 M6t 검증 완료.
- live 조합 buffer가 `[M6s/t: live Hangul rk -> 가 OK]`의 정상 `가` glyph로
  표시됐고 이후에도 `M` 출력이 지속됐다.

### M6u — 공용 순수 두벌식 상태 전이와 복수 음절

M6u는 console의 화면 상태와 무관하게 사용할 수 있는 `hangul64_feed()`를 추가한다.
입력마다 확정된 UTF-8 최대 두 글자, ASCII passthrough와 다음 preedit 상태를
반환하며, 단모음/겹모음, 단받침/겹받침, 받침의 다음 초성 이동 규칙을 처리한다.

M6s/t의 `가` 확인 뒤 Shift 없이 `G K S R M F`를 차례로 눌렀다 놓는다. 두벌식 문자열
`gksrmf`에서 첫 음절 `한`이 commit되고 마지막 preedit `글`을 합친 결과가
`한글`인지 검사한 뒤 실제 UTF-8 buffer를 렌더링한다.

```text
M6 test: A, Shift+A, C, R K, G K S R M F (no Shift)
MTASK: MMMMM...[M6a-u: xHCI + USB HID + FIFO + keymap + 한글 OK] MMMMM...
```

상태 전이, commit/preedit 결합 또는 UTF-8 결과가 다르면 panic code 31을 반복한다.

#### Troubleshooting: `gksrmf` 입력을 놓쳐 M6u가 진행되지 않음

- 증상: M6s/t까지 성공하고 `G K S R M F`를 입력해도 heartbeat `M`만 계속되며
  M6u 표식이 나타나지 않는다.
- 원인: live 입력 루프가 ACT LED 점멸을 위해 500 ms씩 두 번 blocking delay를
  실행했다. xHCI interrupt endpoint를 초당 한 번만 poll/rearm하므로 보통 속도의
  연속 입력에서 중간 HID report가 유실됐다. `R K`처럼 천천히 입력한 짧은 시험은
  우연히 통과할 수 있었다.
- 해결: generic counter deadline으로 500 ms LED 상태를 비차단 전환하고, 메인
  루프가 쉬지 않고 xHCI completion을 poll/rearm하도록 변경했다. scheduler heartbeat
  호출 빈도와 LED 점멸 주기는 기존과 동일하게 유지한다.
- 재검증: 안내 순서대로 입력하고 마지막 `G K S R M F`는 Shift 없이 보통
  속도로 입력하여 통합 M6a-u 성공 표식과 이후의 `M` 출력을 확인한다.

- 2026-09-20: Raspberry Pi 5 실기에서 M6u 검증 완료.
- M6s/t 뒤 `G K S R M F`를 연속 입력했을 때
  `[M6u: full automaton -> 한글 OK]`가 표시되고 이후에도 `M` heartbeat가
  지속됐다.
- 이 결과로 비차단 ACT LED 전환 중의 연속 xHCI poll/rearm, HID report 보존,
  공용 keymap과 `hangul64_feed()`의 복수 음절 commit/preedit 결합 및 실제 UTF-8
  framebuffer 출력 경로를 확인했다.

### M6v — 앱 raw TTY 한글 입력과 공용 오토마타 연결

M6v는 나노 같은 앱이 사용하는 `console64` raw TTY 경로의 별도 두벌식 상태 전이를
제거하고, M6u에서 실기로 검증한 `hangul64_feed()`를 직접 사용한다. 확정된 글자는
`TTY_KIND_CHAR`, 현재 조합 글자는 `TTY_KIND_PREEDIT`으로 기존 ABI 그대로 전달한다.
따라서 x86_64 앱 동작과 ABI를 바꾸지 않으면서 AArch64 USB 입력에서 검증한 상태기를
실제 앱 입력 경로와 공유한다.

이 단계는 장치 제어나 AArch64 실행 경로를 변경하지 않는다. x86_64 QEMU 부팅 시
실행되는 `console64_hangul_smoke()`가 raw TTY 경로를 거쳐 있/닦/밖/겪/앉/없 및
받침 불가 쌍자음 회귀를 검사하므로 별도의 Raspberry Pi 물리 재검증은 필요하지
않다.

- 2026-09-20: `make x86_64`와 `make aarch64` 빌드를 통과했다.
- x86_64 QEMU 부팅에서 `hangul64 smoke=ok`와 정상 콘솔 prompt를 확인했다.
- 이 결과로 앱 raw TTY의 CHAR/PREEDIT ABI를 유지한 공용 오토마타 전환을
  검증했다.

## M7 — 32bpp GUI와 console 통합

M6은 M6a–M6v의 xHCI, USB boot keyboard, HID/FIFO, keymap 및 한글 입력 경로로
종료한다. 여기서부터 작업 성격이 USB transport에서 공용 graphics/console stack으로
바뀌므로 기존 M6w–M6ae를 다음과 같이 M7a–M7i로 재분류한다.

| 새 번호 | 기존 번호 | 내용 |
|---|---|---|
| M7a–M7e | M6w–M6aa | 32bpp sheet, palette, window, 전체 GUI stack |
| M7f–M7i | M6ab–M6ae | console 초기화, 줄 편집, FIFO, 별도 입력 태스크 |

아래 실기 기록 안의 기존 번호와 당시 화면 문자열은 실제로 검증한 이미지를 정확히
남기기 위해 보존한다. 재분류 이후 새 이미지와 후속 작업은 M7 번호만 사용한다.

### M7a (기존 M6w) — 8bpp sheet와 Pi 32bpp framebuffer 경계

기존 `sheet64`의 각 시트는 8-bit palette index를 저장하지만 Raspberry Pi 5
mailbox framebuffer는 32bpp RGB다. M6w는 `SHTCTL64`에 출력 bpp를 추가하고,
8bpp x86_64 경로는 그대로 복사하면서 32bpp에서는 기존 16색 및 6x6x6 palette
index를 RGB 픽셀로 변환한다. VRAM stride는 바이트 단위로 유지한다.

M6a-u 입력 검사가 끝나면 작은 가상 32bpp VRAM에 빨강/초록/파랑/흰색과 palette
색을 sheet로 합성한다. 변환된 픽셀 값과 각 행의 padding이 보존됐는지 검사한다.
성공 출력은 M6a-u와 같은 줄에 이어 붙인다.

```text
[M6a-u: xHCI + USB HID + FIFO + keymap + 한글 OK] [M6w: 32bpp sheet compositor OK]
```

색 변환, 32bpp 주소 계산 또는 stride padding 보존에 실패하면 원인 코드와 함께
panic code 32를 반복한다. 이 단계가 통과하면 다음 단계에서 실제 Pi framebuffer에
공용 GUI/console sheet를 올릴 수 있다.

- 2026-09-20: Raspberry Pi 5 실기에서 M6w 검증 완료.
- 통합 M6a-u 표식 뒤 `[M6w: 32bpp sheet compositor OK]`가 표시되고 이후에도
  `M` heartbeat가 지속됐다.

### M7b (기존 M6x) — 실제 Pi framebuffer sheet 출력

M6x는 M6w의 변환기를 실제 mailbox framebuffer 일부에 연결한다. 화면 왼쪽 아래에
32x32 크기의 빨강, 초록, 파랑, 흰색 블록 네 개를 연속으로 합성하며, 각 변경 행을
`dc cvac`로 정리한 뒤 `dsb sy`로 display가 볼 수 있게 한다. 첫 행의 실제 VRAM
픽셀도 다시 읽어 네 색의 RGB 값을 확인한다.

M6w와 별도 입력은 필요 없으며 성공 표식은 한 줄로 합친다.

```text
[M6a-u: xHCI + USB HID + FIFO + keymap + 한글 OK] [M6w/x: 32bpp live sheet OK]
```

실기에서는 위 문구, 왼쪽 아래의 빨강·초록·파랑·흰색 띠, 이후 계속되는 `M`을
함께 확인한다. 초기화나 실제 VRAM readback이 실패하면 상태값과 panic code 33을
표시한다.

- 2026-09-20: Raspberry Pi 5 실기에서 M6x 검증 완료.
- 왼쪽 아래에 빨강·초록·파랑·흰색 블록이 순서대로 표시됐고,
  `[M6w/x: 32bpp live sheet OK]` 및 이후의 `M` heartbeat를 확인했다.

### M7c (기존 M6y) — 공용 software palette 연결

M6y는 sheet에 중복돼 있던 고정 RGB 변환표를 `graphic64`의 공용 palette 상태로
옮긴다. x86_64에서는 같은 상태를 VGA DAC에도 기록하고, AArch64에서는 port I/O
없이 software palette만 갱신한다. 따라서 앱이 `palette64_install()`로 바꾼
16–231번 색도 Pi의 32bpp sheet 출력에 반영된다.

M6x의 네 색 블록 오른쪽에 palette index 16을 통해 보라색(0x80,0x20,0xc0) 블록을
하나 더 그리고 실제 VRAM 값을 확인한다. 별도 키 입력은 없다.

```text
[M6a-u: xHCI + USB HID + FIFO + keymap + 한글 OK] [M6w-y: 32bpp live sheet + palette OK]
```

실기에서는 왼쪽 아래에 빨강·초록·파랑·흰색·보라색 블록이 순서대로 보여야 한다.

- 2026-09-20: Raspberry Pi 5 실기에서 M6y 검증 완료.
- 왼쪽 아래의 다섯 번째 보라색 블록, `[M6w-y: 32bpp live sheet + palette OK]`
  표식과 이후의 `M` heartbeat를 확인했다.

### M7d (기존 M6z) — 공용 window renderer의 실제 Pi sheet

M6z는 `window64`가 `console64` 전역에서 글꼴을 가져오던 의존성을 명시적인
`window64_set_hangul_font()`로 분리한다. 따라서 아직 전체 console을 올리기 전에도
같은 window renderer를 AArch64에서 사용할 수 있다.

M6x/y가 끝나면 실제 framebuffer 오른쪽 아래에 320x96 크기의 활성 창을 sheet로
합성한다. 창 제목은 SD 카드에서 읽은 H04.FNT로 그린 `머꼬 M6z`이며, 테두리의
회색·흰색·검정 픽셀을 실제 VRAM에서 다시 확인한다.

```text
[M6a-u: xHCI + USB HID + FIFO + keymap + 한글 OK] [M6w-z: 32bpp sheet + palette + window OK]
```

실기에서는 왼쪽 아래의 다섯 색 블록, 오른쪽 아래의 `머꼬 M6z` 창, 성공 표식과
계속되는 `M`을 확인한다. 실패하면 상태값과 panic code 34를 표시한다.

- 2026-09-20: Raspberry Pi 5 실기에서 M6z 검증 완료.
- 오른쪽 아래에 회색 배경, 파란 제목 표시줄, 닫기 버튼과 정상 한글 제목
  `머꼬 M6z`가 있는 창이 표시됐다. 통합 성공 표식과 이후의 heartbeat도
  지속됐다.

### M7e (기존 M6aa) — 전체 공용 GUI sheet stack

M6aa는 function/data section GC를 사용해 아직 포팅하지 않은 console event/command
함수와 분리된 `gui64_init()` 경로를 AArch64 이미지에 연결한다. 실제 화면 크기의
8bpp 배경과 콘솔 버퍼, sheet map 및 최상단 마우스 커서를 만들고 32bpp Pi
framebuffer로 합성한다. 새 콘솔 버퍼는 화면에 올리기 전에 검정색으로 초기화한다.

M6z의 시험용 색 띠와 작은 창은 이 단계에서 전체 화면 GUI 콘솔 sheet로 교체된다.
왼쪽 위에는 공용 `putstr64()`로 다음 문구를 그리고 실제 framebuffer의 흰색 glyph
픽셀을 다시 읽어 확인한다.

```text
머꼬 M6aa GUI console sheet
[M6a-u: xHCI + USB HID + FIFO + keymap + 한글 OK] [M6w-aa: 32bpp GUI sheet stack OK]
```

실기에서는 검정 전체 화면, 왼쪽 위 문구, 화면 중앙의 마우스 커서, 성공 표식과
이후의 `M`을 확인한다. 실패하면 상태값과 panic code 35를 표시한다.

- 2026-09-20: Raspberry Pi 5 실기에서 M6aa 검증 완료.
- 검정 전체 화면으로 전환된 뒤 상단의 `머꼬 M6aa GUI console sheet`, 중앙의
  통합 성공 표식과 이후의 `M` heartbeat를 확인했다. 성공 표식의 별도 색 배경은
  GUI sheet 위에 기존 early-debug renderer가 직접 그리는 진단 셀 배경이다.

### M7f (기존 M6ab) — 실제 `console64` 초기 화면

M6ab는 M6aa가 만든 기존 GUI console sheet를 재사용해 `console64` 상태를
초기화한다. 중복 `gui64_init()` 없이 `console64_init_on_sheet()`로 sheet를 붙이고,
SD 카드 H04.FNT와 공용 palette/renderer를 사용하는 실제 콘솔 시작 문구와
프롬프트를 그린다. AArch64에서는 x86 COM1 port I/O를 수행하지 않는다.

```text
머꼬 OS AArch64 콘솔
한글 입력이 기본입니다. Shift+Space로 영어 입력으로 전환합니다.
>
[M6a-u: xHCI + USB HID + FIFO + keymap + 한글 OK] [M6w-ab: GUI sheet + console64 init OK]
```

실기에서는 M6aa 시험 문구가 위 실제 콘솔 초기 화면으로 교체되고, 중앙 마우스
커서와 이후의 `M`이 유지되는지 확인한다. 실패하면 panic code 36을 표시한다.

- 2026-09-20: Raspberry Pi 5 실기에서 M6ab 검증 완료.
- 왼쪽 위에 실제 AArch64 콘솔 시작 문구와 프롬프트가 표시되고 중앙 커서 및
  heartbeat가 유지됐다. 중앙의 이전 M6 성공 표식과 색 배경은 timer IRQ의
  early-debug 직접 출력이 console sheet를 우회해서 남은 것으로 확인했다.

### M7g (기존 M6ac) — USB 키보드에서 실제 `console64` 줄 편집기로

M6ac는 M6ab 이후의 live USB FIFO 이벤트를 `console64_process_input_key()`로
전달한다. 이 함수는 실제 console의 modifier, keymap, 두벌식 조합, preedit 갱신,
backspace 및 Enter 확정 경로를 사용하되 아직 AArch64 명령 실행기는 호출하지 않는다.

GUI console이 활성화되는 순간 timer IRQ의 early-debug `M` framebuffer 직접 출력을
중지한다. 따라서 중앙에 M6 표식이나 색 배경이 새로 남지 않고, scheduler liveness는
기존 0.5초 ACT LED 점멸로 확인한다.

초기화 후 다음 안내에서 Shift 없이 `G K S R M F`를 입력하고 Enter를 누른다.

```text
M6a-ab: USB HID + GUI + console64 init OK
M6ac test: type G K S R M F without Shift
> 한글
>
```

`한글`이 실제 검정 console sheet 위에 조합돼 표시되고 Enter 뒤 새 프롬프트가
나오며, ACT LED가 계속 점멸하면 성공이다.

- 2026-09-20: Raspberry Pi 5 실기에서 M6ac 검증 완료.
- USB 키보드의 `G K S R M F`가 실제 console sheet에서 `한글`로 조합됐고,
  Enter 뒤 새 프롬프트가 표시됐다. 중앙 마우스 커서, early-debug 출력 중단 및
  0.5초 ACT LED heartbeat도 모두 정상임을 확인했다.

### M7h (기존 M6ad) — 실제 console key FIFO producer/consumer

M6ad는 live USB consumer가 `console64_process_input_key()`를 직접 호출하지 않고
`console64` 인스턴스의 64-entry key FIFO에 `EVENT64_KEYBOARD`를 넣도록 변경한다.
메인 루프의 consumer가 큐를 비우며 같은 줄 편집기를 호출한다. 이후 console task를
가동할 때 producer 쪽을 바꾸지 않고 FIFO의 wakeup task만 연결할 수 있다.

FIFO뿐 아니라 조합 중 backspace도 함께 검증한다. Shift 없이 아래 순서대로 입력한다.

```text
G K S R M X, Backspace, F, Enter
```

중간의 `X`는 `한긑`을 만들고 Backspace가 마지막 받침을 지운 뒤 `F`가 ㄹ 받침을
넣어 최종 결과를 `한글`로 복구해야 한다.

```text
M6ad test: G K S R M X, Backspace, F, Enter
> 한글
>
```

FIFO 초기화, enqueue 또는 drain이 실패하면 panic code 37을 표시한다. 성공 뒤에도
ACT LED heartbeat가 지속돼야 한다.

- 2026-09-20: Raspberry Pi 5 실기에서 M6ad 검증 완료.
- `G K S R M X, Backspace, F, Enter`가 console key FIFO를 거쳐 전달됐고,
  조합 중 마지막 받침을 지운 뒤 `한글`로 복구됐다. Enter 뒤 새 프롬프트,
  direct `M` 출력 없음 및 0.5초 ACT LED heartbeat도 모두 정상임을 확인했다.

### M7i (기존 M6ae) — 별도 console 입력 태스크와 FIFO wakeup

M7i는 M7h의 FIFO consumer를 USB polling 메인 루프에서 분리해 scheduler가 실행하는
전용 console 입력 태스크로 옮긴다. 태스크는 key FIFO가 비면 sleep하고, 메인 루프가
USB HID 이벤트를 enqueue하면 FIFO에 연결된 태스크가 깨어나 줄 편집 입력을 처리한다.
메인 루프는 더 이상 `console64_drain_input()`을 호출하지 않는다.

콘솔이 표시되면 다음 순서로 입력한다.

```text
Shift+Space, H E L L O, Enter
```

첫 `Shift+Space`는 기본 한글 입력을 영어 입력으로 바꾼다. 이후 키 이름의 대문자 표기는
물리 키를 뜻하므로 Shift를 누르지 않고 입력한다.

```text
M7i test: Shift+Space, H E L L O, Enter
> hello
>
```

`hello`와 새 프롬프트가 표시되고 ACT LED가 0.5초 간격으로 계속 점멸하면 producer인
USB 메인 루프와 consumer인 console 태스크의 enqueue, wakeup, sleep이 모두 성공한
것이다. 태스크 시작이 실패하면 console 초기화 panic 경로를, enqueue가 실패하면
panic code 38을 표시한다.

- 2026-09-20: Raspberry Pi 5 실기에서 M7i(당시 M6ae) 검증 완료.
- `Shift+Space` 뒤 `H E L L O, Enter` 입력이 별도 console 태스크에서 처리되어
  `hello`와 새 프롬프트가 표시됐다. FIFO wakeup/sleep과 0.5초 ACT LED heartbeat도
  모두 정상임을 확인했다.

### M7j — 실제 console command dispatcher와 FAT32 `ls`

M7j는 M7i의 입력 전용 consumer를 정상 `console64` 태스크로 교체한다. Enter가 줄을
확정하기만 하던 bring-up 경로 대신 `console64_process_key()`와 실제 command
dispatcher를 호출한다. 아직 AArch64 user ABI를 연결하지 않았으므로 앱 실행과
MicroPython 명령은 후속 M7 단계로 두고, `help`, `clear`, `mem`, `tasks`, `ls`,
`목록`, `type readme.txt`, `xwindow`, `창` 내장 명령을 우선 제공한다.

콘솔이 표시되면 다음 순서로 입력한다.

```text
Shift+Space, L S, Enter
```

`Shift+Space`로 영어 모드로 바꾼 뒤 `ls`를 Shift 없이 입력한다. SD 카드 FAT32의
파일 이름과 크기가 출력되고 새 프롬프트가 표시되며 ACT LED heartbeat가 계속되면
별도 console 태스크의 command dispatch와 storage read가 모두 성공한 것이다.
태스크 시작 또는 FIFO enqueue 실패는 기존 panic code 38 경로로 표시한다.

- 2026-09-20: Raspberry Pi 5 실기에서 M7j 검증 완료.
- 영어 모드에서 `ls`를 실행했을 때 FAT32 파일 이름과 크기 및 새 프롬프트가
  표시됐고, 별도 console 태스크와 0.5초 ACT LED heartbeat도 계속 정상 동작했다.

### M7k — AArch64 ELF64 실행과 기본 syscall

M7k는 공용 ELF/process/syscall 계층을 AArch64에 연결한다. 앱 이미지는 TTBR0의
EL0 전용 identity mapping에, 커널은 기존 TTBR1 high-half mapping에 둔다. AArch64
syscall ABI는 번호를 `x8`, 인수를 `x0`–`x5`, 반환값을 `x0`에 두고 `svc #0`을
사용한다. 첫 검증은 `SYS_WRITE`와 `SYS_EXIT`만 사용하는 `hello.elf`다.

`make aarch64` 뒤 `build64/aarch64-app/hello.elf`를 FAT32 부트 파티션에
`HELLO.ELF`라는 이름으로 복사한다. 새 kernel image로 부팅해 영어 입력 모드에서
다음을 실행한다.

```text
run HELLO.ELF
```

정상 결과는 앱이 EL0에서 문자열을 출력하고 status 0으로 console task에 복귀한
뒤 새 프롬프트를 표시하는 것이다. 그 뒤에도 ACT LED heartbeat가 계속돼야 한다.

```text
hello from app64
exit 0
>
```

ELF machine은 `EM_AARCH64(183)`만 허용한다. 사용자 image, stack, heap은 syscall
진입 때 범위를 검사하며 앱 종료 뒤 TTBR0 mapping과 backing memory를 회수한다.

- 2026-09-24: Raspberry Pi 5 실기에서 M7k 검증 완료.
- `run hello.elf` 실행 뒤 `hello from app64`, `exit 0`과 새 프롬프트가 표시됐다.
  AArch64 ELF 적재, EL0 진입, `SYS_WRITE`, `SYS_EXIT` 및 console task 복귀가 모두
  정상 동작함을 확인했다.

### 부팅 입력 회귀 검사 축약

M7k까지 실기 검증이 끝난 뒤에는 과거 M6 bring-up 단계의 `A`, `Shift+A`, `C`,
`R K`, `G K S R M F` 연속 입력을 매 부팅마다 반복하지 않는다. 현재 부팅 검사는
문자 키 하나를 눌렀다 놓아 interrupt report, KEY64 make/break 변환과 event FIFO를
확인한 뒤 바로 GUI와 console task를 시작한다. 이후 키 입력은 처음부터 console
FIFO로 전달한다.

interrupt TRB를 안내 문구보다 먼저 등록하고 timeout 없이 press/release를 기다린다.
따라서 안내가 보이자마자 키를 빠르게 눌렀다 떼어도 report를 놓치지 않는다. 과거
10초 blocking read에서 이벤트가 모두 0인 채 `M6k status=-4`가 되던 경로는 제거했다.

- 2026-09-24: Raspberry Pi 5 실기에서 수정된 단일 문자 키 검증 완료.
- 키 press/release 뒤 M6k timeout 없이 console task가 시작되는 것을 확인했다.

콘솔 초기화가 이미 제목, 한글 입력 안내와 프롬프트를 출력하므로 별도의
`M7j test: Shift+Space, L S, Enter` 안내 문구와 중복 프롬프트도 제거했다.
과거 스케줄러 bring-up용 `MTASK:` 화면 문구도 제거했다. 스케줄러 상태 갱신과
0.5초 ACT LED heartbeat는 출력 없이 계속 동작한다.

### M7l — AArch64 파일 syscall과 `cat.elf`

M7l은 M7k의 기본 앱 실행 경로를 `SYS_OPEN`, `SYS_READ`, `SYS_CLOSE`까지 넓힌다.
공용 `cat` 앱을 AArch64 ELF로 함께 빌드하며, 인수 전달과 앱 스택의 읽기 버퍼도
EL0 mapping 및 syscall 사용자 범위 검사를 통과해야 한다.

`make aarch64` 뒤 `build64/aarch64-app/cat.elf`를 FAT32 부트 파티션에
`CAT.ELF`라는 이름으로 복사한다. 같은 파티션에 읽을 파일이 있는지 `ls`로 확인한
뒤 다음과 같이 실행한다.

```text
run CAT.ELF README.TXT
```

파일 내용 뒤에 `exit 0`과 새 프롬프트가 표시되면 인수 전달과 open/read/close
syscall 경로가 정상이다. 파일 이름은 `ls`에 실제 표시된 이름을 사용한다.

- 2026-09-24: Raspberry Pi 5 실기에서 M7l 검증 완료.
- `cat.elf`가 지정한 파일을 출력한 뒤 `exit 0`으로 console task에 복귀했다.
  AArch64 인수 전달과 `SYS_OPEN`, `SYS_READ`, `SYS_CLOSE`가 정상 동작함을 확인했다.

### M7m — AArch64 raw TTY와 `ktest.elf`

M7m은 공용 `ktest` 앱을 AArch64 ELF로 빌드해 `SYS_TTY` 전체 경로를 검증한다.
앱은 raw mode로 전환한 뒤 화면 크기 조회, 셀 지우기, 커서 이동, 전경·배경색 변경,
키/PREEDIT 이벤트 대기 및 dirty 영역 flush를 사용한다. 특히 `tty_clear()`는 x0부터
x4까지 사용하는 5인자 syscall이므로 AArch64 SVC 인수 전달도 함께 확인한다.

`build64/aarch64-app/ktest.elf`를 FAT32 부트 파티션에 `KTEST.ELF`로 복사한 뒤
다음을 실행한다.

```text
run KTEST.ELF
```

반전색 제목과 `^X quit` 상태 줄이 나타나면 영문 키, 화살표 키, 한글 조합을 각각
입력해 마지막 이벤트와 조합 중 글자가 갱신되는지 확인한다. `Ctrl+X`로 종료했을 때
화면이 기본 console로 복구되고 `exit 0`과 새 프롬프트가 표시되면 성공이다. 앱은
일부러 raw mode를 끄지 않으므로 종료 시 커널의 강제 복구 경로까지 검증한다.

- 2026-09-24: Raspberry Pi 5 실기에서 M7m 검증 완료.
- `ktest.elf`에서 raw TTY 화면과 키 이벤트를 확인하고 `Ctrl+X`로 종료한 뒤
  `exit 0`과 기본 console 프롬프트가 정상 복구됐다.

### AArch64 앱 직접 실행

x86_64 머꼬OS와 마찬가지로 내장 명령에 해당하지 않는 첫 단어를 앱 이름으로
간주한다. 따라서 `run` 접두사는 선택 사항이며 다음처럼 실행할 수 있다.

```text
hello
cat README.TXT
ktest
```

FAT 파티션에 앱이 확장자 없는 `HELLO`, `CAT`, `KTEST`로 들어 있으면 그 이름을
먼저 찾는다. 파일이 `HELLO.ELF`, `CAT.ELF`, `KTEST.ELF` 형태라면 명령 이름에 점이
없을 때 `.elf`를 자동으로 붙여 다시 찾는다. 명시적인 `run CAT.ELF README.TXT`
형식도 호환성을 위해 계속 지원한다.

### M7n — AArch64 사용자 heap과 `mtest`

M7n은 공용 `mtest`와 `crt/malloc.c`를 AArch64 ELF로 빌드한다. 커널의 1 MiB EL0
heap mapping과 `SYS_ALLOC`으로 4 KiB chunk를 받고, 앱 내부 free-list가 해제된
메모리를 재사용하는지 검사한다. 기본 할당, 4 MiB churn, 인접 블록 병합, 큰 블록
분할 및 교차 할당 뒤 데이터 보존을 순서대로 수행한다.

`build64/aarch64-app/mtest.elf`를 FAT32 부트 파티션에 `MTEST.ELF`로 복사하고
직접 실행한다.

```text
mtest
```

각 항목이 `ok`로 표시되고 마지막에 다음 결과와 `exit 0`이 나오면 성공이다.

```text
mtest: all passed
exit 0
```

- 2026-09-24: Raspberry Pi 5 실기에서 M7n 검증 완료.
- 모든 allocator 항목이 `ok`였고 `mtest: all passed`, `exit 0`을 확인했다.
  EL0 heap mapping, `SYS_ALLOC`과 앱 free-list의 재사용·분할·병합이 정상 동작했다.

### M7o — AArch64 FAT32 쓰기와 `wtest`

M7o는 공용 `wtest`를 AArch64 ELF로 빌드해 앱 syscall을 통한 FAT32 쓰기를
검증한다. `SYS_OPEN`의 `O_CREAT | O_TRUNC`, `SYS_WRITE`, `SYS_READ`, `SYS_CLOSE`를
사용하며 작은 파일과 1,500바이트 다중 클러스터 파일을 생성하고 즉시 다시 읽어
내용을 비교한다. `fd64_write()`가 데이터, FAT, 디렉터리 순으로 `fd64_sync()`까지
수행하므로 재부팅 뒤에도 파일이 남아야 한다.

`build64/aarch64-app/wtest.elf`를 FAT32 부트 파티션에 `WTEST.ELF`로 복사하고
다음처럼 실행한다.

```text
wtest
```

정상 결과는 다음과 같다.

```text
wtest: ok
exit 0
```

그 뒤 재부팅해서 `cat TEST.TXT`가 `phase0 fat12 write ok`를 출력하는지 확인한다.
1.5 MiB 할당과 전체 readback은 시간이 오래 걸리는 선택 시험으로 분리한다.

```text
wtest HUGE
```

- 2026-09-24: Raspberry Pi 5 실기에서 M7o 검증 완료.
- `wtest: ok`, `exit 0`을 확인한 뒤 재부팅했고, `cat TEST.TXT`가
  `phase0 fat12 write ok`를 출력했다. FAT32 데이터와 메타데이터의 매체 영속성이
  정상 동작함을 확인했다.

### M7p — AArch64 `나노` 편집기

M7p는 기존 공용 `나노` 소스를 AArch64 ELF로 빌드한다. 파일 읽기와 인수 전달,
EL0 heap allocator, raw TTY 화면, 이동 키, 한글 CHAR/PREEDIT, 파일 truncate·쓰기와
종료 시 console 복구까지 지금까지 연결한 M7 기능을 실제 응용 프로그램에서 함께
검증한다.

`build64/aarch64-app/나노.elf`를 FAT32 부트 파티션에 `나노.ELF`로 복사하고,
보존해도 되는 시험 파일을 연다.

```text
나노 NANO64.TXT
```

영문과 한글을 입력하고 화살표·Home·End·Backspace가 동작하는지 확인한다.
`Ctrl+O`로 저장했을 때 `저장했습니다`가 표시되어야 하며, `Ctrl+X`로 종료하면
기본 console과 `exit 0` 프롬프트로 복귀해야 한다. 재부팅한 뒤 다음 명령으로
저장 내용이 유지되는지 확인한다.

```text
cat NANO64.TXT
```

이 단계까지 통과하면 M7의 console·syscall·application parity를 완료하고 M8의
USB HID 마우스 및 GUI 상호작용으로 넘어간다.

- 2026-09-24: Raspberry Pi 5 실기에서 M7p 검증 완료.
- `나노 NANO64.TXT`로 영문·한글 편집, 이동 및 삭제, `Ctrl+O` 저장과 `Ctrl+X`
  종료를 확인했다. 재부팅 뒤 `cat NANO64.TXT`로 저장 내용이 유지되는 것도
  확인해 raw TTY, EL0 heap, 파일 syscall과 FAT32 영속성의 통합 경로가 통과했다.

### M7 완료 기록

Raspberry Pi 5 실기에서 `hello`, `cat`, `ktest`, `mtest`, `wtest`, `나노`까지
AArch64 ELF 앱 실행을 확인했다. 이 과정에서 EL0 진입과 SVC, 앱 인수, 표준 출력,
FAT32 읽기·쓰기, raw TTY, 한글 입력, 사용자 heap 및 앱 종료 후 console 복귀를
검증했다. 명령 디스패처는 x86_64와 같이 `run` 없이 앱 이름을 직접 실행하며,
필요하면 `.elf` 확장자를 자동 보완한다.

M7의 console·syscall·application parity는 완료 상태다. 사용자 요청에 따라 M8은
아직 착수하지 않는다.

### 화면 출력 축약

M6 진단이 늘어나면서 framebuffer 세로 공간이 부족해졌으므로, 이미 실기 검증이
끝난 M3/M4/M5의 정상 출력은 다음 한 줄씩으로 합쳤다. M6a부터 M6u까지도 모든
검사가 끝난 뒤 한 줄만 표시한다. 개별 단계의 실패 문구와 panic code는
troubleshooting을 위해 유지한다.

```text
M3: exceptions + scheduler + high-half paging OK
M4: SDHCI + FAT32 + write + Hangul font OK
M5: PCIe + RP1 BAR1 + UART loopback OK
M6a-u: xHCI + USB HID + FIFO + keymap + 한글 OK
```

- 2026-09-20: Raspberry Pi 5 실기에서 통합 M6a-u 검증 완료.
- 현재 부팅 회귀 검사는 문자 키 하나를 눌렀다 놓는 것으로 축약했으며, 이후
  입력은 곧바로 console task에 전달된다.

## M8 — USB HID 마우스와 GUI 상호작용

상태: 진행 중.

마우스 작업은 keyboard/console 경로와 별개인 M8로 분리한다. HID boot mouse의
enumeration과 report decoding부터 시작해 `gui64_mouse_dec()`가 소비하는 공용 이벤트
형식으로 연결하고, 커서 이동, 버튼, 창 focus/drag 및 닫기 버튼을 순서대로 검증한다.
M7의 console·syscall·application parity가 끝나기 전에는 M8 구현을 섞지 않는다.

### M8a — HID boot mouse report 디코더

M8a는 USB 전송과 GUI 연결에 앞서 boot mouse report 형식을 독립적으로 검증한다.
3바이트 기본 report의 버튼 및 signed X/Y를 `USBHID64_MOUSE_REPORT`로 변환하고,
4번째 바이트가 있으면 signed wheel 값으로 보존한다. HID Y축은 화면 좌표와 같이
아래쪽이 양수이므로 PS/2 디코더의 Y 반전은 적용하지 않는다.

부팅 자체 검사는 버튼 조합, 양수·음수 이동의 경계값, wheel과 짧은 report 거부를
확인한다. 성공 문구는 화면에 추가하지 않으며, 실패할 때만 `M8a` 오류와 panic
code 39를 표시한다. 다음 M8b에서 root port와 두 번째 slot을 일반화한 뒤 실제
mouse interrupt endpoint의 report를 이 디코더와 `gui64_mouse_event()`에 연결한다.

- 2026-09-24: Raspberry Pi 5 실기에서 M8a 검증 완료.
- 새 kernel로 기존 keyboard·console·앱 경로까지 정상 부팅해 mouse report 자체
  검사와 기존 기능 회귀 검사가 통과했다.

### M8b — RP1 xHCI root-port inventory

M8b는 두 번째 장치를 할당하기 전에 RP1의 xHCI0/xHCI1 각각에서 root-port 수,
`PORTSC.CCS` 연결 비트맵과 `PORTSC.PED` 활성 비트맵을 읽는다. 한 물리 장치가 어느
컨트롤러의 몇 번째 logical port에 나타나는지 실기에서 먼저 확정해야 기존 keyboard
slot을 보존하면서 mouse용 port reset과 두 번째 slot을 안전하게 추가할 수 있다.

키보드는 검증된 포트에 그대로 두고 USB 마우스를 연결해 부팅한다. 다음 형식의 한
줄을 기록한다.

```text
M8b: ports usb0 count/connected/enabled=.../.../... usb1=.../.../...
```

`connected`의 각 bit 0부터가 port 1부터에 대응한다. 현재 선택된 keyboard port는
뒤의 M6 reset/configure 과정에서 `enabled`가 켜지며, 아직 reset하지 않은 mouse
port는 `connected`만 켜져 있을 수 있다. 이 결과를 기준으로 M8c에서 controller와
port를 명시한 두 번째 장치 열거를 구현한다.

- 2026-09-24: 세로로 나란한 포트에 기존 keyboard와 mouse를 함께 연결하면 기존
  USB0 우선 선택이 mouse(`vid/pid=0x2510093a`, configuration length `0x22`)를 먼저
  열거해 boot-keyboard interface 탐색이 `status=-9`로 끝나는 것을 확인했다.
- 이 결과로 두 장치가 서로 다른 RP1 xHCI에 연결된 것을 확인했다. 다중 controller
  상태를 추가하기 전의 회귀 방지로, 두 controller에 모두 장치가 있으면 M6/M7에서
  검증한 keyboard 쪽 USB1을 우선하도록 바꿨다. USB0 mouse는 M8c에서 별도 상태와
  slot을 할당해 동시에 실행한다.

- 2026-09-24: `usb0 count/connected/enabled=3/1/0`, `usb1=3/1/0`을 실기에서
  확인했다. USB1 keyboard를 선택한 뒤 console 진입과 명령 실행도 정상 동작했다.

### M8c — controller별 xHCI 상태 분리

두 장치가 서로 다른 controller에 있으므로 DCBAA, command/event ring, scratchpad,
input/device context, EP0/interrupt ring과 report buffer를 USB0·USB1별로 분리한다.
기존 API는 선택된 controller의 상태만 다루며, `xhci64_select_controller()`와
`xhci64_start_controller()`로 대상을 명시할 수 있다. 먼저 USB1 keyboard만 사용하는
기존 부팅을 이 구조에서 회귀 검사한 뒤, USB0을 시작해 mouse slot을 추가한다.

- 2026-09-24: controller별 상태 분리 후에도 실기에서 M8b inventory가 종전과 같은
  `usb0=3/1/0`, `usb1=3/1/0`으로 출력되고, USB1 keyboard로 console 진입과 명령
  실행이 정상임을 확인했다.
- USB0 controller를 별도로 시작하고 port reset, Enable Slot, Address Device,
  boot-mouse interface 탐색, Configure Endpoint와 Set Protocol을 수행한다. 성공
  메시지는 표시하지 않고 오류가 발생할 때만 M8c 진단과 panic code를 남긴다.

두 controller의 interrupt transfer를 main loop에서 번갈아 polling한다. USB mouse는
endpoint의 최대 packet보다 짧은 3~4바이트 report가 일반적이므로 xHCI의 Short Packet
completion code와 residual length를 실제 report 길이로 변환한다. 디코딩한 X/Y와
button bit는 `gui64_mouse_event()`로 직접 보내 cursor 이동, focus, drag 및 close를
기존 GUI 경로에서 처리한다.

실기 검증의 성공 기준은 ready 줄의 표시 여부가 아니라 mouse cursor가 실제 움직임을
따르는지 여부다. 이어서 왼쪽 버튼 focus/drag와 같은 상태에서 USB1 keyboard의
console 명령 인식까지 확인한다. 초기화 실패 시
`stage/status`에서 1=start, 2=port reset, 3=slot, 4=address, 5=device descriptor,
6=boot-mouse descriptor, 7=endpoint configure, 8=Set Protocol을 구분한다.

- 2026-09-24: 실기에서 M8c ready 줄은 화면에 남지 않았지만 mouse 움직임에 맞춰
  cursor가 정상 이동했다. 이로써 USB0 mouse 열거, short interrupt report 처리,
  HID boot report 디코딩과 `gui64_mouse_event()` 연결까지 확인했다.

### M8d — 왼쪽 버튼 focus·drag·close

USB boot report의 button bit 0은 `gui64_mouse_event()`의 이전 버튼 상태와 비교한다.
0에서 1로 바뀌는 순간 cursor 아래 창을 올리고 keyboard focus를 옮기며, title bar를
누른 상태로 이동하면 해당 sheet를 끌고 간다. 닫기 버튼은 기존 GUI의 지연 종료
경로를 사용하므로 실행 중인 앱의 전역 자원을 강제로 남기지 않는다. 버튼을 놓으면
drag 상태를 해제한다. 이 경로는 M8c의 report polling에서 이미 연결되어 있으므로
별도의 성공 문구는 추가하지 않는다.

실기 시험은 다음 순서로 진행한다.

1. `xwindow`를 실행해 창 모드로 전환한다.
2. `new`를 실행해 두 번째 terminal을 만든다.
3. 뒤쪽 terminal의 title bar를 한 번 눌러 창과 taskbar의 활성 표시가 바뀌는지,
   이어서 입력한 키가 선택한 terminal에 들어가는지 확인한다.
4. title bar를 누른 채 이동해 창이 cursor를 따라가는지 확인한다.
5. 두 번째 terminal의 닫기 버튼을 눌러 창과 taskbar 항목이 함께 사라지는지 확인한다.
6. 남은 terminal에서 명령을 실행해 keyboard와 mouse 동시 polling의 회귀가 없는지
   확인한다.

- 첫 실기 시험에서 닫기 버튼을 누르면 `콘솔을 닫습니다.`까지 출력되지만 창은
  남는 현상을 확인했다. console task는 정상적으로 `close_ready`를 세우고 잠들었으나,
  AArch64 USB main loop에 x86_64 쪽의 `console64_reap_closed()` 호출이 빠져 있던 것이
  원인이었다. main loop에서 닫힌 console을 회수하도록 추가했다.
- 다중 창 시험에 필요한 `new`/`새창` 명령을 AArch64에서도 활성화했다. USB keyboard
  입력도 고정된 boot console 대신 `gui64_focused_console()`이 반환한 현재 활성 창으로
  보내며, 마지막 창을 닫은 상태에서는 desktop에 입력을 잘못 전달하지 않는다.
- 2026-09-24: Raspberry Pi 5 실기에서 `new`로 두 번째 terminal 생성, mouse 클릭에
  따른 focus 전환, title bar drag를 확인했다. 두 번째 terminal을 닫으면 창과 taskbar
  항목이 정상적으로 제거됐으며, 남은 첫 번째 terminal에서 keyboard 명령 입력과 실행도
  정상 동작했다. 이로써 M8d의 생성·focus·drag·close와 keyboard/mouse 동시 polling
  회귀 검증을 완료했다.
