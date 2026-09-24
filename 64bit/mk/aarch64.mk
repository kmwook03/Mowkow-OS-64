# ---------------------------------------------------------------------------
# Raspberry Pi 5 AArch64 백엔드
#
# M1은 펌웨어가 0x80000에 직접 올리는 flat image만 만든다. 아직 공용 커널은
# 링크하지 않으며, EL2 -> EL1 전환과 GIO_AON ACT LED가 유일한 실행 경로다.
# ---------------------------------------------------------------------------

A64_CC ?= aarch64-none-elf-gcc
A64_LD ?= aarch64-none-elf-ld
A64_OBJCOPY ?= aarch64-none-elf-objcopy

A64_ARCH_DIR = $(SRC64_DIR)/arch/aarch64
A64_BUILD_DIR = $(BUILD64_DIR)/aarch64
A64_ELF = $(A64_BUILD_DIR)/kernel64.elf
A64_IMAGE = $(A64_BUILD_DIR)/kernel_2712.img
A64_APP_BUILD_DIR = $(BUILD64_DIR)/aarch64-app
A64_HELLO_ELF = $(A64_APP_BUILD_DIR)/hello.elf
A64_CAT_ELF = $(A64_APP_BUILD_DIR)/cat.elf
A64_KTEST_ELF = $(A64_APP_BUILD_DIR)/ktest.elf
A64_MTEST_ELF = $(A64_APP_BUILD_DIR)/mtest.elf
A64_WTEST_ELF = $(A64_APP_BUILD_DIR)/wtest.elf
A64_NANO_ELF = $(A64_APP_BUILD_DIR)/나노.elf

A64_CFLAGS = -O2 -ffreestanding -nostdlib -mgeneral-regs-only \
	-mcpu=cortex-a76 -mstrict-align -fno-stack-protector -fno-pic \
	-fno-asynchronous-unwind-tables -fno-unwind-tables -Wall -Wextra \
	-ffunction-sections -fdata-sections -MMD -MP -I$(SRC64_DIR)/include
A64_LDFLAGS = -nostdlib --gc-sections -T $(A64_ARCH_DIR)/kernel64.ld

A64_SRCS = $(A64_ARCH_DIR)/boot64.S $(A64_ARCH_DIR)/vectors64.S \
	$(A64_ARCH_DIR)/user64.S \
	$(A64_ARCH_DIR)/font64.S \
	$(A64_ARCH_DIR)/gioaon64.c $(A64_ARCH_DIR)/mmu64.c \
	$(A64_ARCH_DIR)/mailbox64.c $(A64_ARCH_DIR)/fb64.c \
	$(A64_ARCH_DIR)/exception64.c $(A64_ARCH_DIR)/gic64.c \
	$(A64_ARCH_DIR)/gtimer64.c $(A64_ARCH_DIR)/sched64.c \
	$(A64_ARCH_DIR)/sdhci64.c $(A64_ARCH_DIR)/pcie64.c \
	$(A64_ARCH_DIR)/rp164.c $(A64_ARCH_DIR)/xhci64.c \
	$(A64_ARCH_DIR)/usbhid64.c $(A64_ARCH_DIR)/keyboard64.c \
	$(SRC64_DIR)/drivers/block64.c $(SRC64_DIR)/drivers/graphic64.c \
	$(SRC64_DIR)/kernel/cache64.c $(SRC64_DIR)/kernel/fd64.c \
	$(SRC64_DIR)/kernel/sheet64.c $(SRC64_DIR)/kernel/window64.c \
	$(SRC64_DIR)/kernel/gui64.c $(SRC64_DIR)/kernel/console64.c \
	$(SRC64_DIR)/kernel/elf64_loader.c $(SRC64_DIR)/kernel/process64.c \
	$(SRC64_DIR)/kernel/syscall64.c \
	$(SRC64_DIR)/kernel/memory64.c $(SRC64_DIR)/kernel/mtask64.c \
	$(SRC64_DIR)/lib/fifo64.c $(SRC64_DIR)/lib/hangul64.c \
	$(SRC64_DIR)/lib/keymap64.c \
	$(SRC64_DIR)/lib/utf864.c \
	$(SRC64_DIR)/lib/kstring64.c
A64_OBJS = $(patsubst $(A64_ARCH_DIR)/%.S,$(A64_BUILD_DIR)/%.o,\
	$(filter %.S,$(A64_SRCS))) \
	$(patsubst $(A64_ARCH_DIR)/%.c,$(A64_BUILD_DIR)/%.o,\
	$(filter $(A64_ARCH_DIR)/%.c,$(A64_SRCS))) \
	$(patsubst $(SRC64_DIR)/lib/%.c,$(A64_BUILD_DIR)/lib/%.o,\
	$(filter $(SRC64_DIR)/lib/%.c,$(A64_SRCS))) \
	$(patsubst $(SRC64_DIR)/kernel/%.c,$(A64_BUILD_DIR)/kernel/%.o,\
	$(filter $(SRC64_DIR)/kernel/%.c,$(A64_SRCS))) \
	$(patsubst $(SRC64_DIR)/drivers/%.c,$(A64_BUILD_DIR)/drivers/%.o,\
	$(filter $(SRC64_DIR)/drivers/%.c,$(A64_SRCS)))

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

A64_APP_CFLAGS = -O2 -ffreestanding -nostdlib -mgeneral-regs-only \
	-fno-stack-protector -fno-pic -fno-pie -fno-asynchronous-unwind-tables \
	-fno-unwind-tables -Wall -Wextra -I$(APP64_DIR)/crt/include

$(A64_APP_BUILD_DIR)/hello.o : $(APP64_DIR)/hello/hello.c
	@$(MKDIR) $(dir $@)
	$(A64_CC) $(A64_APP_CFLAGS) -c $< -o $@

$(A64_APP_BUILD_DIR)/cat.o : $(APP64_DIR)/cat/cat.c
	@$(MKDIR) $(dir $@)
	$(A64_CC) $(A64_APP_CFLAGS) -c $< -o $@

$(A64_APP_BUILD_DIR)/ktest.o : $(APP64_DIR)/ktest/ktest.c
	@$(MKDIR) $(dir $@)
	$(A64_CC) $(A64_APP_CFLAGS) -c $< -o $@

$(A64_APP_BUILD_DIR)/mtest.o : $(APP64_DIR)/mtest/mtest.c
	@$(MKDIR) $(dir $@)
	$(A64_CC) $(A64_APP_CFLAGS) -c $< -o $@

$(A64_APP_BUILD_DIR)/wtest.o : $(APP64_DIR)/wtest/wtest.c
	@$(MKDIR) $(dir $@)
	$(A64_CC) $(A64_APP_CFLAGS) -c $< -o $@

$(A64_APP_BUILD_DIR)/나노.o : $(APP64_DIR)/나노/나노.c
	@$(MKDIR) $(dir $@)
	$(A64_CC) $(A64_APP_CFLAGS) -c $< -o $@

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

$(A64_HELLO_ELF) : $(A64_APP_BUILD_DIR)/hello.o \
	$(A64_APP_BUILD_DIR)/crt0.o $(A64_APP_BUILD_DIR)/syscall.o \
	$(A64_APP_BUILD_DIR)/string.o $(APP64_DIR)/app64.ld
	$(A64_CC) -nostdlib -static -T $(APP64_DIR)/app64.ld \
		-Wl,-Map=$(A64_APP_BUILD_DIR)/hello.map -o $@ \
		$(A64_APP_BUILD_DIR)/hello.o $(A64_APP_BUILD_DIR)/crt0.o \
		$(A64_APP_BUILD_DIR)/syscall.o $(A64_APP_BUILD_DIR)/string.o

$(A64_CAT_ELF) : $(A64_APP_BUILD_DIR)/cat.o \
	$(A64_APP_BUILD_DIR)/crt0.o $(A64_APP_BUILD_DIR)/syscall.o \
	$(A64_APP_BUILD_DIR)/string.o $(APP64_DIR)/app64.ld
	$(A64_CC) -nostdlib -static -T $(APP64_DIR)/app64.ld \
		-Wl,-Map=$(A64_APP_BUILD_DIR)/cat.map -o $@ \
		$(A64_APP_BUILD_DIR)/cat.o $(A64_APP_BUILD_DIR)/crt0.o \
		$(A64_APP_BUILD_DIR)/syscall.o $(A64_APP_BUILD_DIR)/string.o

$(A64_KTEST_ELF) : $(A64_APP_BUILD_DIR)/ktest.o \
	$(A64_APP_BUILD_DIR)/crt0.o $(A64_APP_BUILD_DIR)/syscall.o \
	$(A64_APP_BUILD_DIR)/string.o $(APP64_DIR)/app64.ld
	$(A64_CC) -nostdlib -static -T $(APP64_DIR)/app64.ld \
		-Wl,-Map=$(A64_APP_BUILD_DIR)/ktest.map -o $@ \
		$(A64_APP_BUILD_DIR)/ktest.o $(A64_APP_BUILD_DIR)/crt0.o \
		$(A64_APP_BUILD_DIR)/syscall.o $(A64_APP_BUILD_DIR)/string.o

$(A64_MTEST_ELF) : $(A64_APP_BUILD_DIR)/mtest.o \
	$(A64_APP_BUILD_DIR)/crt0.o $(A64_APP_BUILD_DIR)/syscall.o \
	$(A64_APP_BUILD_DIR)/string.o $(A64_APP_BUILD_DIR)/malloc.o \
	$(APP64_DIR)/app64.ld
	$(A64_CC) -nostdlib -static -T $(APP64_DIR)/app64.ld \
		-Wl,-Map=$(A64_APP_BUILD_DIR)/mtest.map -o $@ \
		$(A64_APP_BUILD_DIR)/mtest.o $(A64_APP_BUILD_DIR)/crt0.o \
		$(A64_APP_BUILD_DIR)/syscall.o $(A64_APP_BUILD_DIR)/string.o \
		$(A64_APP_BUILD_DIR)/malloc.o

$(A64_WTEST_ELF) : $(A64_APP_BUILD_DIR)/wtest.o \
	$(A64_APP_BUILD_DIR)/crt0.o $(A64_APP_BUILD_DIR)/syscall.o \
	$(A64_APP_BUILD_DIR)/string.o $(APP64_DIR)/app64.ld
	$(A64_CC) -nostdlib -static -T $(APP64_DIR)/app64.ld \
		-Wl,-Map=$(A64_APP_BUILD_DIR)/wtest.map -o $@ \
		$(A64_APP_BUILD_DIR)/wtest.o $(A64_APP_BUILD_DIR)/crt0.o \
		$(A64_APP_BUILD_DIR)/syscall.o $(A64_APP_BUILD_DIR)/string.o

$(A64_NANO_ELF) : $(A64_APP_BUILD_DIR)/나노.o \
	$(A64_APP_BUILD_DIR)/crt0.o $(A64_APP_BUILD_DIR)/syscall.o \
	$(A64_APP_BUILD_DIR)/string.o $(A64_APP_BUILD_DIR)/malloc.o \
	$(APP64_DIR)/app64.ld
	$(A64_CC) -nostdlib -static -T $(APP64_DIR)/app64.ld \
		-Wl,-Map=$(A64_APP_BUILD_DIR)/나노.map -o $@ \
		$(A64_APP_BUILD_DIR)/나노.o $(A64_APP_BUILD_DIR)/crt0.o \
		$(A64_APP_BUILD_DIR)/syscall.o $(A64_APP_BUILD_DIR)/string.o \
		$(A64_APP_BUILD_DIR)/malloc.o

aarch64 : $(A64_IMAGE) $(A64_HELLO_ELF) $(A64_CAT_ELF) $(A64_KTEST_ELF) \
	$(A64_MTEST_ELF) $(A64_WTEST_ELF) $(A64_NANO_ELF)

clean-a64 :
	$(DEL) $(A64_BUILD_DIR) $(A64_APP_BUILD_DIR)

-include $(A64_OBJS:.o=.d)
