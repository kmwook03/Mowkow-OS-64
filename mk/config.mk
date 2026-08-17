# ---------------------------------------------------------------------------
# 공통 설정: 디렉터리 배치, 도구 경로, 컴파일 옵션
#
# 두 트리(32비트 src/, 64비트 src64/)가 함께 쓰는 값만 여기에 둔다.
# 한쪽 트리에서만 쓰는 규칙은 mk/x86.mk, mk/x86_64.mk에 있다.
# ---------------------------------------------------------------------------

# -- 디렉터리 --
TOOLPATH = tools
INCPATH = tools/haribote
FONT_DIR = src/graphics/font

SRC_DIR = src
APP_DIR = app
BUILD_DIR = build
IMG_DIR = img

SRC64_DIR = src64
APP64_DIR = app64
BUILD64_DIR = build64
IMG64_DIR = img64

MPY_DIR = third_party/micropython
MPY_PY_DIR = $(MPY_DIR)/py

# -- 공용 도구 --
QEMU = qemu-system-x86_64
PYTHON ?= python3

COPY = cp
DEL = rm -rf
MKDIR = mkdir -p

# -- 32비트 도구 모음 (교재 툴체인, tools/ 에 함께 들어 있다) --
NASK = $(TOOLPATH)/nask
CC1 = $(TOOLPATH)/gocc1 -I$(INCPATH) -Os -Wall -quiet
GAS2NASK = $(TOOLPATH)/gas2nask -a
OBJ2BIM = $(TOOLPATH)/obj2bim
MAKEFONT = $(TOOLPATH)/makefont
BIN2OBJ = $(TOOLPATH)/bin2obj
BIM2HRB = $(TOOLPATH)/bim2hrb
BIM2BIN = $(TOOLPATH)/bim2bin
RULEFILE = $(TOOLPATH)/haribote/haribote.rul
EDIMG = $(TOOLPATH)/edimg
FDIMG2ISO = $(TOOLPATH)/makeiso/fdimg2iso
GOLIB = $(TOOLPATH)/golib00

# -- 64비트 도구 모음 (바깥에서 설치한 크로스 툴체인) --
X64_CC ?= x86_64-elf-gcc
X64_ASM ?= nasm
X64_LD ?= x86_64-elf-ld
X64_OBJCOPY ?= x86_64-elf-objcopy

# -MMD -MP: 헤더 의존성을 만들지 않으면 src64/include/fd64.h 같은 헤더를
# 고쳤을 때 예전 구조체 배치로 컴파일된 오브젝트가 그대로 링크된다.
X64_DEPFLAGS = -MMD -MP

X64_CFLAGS = -ffreestanding -mno-red-zone -fno-pic -fno-stack-protector -Wall -Wextra -Wa,--noexecstack -I$(SRC64_DIR)/include
X64_ASMFLAGS = -f elf64
X64_BOOT_ASMFLAGS = -f bin
X64_LDFLAGS = -nostdlib -T $(SRC64_DIR)/kernel/kernel64.ld

APP64_CFLAGS = -ffreestanding -fno-pic -fno-pie -mno-red-zone -fno-stack-protector -fno-asynchronous-unwind-tables -fno-unwind-tables -nostdlib -Wall -Wextra -I$(APP64_DIR)/crt/include
APP64_LDFLAGS = -nostdlib -static -T $(APP64_DIR)/app64.ld
