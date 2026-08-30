# ---------------------------------------------------------------------------
# 64비트 트리(src64/, app64/) 빌드 규칙
#
# 진짜 크로스 툴체인(x86_64-elf-gcc, nasm -f elf64, x86_64-elf-ld)으로
# FAT32 이미지 img64/mowkow64.img를 만든다. 새 작업은 이쪽에서 한다.
#
# 목표:  make x86_64 / make run64 / make run64-ahci / make clean64
#
# 커널에 링크할 MicroPython 오브젝트 목록(MPY_*)은 mk/micropython.mk에서
# 온다. 규칙을 읽는 시점에 그 변수들이 있어야 하므로 include 순서가 중요하다.
# ---------------------------------------------------------------------------

# -- FAT32 배치 --
# 배치는 tools/mkfat32_64.py 한 곳에만 있고, 이 makefile과 boot64.asm의 BPB가
# 거기서 값을 받아 간다. 이미지와 부트 섹터가 조용히 어긋나면 실행 중에는
# 되돌릴 방법이 없다.
MKFAT32 = $(TOOLPATH)/mkfat32_64.py
$(foreach v,$(shell $(PYTHON) $(MKFAT32) --make-vars),$(eval $(v)))

# 로더가 읽어 들이는 커널 섹터 수이자 커널 크기 상한. 예약 영역은 992섹터
# (507,904바이트)까지 있으므로 그 안에서는 올려도 파일 시스템이 밀리지 않는다.
# 800섹터일 때 커널이 389,540바이트(95.1%)라 남은 자리가 20KiB뿐이었다. 
# 992섹터 천장 아래로 여유를 두고 960으로 올린다.
KERNEL64_SECTORS = 960

# -- 소스 찾기 --
KERNEL64_C_SRCS = $(wildcard $(SRC64_DIR)/kernel/*.c) $(wildcard $(SRC64_DIR)/drivers/*.c) $(wildcard $(SRC64_DIR)/lib/*.c)
KERNEL64_ASM_SRCS = $(wildcard $(SRC64_DIR)/kernel/*.asm)
KERNEL64_C_OBJS = $(patsubst $(SRC64_DIR)/%.c, $(BUILD64_DIR)/%.o, $(KERNEL64_C_SRCS))
KERNEL64_ASM_OBJS = $(patsubst $(SRC64_DIR)/kernel/%.asm, $(BUILD64_DIR)/kernel/%.o, $(KERNEL64_ASM_SRCS))

KERNEL64_OBJS = $(KERNEL64_ASM_OBJS) $(KERNEL64_C_OBJS) $(MPY_OBJS) $(MPY_PORT_OBJS) $(MPY_SHARED_OBJS)

KERNEL64_ELF = $(BUILD64_DIR)/kernel/kernel64.elf
KERNEL64_BIN = $(BUILD64_DIR)/kernel/kernel64.bin
BOOT64_BIN = $(BUILD64_DIR)/boot/boot64.bin
LOADER64_BIN = $(BUILD64_DIR)/boot/loader64.bin
IMG64_FILE = $(IMG64_DIR)/mowkow64.img

# -- 앱 찾기 (app64/ 아래 디렉터리 하나가 앱 하나, 공용 런타임 crt는 뺀다) --
# -- 이미지에 넣을 파이썬 소스 (py64/ 바로 아래, 이름이 곧 파일 이름) --
# 머꼬 모듈은 py64/머꼬/ 아래 있고 이미지에서는 루트에 평평하게 놓인다.
PY64_FILES = $(wildcard $(PY64_DIR)/*.PY) $(wildcard $(PY64_DIR)/*.SCM) \
	$(wildcard $(PY64_DIR)/*.py) $(wildcard $(PY64_DIR)/*.scm) \
	$(wildcard $(PY64_DIR)/머꼬/*.py) $(wildcard $(PY64_DIR)/머꼬/*.scm) \
	$(wildcard $(PY64_DIR)/머꼬/*.mk)

APP64_DIRS = $(wildcard $(APP64_DIR)/*/)
APP64_NAMES = $(filter-out crt,$(notdir $(patsubst %/,%,$(APP64_DIRS))))
APP64_TARGETS = $(foreach app, $(APP64_NAMES), $(BUILD64_DIR)/app/$(app)/$(app).elf)
APP64_CRT_OBJS = $(BUILD64_DIR)/app/crt/crt0.o $(BUILD64_DIR)/app/crt/syscall.o \
	$(BUILD64_DIR)/app/crt/string.o $(BUILD64_DIR)/app/crt/malloc.o

# -- 부트로더 (1단계 부트 섹터, 2단계 로더) --
# 두 단계 모두 flat binary라 BPB와 커널 LBA를 -D로 심어 준다.
$(BOOT64_BIN) : $(SRC64_DIR)/boot/boot64.asm Makefile mk/x86_64.mk $(MKFAT32)
	@$(MKDIR) $(dir $@)
	$(X64_ASM) $(X64_BOOT_ASMFLAGS) -DSTAGE2_SECTORS=$(STAGE2_64_SECTORS) \
		-DSTAGE2_LBA=$(STAGE2_64_LBA) -DTOTAL_SECTORS=$(FAT32_64_TOTAL_SECTORS) \
		-DRESERVED_SECTORS=$(FAT32_64_RESERVED_SECTORS) -DFAT_COUNT=$(FAT32_64_FAT_COUNT) \
		-DSECTORS_PER_FAT=$(FAT32_64_SECTORS_PER_FAT) -DROOT_CLUSTER=$(FAT32_64_ROOT_CLUSTER) \
		-DFSINFO_LBA=$(FAT32_64_FSINFO_LBA) -DBACKUP_BOOT_LBA=$(FAT32_64_BACKUP_BOOT_LBA) \
		-DVOLUME_ID=$(FAT32_64_VOLUME_ID) $< -o $@

$(LOADER64_BIN) : $(SRC64_DIR)/boot/loader64.asm Makefile mk/x86_64.mk
	@$(MKDIR) $(dir $@)
	$(X64_ASM) $(X64_BOOT_ASMFLAGS) -DKERNEL_LBA=$(KERNEL64_LBA) -DKERNEL_SECTORS=$(KERNEL64_SECTORS) $< -o $@

# -- 커널 (C와 어셈블리 -> ELF -> flat binary) --
$(BUILD64_DIR)/%.o : $(SRC64_DIR)/%.c
	@$(MKDIR) $(dir $@)
	$(X64_CC) $(X64_CFLAGS) $(X64_DEPFLAGS) -c $< -o $@

$(BUILD64_DIR)/kernel/%.o : $(SRC64_DIR)/kernel/%.asm
	@$(MKDIR) $(dir $@)
	$(X64_ASM) $(X64_ASMFLAGS) $< -o $@

$(KERNEL64_ELF) : $(KERNEL64_OBJS) $(SRC64_DIR)/kernel/kernel64.ld
	@$(MKDIR) $(dir $@)
	$(X64_LD) $(X64_LDFLAGS) -o $@ $(KERNEL64_OBJS)

# 로더는 KERNEL64_SECTORS만큼만 읽는다. 커널이 그보다 커지면 뒷부분이 없는 채로
# 실행되므로, 링크 직후에 크기를 확인하고 넘으면 빌드를 멈춘다.
$(KERNEL64_BIN) : $(KERNEL64_ELF)
	$(X64_OBJCOPY) -O binary $< $@
	@size=$$(stat -c%s $@); budget=$$(expr $(KERNEL64_SECTORS) \* 512); \
	if [ $$size -gt $$budget ]; then \
		echo "error: $@ is $$size bytes, exceeds KERNEL64_SECTORS budget of $$budget bytes"; \
		echo "raise KERNEL64_SECTORS in mk/x86_64.mk"; \
		exit 1; \
	fi

# -- 앱 (공용 런타임 crt + 앱 하나짜리 소스) --
$(BUILD64_DIR)/app/crt/%.o : $(APP64_DIR)/crt/%.S
	@$(MKDIR) $(dir $@)
	$(X64_CC) $(APP64_CFLAGS) $(X64_DEPFLAGS) -c $< -o $@

$(BUILD64_DIR)/app/crt/%.o : $(APP64_DIR)/crt/%.c
	@$(MKDIR) $(dir $@)
	$(X64_CC) $(APP64_CFLAGS) $(X64_DEPFLAGS) -c $< -o $@

# 앱 이름이 곧 디렉터리 이름이자 소스 이름이다(app64/cat/cat.c).
# 앱마다 같은 규칙 두 개를 찍어 내므로 이름별로 만들어 준다.
define APP64_RULES
$(BUILD64_DIR)/app/$(1)/$(1).o : $(APP64_DIR)/$(1)/$(1).c
	@$$(MKDIR) $$(dir $$@)
	$$(X64_CC) $$(APP64_CFLAGS) $$(X64_DEPFLAGS) -c $$< -o $$@

$(BUILD64_DIR)/app/$(1)/$(1).elf : $(BUILD64_DIR)/app/$(1)/$(1).o $$(APP64_CRT_OBJS) $(APP64_DIR)/app64.ld
	$$(X64_CC) $$(APP64_LDFLAGS) -Wl,-Map=$(BUILD64_DIR)/app/$(1)/$(1).map -o $$@ $(BUILD64_DIR)/app/$(1)/$(1).o $$(APP64_CRT_OBJS)
endef

$(foreach app,$(APP64_NAMES),$(eval $(call APP64_RULES,$(app))))

# -- 이미지 만들기 --
# 부트 섹터, 2단계 로더, 커널은 예약 영역에 넣고 나머지는 파일로 넣는다.
# app=경로 꼴로 넘기면 그 이름이 파일 이름이 된다(한글 이름은 VFAT 긴 이름).
$(IMG64_FILE) : $(BOOT64_BIN) $(LOADER64_BIN) $(KERNEL64_BIN) $(FONT64_DIR)/H04.FNT $(APP64_TARGETS) $(PY64_FILES) $(MKFAT32)
	@$(MKDIR) $(IMG64_DIR)
	$(PYTHON) $(MKFAT32) $@ $(BOOT64_BIN) $(LOADER64_BIN) $(KERNEL64_BIN) $(FONT64_DIR)/H04.FNT $(foreach app,$(APP64_NAMES),$(app)=$(BUILD64_DIR)/app/$(app)/$(app).elf) $(foreach f,$(PY64_FILES),$(notdir $(f))=$(f))

# -- 명령 --
x86_64 : $(IMG64_FILE)

run64 : $(IMG64_FILE)
	$(QEMU) -drive file=$(IMG64_FILE),format=raw,if=ide -boot c -no-reboot -d int -m 512M

# 같은 이미지를 AHCI 경로로 띄운다. q35에는 레거시 IDE가 없어서 ATA PIO 대신
# src64/drivers/ahci64.c를 타게 된다. 저장 장치를 고쳤으면 둘 다 돌려 봐야 한다.
# 머꼬 병행 검사. 이미지 안의 머꼬와 호스트 CPython의 업스트림 머꼬를 같은
# 입력으로 돌려 견준다. --ahci로 전송 경로도 바꾼다.
parity64 : $(IMG64_FILE)
	$(PYTHON) $(TOOLPATH)/mowkow_parity.py

run64-ahci : $(IMG64_FILE)
	$(QEMU) -machine q35 -drive file=$(IMG64_FILE),format=raw,if=none,id=disk0 \
		-device ich9-ahci,id=ahci -device ide-hd,drive=disk0,bus=ahci.0 \
		-boot c -no-reboot -d int -m 512M

clean64 :
	$(DEL) $(BUILD64_DIR) $(IMG64_DIR)

# -MMD가 만든 헤더 의존성 파일. 없으면 그냥 넘어간다.
-include $(shell find $(BUILD64_DIR) -name '*.d' 2>/dev/null)
