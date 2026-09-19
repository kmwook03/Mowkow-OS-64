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

A64_CFLAGS = -O2 -ffreestanding -nostdlib -mgeneral-regs-only \
	-mcpu=cortex-a76 -mstrict-align -fno-stack-protector -fno-pic \
	-fno-asynchronous-unwind-tables -fno-unwind-tables -Wall -Wextra \
	-MMD -MP -I$(SRC64_DIR)/include
A64_LDFLAGS = -nostdlib -T $(A64_ARCH_DIR)/kernel64.ld

A64_SRCS = $(A64_ARCH_DIR)/boot64.S $(A64_ARCH_DIR)/gioaon64.c
A64_OBJS = $(patsubst $(A64_ARCH_DIR)/%.S,$(A64_BUILD_DIR)/%.o,\
	$(filter %.S,$(A64_SRCS))) \
	$(patsubst $(A64_ARCH_DIR)/%.c,$(A64_BUILD_DIR)/%.o,\
	$(filter %.c,$(A64_SRCS)))

$(A64_BUILD_DIR)/%.o : $(A64_ARCH_DIR)/%.S
	@$(MKDIR) $(dir $@)
	$(A64_CC) $(A64_CFLAGS) -c $< -o $@

$(A64_BUILD_DIR)/%.o : $(A64_ARCH_DIR)/%.c
	@$(MKDIR) $(dir $@)
	$(A64_CC) $(A64_CFLAGS) -c $< -o $@

$(A64_ELF) : $(A64_OBJS) $(A64_ARCH_DIR)/kernel64.ld
	@$(MKDIR) $(dir $@)
	$(A64_LD) $(A64_LDFLAGS) -Map=$(A64_BUILD_DIR)/kernel64.map -o $@ $(A64_OBJS)

$(A64_IMAGE) : $(A64_ELF)
	$(A64_OBJCOPY) -O binary $< $@

aarch64 : $(A64_IMAGE)

clean-a64 :
	$(DEL) $(A64_BUILD_DIR)

-include $(A64_OBJS:.o=.d)
