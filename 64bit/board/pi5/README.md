# Raspberry Pi 5 boot files

M1은 아직 SD 카드 이미지를 만들지 않는다. FAT32 부트 파티션에 다음 두 파일을
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
