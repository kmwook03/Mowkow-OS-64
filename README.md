# Mowkow OS 64

## 0. **머꼬 OS 소개**
### **머꼬 인터프리터와 GNU Nano가 내장된 32비트 교육용 한글 운영체제**
Mowkow OS는 저서 『OS 구조와 원리』를 기반으로 제작된 교육용 한글 운영체제입니다.<br>
기본적인 커널 구조 위에 독자적인 한글 입력기를 구현하여 자유로운 한글 입출력이 가능합니다. <br>
부산대학교 우균 교수님이 개발한 한글 LIPS 머꼬 인터프리터를 내장하고 있습니다.

### **64bit로 확장**
32bit 머꼬 OS는 아래와 같은 문제를 가졌습니다.

1. 교재에서 제공하는 툴체인(`gocc1`, `nask` 등)에 갇혀 외부 소프트웨어 이식이 매우 제한적이었습니다.
2. 메모리 배치가 교재 기준으로 고정되어 있어 확장이 어려웠고, 앱 보호를 LDT 세그먼트에 의존하여 롱 모드에서는 유지할 수 없는 방식이었습니다.
3. 플로피 디스크와 FAT12(8.3 포맷)를 전제로 만들어져, 디스크 이미지를 통째로 메모리에 올려두어야 했고 실제 디스크에 얹을 수 없었습니다.

이를 극복하기 위해 64bit 마이그레이션을 결정하였습니다.

### 목차<br>
* [1. 64bit 마이그레이션](#1-64bit-마이그레이션)
* [2. 64bit 빌드 및 실행 방법](#2-64bit-빌드-및-실행-방법)

    * [2-1. 준비물](#2-1-준비물)
    * [2-2. 빌드와 실행](#2-2-빌드와-실행)
    * [2-3. 부팅이 이상할 때](#2-3-부팅이-이상할-때)
* [3. 64bit 명령어 목록](#3-64bit-명령어-목록)
* [4. 64bit 시스템 구조](#4-64bit-시스템-구조)
* [5. 개발 진행 사황](#5-진행-상황)
* [6. 주의할 점](#6-주의할-점)

---

## 1. 64bit 마이그레이션
이 저장소에는 운영체제 트리가 두 가지 존재합니다.

| | 32bit | 64bit |
|-|-|-|
| 소스 | `src/`, `app/` | `src64/`, `app64` |
| 모드 | x86 보호 모드 | x86-64 롱 모드 |
| 파일 시스템 | FAT12 (플로피) | FAT32 (하드디스크, 64MiB) |
| 툴체인 | 교재 제공 도구(`tools/`) | `x86_64-elf-gcc`, `nasm`, `x86_64-elf-ld` |
| 실행 파일 | HRB | 정적 ELF64 |
| 결과물 | `img/haribote.img` | `img64/mowkow64.img` |

두 트리는 빌드 결과물을 공유하지 않습니다.

### 1-1. 64bit 주요 기능

* 아키텍처: x86-64 롱 모드 (PAE, 4단계 페이지 테이블, 소프트웨어 태스크 스위치)
* 화면: 800x600 256색 (VBE 선형 프레임 버퍼)
* 한글화: 두벌식 오토마타와 조합형 글꼴을 커널이 관리
* 파일 시스템: FAT32 읽기/쓰기, VFAT 긴 이름 지원
* 저장 장치: AHCI(SATA)를 먼저 찾고 없으면 ATA PIO로
* 창 화면: `창`/`window` 명령어 입력 시 GUI로 전환
* 파이썬: MicroPython이 커널에 함께 링크되어 있어 `py` 명령어로 사용 가능
* 앱: 나노 편집기를 비롯한 ELF64 응용 프로그램

## 2. 64bit 빌드 및 실행 방법
### 2-1. 준비물
64bit 트리는 교재 도구가 아닌 gnu 크로스 툴체인(GCC 13.1.0, binutils 2.40.)을 사용합니다.

* `x86_64-elf-gcc`, `x86_64-elf-ld`, `x86_64-elf-objcopy`
* `nasm`
* `qemu-system-x86_64`
* `python3` (이미지 생성 도구)

#### 크로스 툴체인 설치 방법
```bash
sudo apt install build-essential bison flex libgmp-dev libmpc-dev libmpfr-dev texinfo
```
```bash
export PREFIX="$HOME/opt/cross"
export TARGET=x86_64-elf
export PATH="$PREFIX/bin:$PATH"
```
```bash
# binutils 2.40
tar xf binutils-2.40.tar.xz && mkdir build-binutils && cd build-binutils
../binutils-2.40/configure --target=$TARGET --prefix="$PREFIX" \
    --with-sysroot --disable-nls --disable-werror
```
```bash
make -j$(nproc) && make install && cd ..
```
```bash
# gcc 13.1.0
tar xf gcc-13.1.0.tar.xz && mkdir build-gcc && cd build-gcc
../gcc-13.1.0/configure --target=$TARGET --prefix="$PREFIX" \
    --disable-nls --enable-languages=c --without-headers
make -j$(nproc) all-gcc all-target-libgcc
make install-gcc install-target-libgcc
```

#### `make x86_64`가 컴파일러를 못찾는 경우
```bash
make x86_64 X64_CC=/path/to/x86_64-elf-gcc X64_LD=/path/to/ x86_64-elf-ld
```

### 2-2. 빌드와 실행

```bash
make x86_64         # img64/mowkow.img 만들기
make run64          # IDE로 실행 (ATA PIO 경로)
make run64-ahci     # q35 + AHCI로 실행 (AHCI 경로)
make clean64        # build64/, img64/ 지우기(rm)
```

`make help`를 치면 32bit와 64bit 명령이 함께 출력됩니다.

`run64`와 `run64-ahci`는 같은 이미지를 서로 다른 저장 장치 경로로 띄웁니다.
q35에는 옛 IDE가 없어서 저장 장치 쪽이 수정되었다면 두 명령어 다 시도해봐야합니다.

### 2-3. 부팅이 이상할 때
콘솔이 뜨기 전 단계는 화면에 아무 것도 출력할 수 없습니다.
따라서 커널이 진행 상황을 COM1(시리얼)로 내보냅니다.
QEMU에 `-serial stdio`를 붙이면 아래와 같이 확인 가능합니다.

```
Mowkow OS x86_64 kernel64_main
fpu smoke=2
disk transport=ata part-base=0
fat32 files=8
fat32 write=18 sectors=0
fat32 chain=ok
fat32 lfn=ok
sheet64 smoke=ok
keyboard64 smoke=ok
hangul64 smoke=ok
hangul font=loaded
```

위 출력은 부팅마다 수행되는 자체 점검입니다.
멈춘 위치가 깨진 위치입니다. `sectors=0`은 오류가 아니라 이미 디스크에 쓰기가 완료된 후라 내보낼 것이 없다는 뜻입니다.

## 3. 64bit 명령어 목록

### 3-1. 애플리케이션 실행
64bit 콘솔에서는 애플리케이션 이름만 입력하면 실행됩니다.
```bash
# 예) 나노 편집기 열기
> 나노

# 예) 나노 편집기로 텍스트 파일 열기
> 나노 README.TXT

# 예) 파일 내용 보기
> cat readme.txt
```

내장 명령이 아닌 낱말은 FAT32에서 같은 이름의 실행 파일을 찾습니다.
없으면 `알 수 없는 명령어`라고 알립니다.

### 3-2. CLI-GUI 전환
```bash
# 한글 명령어
> 창

# 영어 명령어
> window
```

화면 출력 모드를 변경합니다.
GUI 모드에서는 마우스로 제목 표시줄을 끌어 창을 옮기거나 창을 클릭하여 활성화 여부를 변경할 수 있습니다.

### 3-3. 파이썬
```bash
# REPL 시작 (Ctrl-D로 나감)
> py

# 파일 실행
> py TEST.PY
```

### 3-4. 기타 명령어
```bash
> help
> 목록 / ls # 파일 목록
> 메모리 / mem # 남은 메모리
> 지우기 / clear # 화면 지우기
> 태스크 / tasks # 태스크 전환 횟수
> ticks # 타이머 틱
> 읽기 / type # 파일 내용 보기
```

## 4. 64bit 시스템 구조
### 4-1. 부트 시퀀스
```
boot64.asm (1단계, 부트 섹터)
  └ A20, 화면 모드(VBE 0x4103), 2단계 로더 읽기
    └ loader64.asm (2단계)
      └ 페이지 테이블, PAE, EFER.LME, CR0.PG → 롱 모드
        └ kernel64_main (kernel64.c)
```

`kernel64_main` 초기화 순서
```
1. GDT/IDT
2. 메모리 관리자
3. FAT32
4. 한글 글꼴
5. 콘솔
6. 태스크
7. 이벤트 큐 + 타이머/키보드
8. PIC
9. sti
10. 이벤트 루프
```

### 4-2. 저장 장치 계층
```
fd64.c FAT32 + VFAT 긴 이름
    ↓
cache64.c 되쓰기 섹터 캐시 (4KiB 블록, 4MiB)
    ↓
block64.c 전송 계층 고르기 + 파티션 시작 위치
    ↓
ahci64.c / ata64.c
```

전송 계층을 추가하고싶다면 `struct BLOCK64_OPS`만 채우면 됩니다.
그 위쪽은 변경되지 않습니다.

### 4-3. 디스크 배치 (64MiB 이미지)
```
LBA 0           부트 섹터 (FAT32 BPB)
LBA 1           FSInfo
LBA 6, 7        부트 섹터/FSInfo 사본
LBA 8..23       2단계 로더
LBA 32..1023    커널 이미지 (예약 영역)
LBA 1024        FAT #1
LBA 2025        FAT #2
LBA 3026        데이터 영역, 클러스터 2 = 루트 디렉터리
```

2단계 로더와 커널이 예약 영역에 있으므로 커널이 커져도 파일 시스템이 밀리지 않습니다.
이 배치는 `tools/mkfat32_64.py` 한 곳에만 적혀 있고, Makefile과 부트 섹터가 해당 파일에서 값을 읽습니다.
배치를 바꿔야 하면 Makefile이나 BPB를 직접 고치지 말고, `tools/mkfat32_64.py`를 고쳐야 합니다.

### 4-4. 빌드 구성
Makefile은 트리별로 나뉘어 있습니다.
```
Makefile            기본 목표, help, info
mk/config.mk        디렉터리, 도구, 컴파일 옵션 (공통)
mk/x86.mk           32bit 빌드 규칙
mk/micropython.mk   마이크로파이썬 빌드 규칙
mk/x86_64.mk        64bit 빌드 규칙
```

### 4-5. 프로젝트 디렉터리
```
.
├── 📂mk                # 빌드 규칙 (트리별)
├── 📂src64             # 64비트 커널
│   ├── 📂boot              # boot64.asm(1단계), loader64.asm(2단계)
│   ├── 📂drivers           # ahci64 ata64 block64 pci64 graphic64
│   │                       # keyboard64 mouse64 timer64 int64
│   ├── 📂kernel            # kernel64 console64 fd64 cache64 memory64
│   │                       # mtask64 dsctbl64 sheet64 window64 gui64
│   │                       # process64 syscall64 elf64_loader
│   ├── 📂lib               # hangul64 utf864 fifo64 kstring64
│   ├── 📂mpport            # MicroPython 포팅 계층
│   └── 📂include           # 헤더 (모든 이름에 64가 붙음)
├── 📂app64             # 64비트 응용 프로그램
│   ├── 📂crt               # 공용 런타임 (crt0, 시스템 콜, 문자열, malloc)
│   ├── 📁cat               # 파일 내용 출력
│   ├── 📁hello             # 최소 예제
│   ├── 📁ktest             # 키 입력 점검
│   ├── 📁mtest             # 메모리 할당 점검
│   ├── 📁wtest             # FAT32 쓰기 점검
│   └── 📁나노              # 나노 편집기
├── 📂py64              # 머꼬 인터프리터 (파이썬 판)
└── 📂third_party
    └── 📁micropython       # 업스트림 MicroPython
```

## 5. 진행 상황

| 항목 | 상태 |
|-|-|
| 롱 모드 부팅 | ✅ |
| FAT32 읽기/쓰기 | ✅ |
| 한글 콘솔 (두벌식, UTF-8) | ✅ |
| 시트/창 컴포지터, 창 전환 | ✅ |
| 커널 내장 MicroPython | ✅ |
| 나노 편집기 | ✅ |
| 프로세스별 주소 공간 (Ring-3 보호) | ❌ |
| 머꼬 인터프리터 | ❌ |

## 6. 주의할 점

* 가드 페이지가 없어 재귀가 깊은 프로그램은 스택을 넘길 수 있습니다.
* 나노로 저장한 파일은 이미지 파일에 실제로 남습니다. `make clean64` 뒤 다시 빌드하면 새로 저장된 파일이 사라지니 주의하세요.
* 모든 파일은 루트 디렉터리에 위치합니다.
