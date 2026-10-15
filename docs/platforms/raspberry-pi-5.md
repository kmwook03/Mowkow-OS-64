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
      └ aarch64_high_main (arch/aarch64/gioaon64.c)
        └ Mailbox framebuffer → 예외/GIC → 메모리·스케줄러·Generic Timer
          └ SDHCI/FAT32·글꼴 → PCIe/RP1·xHCI/USB HID → GUI·콘솔 태스크
            └ 입력 폴링·이벤트 처리·닫힌 콘솔 회수·ACT LED heartbeat
```

Pi 5의 보드 초기화와 자체 시험은 아직 `gioaon64.c`에 함께 있습니다.
이를 분리하는 작업과 xHCI 계층 정리는 리팩터링 계획의 후속 항목입니다.

### 메모리와 태스크

Pi 5 커널은 물리 `0x80000`에 적재되고 `ARCH64_KERNEL_VA_BASE`
(`0xffffff8000000000`)를 더한 high-half 주소에서 실행됩니다. 일반 메모리 풀도
high-half 주소로 관리하며, ELF 앱은 공용 `app64.ld`의 낮은 가상 주소를 씁니다.

MMU는 4KiB 페이지를 사용합니다. 부팅 중 저주소 bootstrap 매핑에서 TTBR1의
high-half 커널로 전환한 뒤 TTBR0를 사용자 매핑용으로 사용합니다. 프로세스마다
별도 페이지 테이블을 두지는 않으며, ELF 이미지 창은 공용 실행 정책에 따라
한 앱만 소유할 수 있습니다.

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

Mailbox property channel로 32bpp RGB 프레임버퍼를 얻고, 공용 8bpp 시트의
팔레트 인덱스를 `framebuffer64.c`에서 RGB로 변환합니다. 갱신 행의 D-cache를
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

기본 부팅은 FAT32 쓰기 시험을 하지 않습니다. 폐기 가능한 SD 카드에서
부팅 중 쓰기 시험을 하려면 다음과 같이 clean build합니다.

```bash
make clean-a64
make BOOT_TEST64=1 aarch64
```

시험 빌드는 테스트 파일을 만들거나 갱신하므로 매체를 변경합니다.
일반 빌드로 돌아올 때도 `make clean-a64` 뒤 `make aarch64`로 다시 빌드합니다.
