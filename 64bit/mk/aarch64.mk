# ---------------------------------------------------------------------------
# Raspberry Pi 5 AArch64 백엔드
#
# M1은 펌웨어가 0x80000에 직접 올리는 flat image만 만든다. 아직 공용 커널은
# 링크하지 않으며, EL2 -> EL1 전환과 GIO_AON ACT LED가 유일한 실행 경로다.
# ---------------------------------------------------------------------------

A64_CC ?= aarch64-none-elf-gcc
A64_LD ?= aarch64-none-elf-ld
A64_OBJCOPY ?= aarch64-none-elf-objcopy
A64_NM ?= aarch64-none-elf-nm

A64_ARCH_DIR = $(SRC64_DIR)/arch/aarch64
A64_BUILD_DIR = $(BUILD64_DIR)/aarch64
A64_ELF = $(A64_BUILD_DIR)/kernel64.elf
A64_IMAGE = $(A64_BUILD_DIR)/kernel_2712.img
A64_APP_BUILD_DIR = $(BUILD64_DIR)/aarch64-app
A64_APP_TARGETS = $(foreach app,$(APP64_NAMES),$(A64_APP_BUILD_DIR)/$(app).elf)
A64_APP_CRT_OBJS = $(A64_APP_BUILD_DIR)/crt0.o \
	$(A64_APP_BUILD_DIR)/syscall.o $(A64_APP_BUILD_DIR)/string.o
A64_APP_EXTRA_OBJS_mtest = $(A64_APP_BUILD_DIR)/malloc.o
A64_APP_EXTRA_OBJS_나노 = $(A64_APP_BUILD_DIR)/malloc.o
A64_BOOT_DIR = $(BUILD64_DIR)/aarch64-boot
A64_BOOT_STAGER = $(TOOL64PATH)/stage_aarch64_boot.py
A64_PI5_CONFIG = 64bit/board/pi5/config.txt
A64_BOOT_STAGE_SPECS = config.txt=$(A64_PI5_CONFIG) \
	kernel_2712.img=$(A64_IMAGE) H04.FNT=$(FONT_DIR)/H04.FNT \
	HELLO.ELF=$(A64_APP_BUILD_DIR)/hello.elf \
	CAT.ELF=$(A64_APP_BUILD_DIR)/cat.elf \
	KTEST.ELF=$(A64_APP_BUILD_DIR)/ktest.elf \
	MTEST.ELF=$(A64_APP_BUILD_DIR)/mtest.elf \
	WTEST.ELF=$(A64_APP_BUILD_DIR)/wtest.elf \
	나노.ELF=$(A64_APP_BUILD_DIR)/나노.elf \
	smoke.py=$(PY64_DIR)/smoke.py float_smoke.py=$(PY64_DIR)/float_smoke.py \
	mowkow.py=$(PY64_DIR)/머꼬/mowkow.py _compat.py=$(PY64_DIR)/머꼬/_compat.py \
	_data.py=$(PY64_DIR)/머꼬/_data.py _error.py=$(PY64_DIR)/머꼬/_error.py \
	_eval.py=$(PY64_DIR)/머꼬/_eval.py _parse.py=$(PY64_DIR)/머꼬/_parse.py \
	library_kor.scm=$(PY64_DIR)/머꼬/library_kor.scm \
	add.mk=$(PY64_DIR)/머꼬/add.mk deep.mk=$(PY64_DIR)/머꼬/deep.mk \
	gcd_lcm.mk=$(PY64_DIR)/머꼬/gcd_lcm.mk greet.mk=$(PY64_DIR)/머꼬/greet.mk \
	hello.mk=$(PY64_DIR)/머꼬/hello.mk lib_all.mk=$(PY64_DIR)/머꼬/lib_all.mk
A64_BOOT_STAGE_INPUTS = $(foreach spec,$(A64_BOOT_STAGE_SPECS),\
	$(word 2,$(subst =, ,$(spec))))

A64_BASE_CFLAGS = -O2 -ffreestanding -nostdlib -mgeneral-regs-only \
	-mcpu=cortex-a76 -mstrict-align -fno-stack-protector -fno-pic \
	-fno-asynchronous-unwind-tables -fno-unwind-tables \
	-ffunction-sections -fdata-sections -MMD -MP -I$(SRC64_DIR)/include
A64_CFLAGS = $(A64_BASE_CFLAGS) $(WARN64_CFLAGS)
A64_LDFLAGS = -nostdlib --gc-sections -T $(A64_ARCH_DIR)/kernel64.ld

A64_SRCS = $(A64_ARCH_DIR)/boot64.S $(A64_ARCH_DIR)/vectors64.S \
	$(A64_ARCH_DIR)/user64.S $(A64_ARCH_DIR)/fptest64.S \
	$(A64_ARCH_DIR)/font64.S \
	$(A64_ARCH_DIR)/gioaon64.c $(A64_ARCH_DIR)/mmu64.c \
	$(A64_ARCH_DIR)/mailbox64.c $(A64_ARCH_DIR)/fb64.c \
	$(A64_ARCH_DIR)/exception64.c $(A64_ARCH_DIR)/gic64.c \
	$(A64_ARCH_DIR)/gtimer64.c $(A64_ARCH_DIR)/sched64.c \
	$(A64_ARCH_DIR)/sdhci64.c $(A64_ARCH_DIR)/pcie64.c \
	$(A64_ARCH_DIR)/rp164.c $(A64_ARCH_DIR)/xhci64.c \
	$(A64_ARCH_DIR)/usbhid64.c $(A64_ARCH_DIR)/keyboard64.c \
	$(COMMON64_C_SRCS)
A64_OBJS = $(patsubst $(A64_ARCH_DIR)/%.S,$(A64_BUILD_DIR)/%.o,\
	$(filter %.S,$(A64_SRCS))) \
	$(patsubst $(A64_ARCH_DIR)/%.c,$(A64_BUILD_DIR)/%.o,\
	$(filter $(A64_ARCH_DIR)/%.c,$(A64_SRCS))) \
	$(patsubst $(SRC64_DIR)/lib/%.c,$(A64_BUILD_DIR)/lib/%.o,\
	$(filter $(SRC64_DIR)/lib/%.c,$(A64_SRCS))) \
	$(patsubst $(SRC64_DIR)/kernel/%.c,$(A64_BUILD_DIR)/kernel/%.o,\
	$(filter $(SRC64_DIR)/kernel/%.c,$(A64_SRCS))) \
	$(patsubst $(SRC64_DIR)/drivers/%.c,$(A64_BUILD_DIR)/drivers/%.o,\
	$(filter $(SRC64_DIR)/drivers/%.c,$(A64_SRCS))) \
	$(A64_MPY_LINK_OBJS)

$(A64_BUILD_DIR)/%.o : $(A64_ARCH_DIR)/%.S
	@$(MKDIR) $(dir $@)
	$(A64_CC) $(A64_CFLAGS) -c $< -o $@

$(A64_BUILD_DIR)/%.o : $(A64_ARCH_DIR)/%.c
	@$(MKDIR) $(dir $@)
	$(A64_CC) $(A64_CFLAGS) -c $< -o $@

$(A64_BUILD_DIR)/lib/%.o : $(SRC64_DIR)/lib/%.c
	@$(MKDIR) $(dir $@)
	$(A64_CC) $(A64_CFLAGS) -c $< -o $@

$(A64_BUILD_DIR)/kernel/%.o : $(SRC64_DIR)/kernel/%.c
	@$(MKDIR) $(dir $@)
	$(A64_CC) $(A64_CFLAGS) -c $< -o $@

$(A64_BUILD_DIR)/drivers/%.o : $(SRC64_DIR)/drivers/%.c
	@$(MKDIR) $(dir $@)
	$(A64_CC) $(A64_CFLAGS) -c $< -o $@

$(A64_ELF) : $(A64_OBJS) $(A64_ARCH_DIR)/kernel64.ld
	@$(MKDIR) $(dir $@)
	$(A64_LD) $(A64_LDFLAGS) -Map=$(A64_BUILD_DIR)/kernel64.map -o $@ $(A64_OBJS)

$(A64_IMAGE) : $(A64_ELF)
	$(A64_OBJCOPY) -O binary $< $@

A64_APP_BASE_CFLAGS = -O2 -ffreestanding -nostdlib -mgeneral-regs-only \
	-fno-stack-protector -fno-pic -fno-pie -fno-asynchronous-unwind-tables \
	-fno-unwind-tables -I$(APP64_DIR)/crt/include
A64_APP_CFLAGS = $(A64_APP_BASE_CFLAGS) $(WARN64_CFLAGS)

$(A64_APP_BUILD_DIR)/crt0.o : $(APP64_DIR)/crt/crt0_aarch64.S
	@$(MKDIR) $(dir $@)
	$(A64_CC) $(A64_APP_CFLAGS) -c $< -o $@

$(A64_APP_BUILD_DIR)/syscall.o : $(APP64_DIR)/crt/syscall.c
	@$(MKDIR) $(dir $@)
	$(A64_CC) $(A64_APP_CFLAGS) -c $< -o $@

$(A64_APP_BUILD_DIR)/string.o : $(APP64_DIR)/crt/string.c
	@$(MKDIR) $(dir $@)
	$(A64_CC) $(A64_APP_CFLAGS) -c $< -o $@

$(A64_APP_BUILD_DIR)/malloc.o : $(APP64_DIR)/crt/malloc.c
	@$(MKDIR) $(dir $@)
	$(A64_CC) $(A64_APP_CFLAGS) -c $< -o $@

define A64_APP_RULES
$(A64_APP_BUILD_DIR)/$(1).o : $(APP64_DIR)/$(1)/$(1).c
	@$$(MKDIR) $$(dir $$@)
	$$(A64_CC) $$(A64_APP_CFLAGS) -c $$< -o $$@

$(A64_APP_BUILD_DIR)/$(1).elf : $(A64_APP_BUILD_DIR)/$(1).o \
	$$(A64_APP_CRT_OBJS) $$(A64_APP_EXTRA_OBJS_$(1)) \
	$(APP64_DIR)/app64.ld
	$$(A64_CC) -nostdlib -static -T $(APP64_DIR)/app64.ld \
		-Wl,-Map=$(A64_APP_BUILD_DIR)/$(1).map -o $$@ \
		$(A64_APP_BUILD_DIR)/$(1).o $$(A64_APP_CRT_OBJS) \
		$$(A64_APP_EXTRA_OBJS_$(1))
endef

$(foreach app,$(APP64_NAMES),$(eval $(call A64_APP_RULES,$(app))))

aarch64-stage : aarch64-mpy-foundation $(A64_BOOT_STAGE_INPUTS) $(A64_BOOT_STAGER)
	$(PYTHON) $(A64_BOOT_STAGER) $(A64_BOOT_DIR) $(A64_BOOT_STAGE_SPECS)

aarch64 : aarch64-stage

clean-a64 :
	$(DEL) $(A64_BUILD_DIR) $(A64_APP_BUILD_DIR) \
		$(A64_MPY_GEN_DIR) $(A64_MPY_OBJS_DIR) $(A64_BOOT_DIR) \
		$(A64_BOOT_DIR).tmp

-include $(A64_OBJS:.o=.d)
