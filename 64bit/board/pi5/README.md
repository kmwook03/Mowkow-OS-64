# Raspberry Pi 5 boot files

아직 전용 SD 카드 이미지를 만들지 않는다. FAT32 부트 파티션에 다음 두 파일을
복사한다.

- `config.txt`: 이 디렉터리의 파일
- `kernel_2712.img`: `make aarch64`가 만든
  `build64/aarch64/kernel_2712.img`

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
