# Raspberry Pi 5 (AArch64)

[프로젝트 소개](../../README.md) · [공통 시스템 구조](../architecture.md) · [x86_64](x86_64.md)

Raspberry Pi 5 포트는 펌웨어가 `0x80000` 주소에 직접 적재하는 AArch64 flat image를 사용합니다. 현재 빌드는 SD 카드 전체에 쓰는
raw 이미지가 아니라, FAT32 부트 파티션에 복사할 파일 묶음을 만들기 때문에 `img64/mowkow-pi5.img`를 찾거나 `dd`로 기록하면 안 됩니다.

셸 명령은 저장소 루트에서 실행합니다. 커널 소스의 짧은 경로는
`64bit/src64/` 기준이며, 빌드·앱·도구 경로는 저장소 루트 기준입니다.

- [준비물](#준비물)
- [빌드](#빌드)
- [SD 카드 준비와 부팅](#sd-카드-준비와-부팅)
- [시스템 구조](#시스템-구조)
- [검증과 문제 해결](#검증과-문제-해결)

## 준비물

* Raspberry Pi 5, microSD 카드, HDMI 디스플레이
* 부팅 전에 연결한 USB 키보드, 마우스
* `aarch64-none-elf-gcc`, `aarch64-none-elf-ld`,
  `aarch64-none-elf-objcopy`
* `make`, `python3`
* 초기화된 `third_party/micropython` 서브모듈

처음 clone한 저장소라면 서브모듈부터 준비합니다.

```bash
git submodule update --init --recursive
```

## 빌드

저장소 루트에서 다음 명령을 실행합니다.

```bash
make aarch64
```

빌드가 끝나면 다음 두 산출물이 생깁니다.

* `build64/aarch64/kernel_2712.img`: Raspberry Pi 펌웨어가 적재하는 커널 이미지
* `build64/aarch64-boot/`: SD 카드 부트 파티션에 복사할 완성 파일 묶음

`aarch64-boot/`에는 `config.txt`, `kernel_2712.img`, 한글 글꼴, AArch64 앱, MicroPython 및 머꼬 스크립트가 함께 들어갑니다. staging 파일 묶음만 다시 동기화하려면 `make aarch64-stage`, AArch64 산출물을 지우려면 `make clean-a64`를 사용합니다.

기본 경로에서 크로스 툴체인을 찾지 못하면 도구 경로를 지정할 수 있습니다.

```bash
make aarch64 \
  A64_CC=/path/to/aarch64-none-elf-gcc \
  A64_LD=/path/to/aarch64-none-elf-ld \
  A64_OBJCOPY=/path/to/aarch64-none-elf-objcopy
```

## SD 카드 준비와 부팅

1. SD 카드에 MBR 파티션 테이블과 FAT32 LBA(type `0x0c`) 부트 파티션을 준비합니다. 데이터가 지워질 수 있으므로 장치 이름을 반드시 확인하고, 가능하면 비어 있는 전용 카드를 사용합니다.
2. FAT32 부트 파티션을 마운트한 뒤 `build64/aarch64-boot/` 안의 파일을 디렉터리 자체가 아니라 파티션 루트에 모두 복사합니다.

   ```bash
   cp -R build64/aarch64-boot/. /path/to/mounted-boot-partition/
   sync
   ```

3. 파티션을 안전하게 마운트 해제하고 SD 카드를 Raspberry Pi 5에 넣습니다.
4. HDMI와 USB 키보드(필요하면 마우스)를 먼저 연결한 다음 전원을 넣습니다.

Pi 5의 부트 펌웨어는 EEPROM에 있으므로 `bootcode.bin`, `start.elf`, UEFI 파일은 필요하지 않습니다. 저장소의 `config.txt`는 `kernel_2712.img`를 `0x80000`에 적재하고, RP1 USB를 위해 PCIe 링크를 유지하도록 설정되어 있습니다.

정상 부팅 조건:

* Mowkow OS 콘솔 출력
* 녹색 ACT LED가 0.5초 간격으로 점멸

화면이 나오지 않으면 먼저 다음을 확인합니다.

* `config.txt`와 `kernel_2712.img`가 하위 디렉터리가 아닌 FAT32 루트에 있는지
* staging의 `H04.FNT`와 앱·스크립트 파일을 빠짐없이 복사했는지
* USB 키보드를 전원을 넣기 전에 연결했는지
* ACT LED가 일정한 heartbeat 대신 반복적인 panic 점멸 패턴을 보이는지

## 시스템 구조

### 부트 시퀀스

Pi 5는 펌웨어에서 바로 진입합니다.

```
Pi 5 펌웨어 → kernel_2712.img (물리 0x80000)
  └ arch/aarch64/boot64.S: 보조 코어 대기, 필요 시 EL2 → EL1, BSS 초기화
    └ aarch64_main: MMU 설정, TTBR1 high-half로 전환
      └ aarch64_high_main (arch/aarch64/board64.c)
        └ Mailbox framebuffer → 예외/GIC → 메모리·스케줄러·Generic Timer
          └ SDHCI/FAT32·글꼴 → PCIe/RP1·xHCI/USB HID → GUI·콘솔 태스크
            └ 입력 폴링·이벤트 처리·닫힌 콘솔 회수·ACT LED heartbeat
```

보드 부팅과 입력 루프는 `board64.c`, GIO/ACT LED와 panic 점멸은
`gioaon64.c`, 자체 시험은 `boot_smoke64.c`가 담당합니다. IRQ/halt와
부팅용 카운터 접근은 `platform64.c`, 모듈 간 선언은
`include/arch/aarch64_boot64.h`에 있습니다. GUI·콘솔과 글꼴 수명은 보드
초기화가 소유하며 시험은 기존 초기화 단계에서 호출합니다.

`xhci64.c`는 controller 선택·MMIO·context·ring·DMA 버퍼를 소유하고,
`usb_xhci64.c`는 USB 요청·descriptor/HID 정책을 담당합니다. descriptor와
report 해독은 기존 `usbhid64.c`가 맡습니다. 공개 선언은
`include/arch/xhci64.h`와 `include/arch/usb_xhci64.h`에서 포함합니다.
비공개 전송 경계는 완료된 바이트를 복사하며 DMA 버퍼 포인터를 넘기지 않습니다.
보드의 단일 poller가 controller 선택부터 전송 완료까지 직렬화합니다.

xHCI가 사용하는 offset은 DWC3의 32-KiB xHCI register 영역 안에서 검사합니다
([Pi Linux의 DWC3 영역 정의](https://github.com/raspberrypi/linux/blob/rpi-6.12.y/drivers/usb/dwc3/core.h)).
capability chain은 최대 64개 header를 순회하며, 포트 일치 후에도 나머지
chain과 Supported Protocol의 PSI 길이를 검사합니다. context 크기와 completion
형식의 기준은 [xHCI 1.22 규격](https://www.intel.com/content/dam/www/public/us/en/documents/technical-specifications/extensible-host-controler-interface-usb-xhci.pdf)의
6.2 및 7.2절입니다. 초기화 때 기록한 32/64바이트 context 크기가 변경되면
전송을 거부하며, ring index·cycle·Link TRB와 제출한 TRB의 정렬·소속을
확인합니다. command/EP0 큐는 소진 시 오류를 반환하고 interrupt/event ring만
cycle을 바꾸며 순환합니다. 잘못된 completion은 입력 데이터를 복사하지 않고
대기 중인 interrupt 버퍼의 소유권을 유지합니다.

USB configuration은 알려진 descriptor의 최소 길이, HID의 subordinate 목록,
interface·endpoint 수와 alternate zero의 endpoint 중복을 검사합니다.
전체 configuration을 받으면 앞서 읽은 9-byte header와도 대조합니다.
HID 구조의 기준은 [HID 1.11의 6.2.1절](https://www.usb.org/sites/default/files/documents/hid1_11.pdf)입니다.
거부한 장치는 해당 root port만 격리하고 연관된 slot을 Disable Slot으로
회수합니다. PORTSC PED와 slot 회수의 기준은 위 xHCI 규격의 5.4.8 및
4.6.4절입니다. 회수 실패 시 DMA 버퍼를 재사용하지 않고 소유권을 유지합니다.
성공한 경우에는 root port 수 이내에서 다음 포트를 탐색합니다. controller당
하나의 장치를 처리하는 기존 버퍼 구조는 유지하며, 두 controller 중 실제
키보드 controller를 보존하고 반대쪽에서 마우스를 초기화합니다. 자동 hotplug나
여러 장치의 동시 구동을 추가한 것은 아닙니다.

mailbox는 channel 8의 16바이트 정렬 요청을 받아 별도 4KiB DMA 버퍼에
복사합니다. 버퍼 크기·tag 경계·응답 bit와 요청의 tag 배치를 검사한 뒤에만
호출자에게 응답을 복사합니다. timeout 뒤에도 firmware가 버퍼를 소유하므로
일치하는 늦은 응답을 회수하기 전에는 다음 요청에 재사용하지 않습니다.
광고된 응답 길이가 용량보다 커도 잘린 응답을 허용하는
[펌웨어 mailbox 규약](https://github.com/raspberrypi/firmware/wiki/Mailbox-property-interface)을
유지하며, 실제 framebuffer 필드의 최소 길이 검사는 framebuffer가 담당합니다.

RP1 BAR1은 이 포트가 지원하는 32비트 non-prefetchable 4MiB 영역인지와
root bridge memory base/limit 안에 전체가 포함되는지를 검사합니다.
[RP1 주소 맵](https://datasheets.raspberrypi.com/rp1/rp1-peripherals.pdf)의
2.3.1절을 기준으로 변환하며, I/O·64비트·예약 타입을 주소로 마스킹하지
않습니다. outbound/inbound window 설정은 readback 실패 시 이전 값으로
복구합니다. RP1 DMA 주소는 첫 1GiB RAM 안의 정렬된 버퍼 범위를 확인한 뒤
PCI alias를 더합니다. xHCI의 정적 전체 DMA state도 초기화 전에 검사하며,
64바이트 report 버퍼를 독립적으로 유지할 수 없는 cache line 크기는 거부합니다.

하드웨어 대기와 settling delay는 `include/arch/poll_deadline64.h`의 공통
counter helper를 사용합니다. fractional tick은 올림하고 주파수·곱셈 overflow와
반 counter 범위를 검사하며, unsigned elapsed 비교로 counter wrap을 처리합니다.
장치 응답은 각 단계 1초, USB 포트 연결은 3초, 부팅 키 누름/해제 시험은 각각
30초입니다. SD ACMD41의 준비 대기는 별도 1초 deadline을 적용하며, 내부 SD 명령도
각 단계 timeout으로 제한합니다. 복구 중 full reset에 도달한 카드는 재탐색해야 합니다.

실패하면 `[poll] 장치 단계 레지스터@주소 value=값 error=오류` 형식으로 출력하고
최근 32개 진단을 `poll64.c`의 IRQ-save ring에 보관합니다. W1C·reset·PCIe rollback
전에 상태를 기록합니다. 주소는 CPU 가상 주소이고 0은 counter/소프트웨어 상태를
뜻합니다. `aarch64_poll_snapshot64()`는 보관 중인 기록을 오래된 순서로 복사하며
덮어쓴 개수도 반환합니다. 화면 초기화 전 mailbox 실패는 메모리 기록과 기존
LED 진단으로 확인할 수 있습니다. ring은 재부팅 시 사라지고, 화면 출력은 기존
debug sink의 가시 영역 한계를 따릅니다. heartbeat 주파수 검증 실패의 panic
점멸 코드는 44입니다. panic 표시·보조 코어 대기·태스크 idle·실시간 이벤트 루프는
의도적으로 계속 실행되며 장치 준비 polling과 구분합니다.

### 메모리와 태스크

Pi 5 커널은 물리 `0x80000`에 적재되고 `ARCH64_KERNEL_VA_BASE`
(`0xffffff8000000000`)를 더한 high-half 주소에서 실행됩니다. 일반 메모리 풀도
high-half 주소로 관리하며, ELF 앱은 공용 `app64.ld`의 낮은 가상 주소를 씁니다.

MMU는 4KiB 페이지를 사용합니다. 부팅 중 저주소 bootstrap 매핑에서 TTBR1의
high-half 커널로 전환한 뒤 TTBR0를 사용자 매핑용으로 사용합니다. 프로세스마다
별도 페이지 테이블을 두지는 않으며, ELF 이미지 창은 공용 실행 정책에 따라
한 앱만 소유할 수 있습니다. 사용자 매핑은 범위와 필요한 L3 테이블 수를
먼저 확인하며, 테이블 부족 시 기존 매핑을 보존합니다. cache maintenance는
CTR_EL0에서 D/I line 크기를 각각 읽고 끝 주소·정렬 overflow를 검사합니다.

`arch/aarch64/sched64.c`는 Generic Timer IRQ의 예외 복귀 프레임을 바꾸어
태스크를 전환합니다. 공용 `mtask64.c`의 run queue·sleep/wake 정책을 사용하고,
아키텍처별 저장 프레임에 일반 레지스터와 FP/SIMD 상태를 보존합니다.

### 사용자 모드와 syscall

`arch/aarch64/user64.S`가 EL0 진입·EL1 복귀를 처리합니다. SVC 경계에서 인자를
공용 syscall 프레임으로 변환해 `syscall_handler64`에 전달합니다.
EL0 fault는 해당 프로세스를 종료하고 콘솔로 복귀하며 EL1 fault는 panic합니다.

| syscall ABI | 값 |
|-|-|
| 진입 | `svc #0` |
| 번호 | `x8` |
| 인자 1~6 | `x0`~`x5` |
| 반환값 | `x0` (오류는 음수) |
| ELF machine | `EM_AARCH64` |

번호와 각 연산의 의미는 [공통 syscall 계약](../architecture.md#프로세스와-syscall)을 따릅니다.

### 화면과 입력

Mailbox property channel로 32bpp 프레임버퍼를 얻고, 공용 8bpp 시트의
팔레트 인덱스를 `framebuffer64.c`에서 펌웨어가 반환한 RGB/BGR 순서로 변환합니다.
갱신 행의 D-cache를
정리한 뒤 가시성 barrier를 수행합니다.

GIC와 100Hz Generic Timer를 사용합니다. USB는 PCIe → RP1 → xHCI → HID 경로로
연결하며, 부팅 전에 연결한 키보드·마우스를 대상으로 합니다. 메인 루프는 HID를
폴링하여 키를 포커스 콘솔 큐에 넣고 마우스 보고서를 GUI에 전달합니다.

### 저장 장치와 앱 파일

`arch/aarch64/sdhci64.c`가 SD 카드 I/O를 제공하고, `block64.c`가 MBR의
파티션 offset을 적용합니다. 공용 FAT32·캐시를 통해 부트 파티션 루트의 글꼴,
앱과 스크립트를 읽습니다. x86 BIOS용 예약 영역에 커널을 넣는 방식은 사용하지
않으며, 펌웨어가 `kernel_2712.img` 파일을 직접 적재합니다.

앱은 `HELLO.ELF`, `CAT.ELF`, `KTEST.ELF`, `MTEST.ELF`, `WTEST.ELF`, `나노.ELF`로
staging됩니다. 콘솔에서는 `hello`, `cat`, `나노`처럼 확장자 없이 실행할 수 있습니다.
부트 파일 목록은 `64bit/mk/aarch64.mk`의 `A64_BOOT_STAGE_SPECS`에서 관리하므로,
새 앱이나 스크립트는 staging 목록도 확인해야 합니다.

## 검증과 문제 해결

크로스 빌드 통과는 실제 보드 검증을 대신하지 않습니다. SDHCI·PCIe/RP1·xHCI·
Mailbox·MMU·cache·IRQ·타이머 변경은 관련 Pi 5 실기기 검증이 필요합니다.

- 부트 파일 누락·panic 점멸 패턴을 먼저 확인합니다.
- [보드 bring-up·panic code·실기 검증 기록](../../64bit/board/pi5/README.md)은
  당시 단계별 동작을 기록한 자료입니다. 현재 빌드·복사 절차는 이 문서를 따릅니다.
- 하드웨어 결과는 날짜, 소스 commit, 보드·주변기기 구성, pass/fail 항목을 함께 기록합니다.

`config.txt`에서 `armstub=`을 지정하지 않으면 펌웨어 내장 stub을 사용합니다.
따라서 `armstub8-2712.bin` 파일이 없어도 되며, 펌웨어의 해당 파일 탐색 로그만으로
부팅 실패를 판정하지 않습니다.

커널 진입 후 첫 화면이 나오기 전 ACT LED가 150ms 간격으로 점멸하고 1.2초 쉬는
패턴을 반복하면 다음 framebuffer 진단 코드를 확인합니다. 펌웨어 자체 LED 코드와는
별개이며, 화면이 나온 뒤의 동일 횟수는 다른 부팅 단계의 실패를 뜻할 수 있습니다.

| 점멸 횟수 | 첫 화면 이전 실패 조건 |
|-|-|
| 2 | mailbox 전송 실패 또는 timeout |
| 3 | 전체 응답 크기·상태 또는 끝 태그 오류 |
| 4 | 태그 ID·버퍼 크기·응답 비트 또는 필요한 응답 길이 오류 |
| 5 | RGB/BGR 이외의 알 수 없는 색상 순서 |
| 6 | framebuffer 주소 정렬 또는 첫 1GiB 매핑 범위 오류 |
| 7 | pitch·가시 영역·버퍼 용량 등 공용 framebuffer 검증 실패 |
| 8 | 32bpp 이외의 색 깊이 |
| 9 | 해상도 0 또는 BOOTINFO64 해상도·pitch 필드 범위 초과 |
| 10 | 가상 framebuffer가 실제 화면보다 작음 |
| 11 | 가상 화면 offset이 0이 아님 |
| 12 | framebuffer 할당 크기 0 |

2026-10-23: 리팩토링 후 첫 화면 없이 LED 3회 점멸한다는 실기기 보고가 있습니다.
기존 코드는 모든 framebuffer 응답 검증 실패를 3회로 표시했습니다. 응답 길이가
버퍼보다 커질 수 있는 mailbox 규약을 반영하고 위 진단 코드를 추가했습니다.
이 단계에서는 거부된 필드를 확인하지 못해 후속 진단 이미지를 준비했습니다.

2026-10-24: 위 진단 이미지에서 첫 화면 이전 LED 5회 점멸이 보고되었습니다.
이 이미지의 5회 코드는 여러 값 검사를 묶고 있어 RGB/BGR 중 어느 조건인지
확정할 수 없습니다. 유효한 BGR 응답도 조기 화면과 GUI에서 처리하도록 수정하고,
나머지 값 검사도 위 표처럼 분리했습니다. BOOTINFO64의 24바이트 배치를 유지하며
framebuffer_type 상위 비트로 BGR을 전달합니다.

2026-10-24: BGR 처리 수정 이미지를 적용한 뒤 사용자가 Pi 5에서 화면 표시 복구를
확인했습니다. RGB만 허용한 응답 검사가 유효한 BGR 응답을 거부한 회귀로 판단합니다.
원시 mailbox 응답 덤프를 수집한 것은 아니며, 기존 5회 점멸과 BGR 지원 변경 후의
복구 결과에 근거합니다. 응답 길이 검사 수정만 적용한 이미지에서는 5회 점멸이
계속되었으므로, 그 수정만으로 부팅 문제가 해결된 것은 아닙니다.

검증 이미지와 범위:

- 소스: `f55fb96` 기반 미커밋 framebuffer 수정 작업 트리.
- 이미지 SHA-256:
  `fac555e80d8280747a6beb24dbead7fb4a14c21e0922373918c77bf7c89f7721`.
- 보드/구성: Raspberry Pi 5, SD 부팅, staging과 동일한 `config.txt` 및
  `kernel_2712.img`, 외부 `armstub8-2712.bin` 없음. RAM 용량, 펌웨어 버전,
  모니터와 주변기기 상세는 기록되지 않았습니다.
- PASS: 수정 이미지에서 화면 표시 복구(사용자 실기기 확인).
- PASS: `make clean64` 후 `make check64` — 스타일 검사, 호스트 테스트 57개,
  x86_64/AArch64 빌드 및 boot staging. 호스트에서 RGB/BGR 채널 변환,
  owner mask, 잘못된 형식 거부와 조기 화면 색상 변환을 확인했습니다.
- 미검증: 실제 화면 색상 정확도, GUI/입력 전체 동작, SD 쓰기·재부팅 persistence,
  앱·nano·MicroPython·머꼬, 장시간 동작. 이번 수정의 QEMU 실행 검증도 미수행입니다.

화면 표시 회귀는 복구되었지만, 이 결과만으로 리팩토링 G 전체의 Pi 5 검증을
완료 처리하지 않습니다.

2026-10-24 후속 보고에서 사용자는 `refactor_plan.md`의 기존 실기기 검증을
모두 완료했다고 확인했습니다. 따라서 G 검증 대기와 H 보류를 해제했습니다.
해당 시험 이미지의 commit/hash와 상세 구성·항목별 로그는 추가로 제공되지
않았습니다. 이 보고는 H 첫 모듈 분리 이전 상태에 해당하며, 분리 이후
이미지의 실기기 재검증 결과는 아닙니다([기록](../../CONTEXT.md)).

이후 사용자 보고로 H 첫 모듈 분리, 두 번째 xHCI/USB 계층 분리,
세 번째 capability/ring/context 강화, 네 번째 descriptor/port 격리와
다섯 번째 mailbox·PCIe·MMU·cache 이미지의 실기기 검증도 각각 완료했습니다.
날짜·직전 staging hash·보고 범위는 [검증 기록](../../CONTEXT.md)에 있습니다.
후속 사용자 보고로 H 여섯 번째 deadline·진단을 포함한 섹션 H 전체의
실기기 검증도 완료했습니다. 이후 테스트 정리는 호스트 테스트와 문서에
한정하며 커널 이미지는 변경하지 않습니다.

기본 부팅은 FAT32 쓰기 시험을 하지 않습니다. 폐기 가능한 SD 카드에서
부팅 중 쓰기 시험을 하려면 다음과 같이 clean build합니다.

```bash
make clean-a64
make BOOT_TEST64=1 aarch64
```

시험 빌드는 테스트 파일을 만들거나 갱신하므로 매체를 변경합니다.
일반 빌드로 돌아올 때도 `make clean-a64` 뒤 `make aarch64`로 다시 빌드합니다.
