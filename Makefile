# -- Path --
TOOLPATH = tools
INCPATH = tools/haribote
SRC_DIR = src
BUILD_DIR = build
IMG_DIR = img
APP_DIR = app
APP64_DIR = app64
SRC64_DIR = src64
BUILD64_DIR = build64
IMG64_DIR = img64

FONT_DIR = src/graphics/font

# -- Tools --
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

QEMU = qemu-system-x86_64
PYTHON ?= python3

X64_CC ?= x86_64-elf-gcc
X64_ASM ?= nasm
X64_LD ?= x86_64-elf-ld
X64_OBJCOPY ?= x86_64-elf-objcopy
X64_CFLAGS = -ffreestanding -mno-red-zone -fno-pic -fno-stack-protector -Wall -Wextra -Wa,--noexecstack -I$(SRC64_DIR)/include
APP64_CFLAGS = -ffreestanding -fno-pic -fno-pie -mno-red-zone -fno-stack-protector -fno-asynchronous-unwind-tables -fno-unwind-tables -nostdlib -Wall -Wextra -I$(APP64_DIR)/crt/include
APP64_LDFLAGS = -nostdlib -static -T $(APP64_DIR)/app64.ld
X64_ASMFLAGS = -f elf64
X64_BOOT_ASMFLAGS = -f bin
X64_LDFLAGS = -nostdlib -T $(SRC64_DIR)/kernel/kernel64.ld
STAGE2_64_SECTORS = 16
KERNEL64_SECTORS = 80
FAT12_64_RESERVED_SECTORS = $(shell expr 1 + $(STAGE2_64_SECTORS))
FAT12_64_ROOT_LBA = $(shell expr $(FAT12_64_RESERVED_SECTORS) + 18)
FAT12_64_DATA_LBA = $(shell expr $(FAT12_64_ROOT_LBA) + 14)
KERNEL64_LBA = $(FAT12_64_DATA_LBA)

COPY = cp
DEL = rm -rf
MKDIR = mkdir -p

# -- Srouce Discovery --
KERNEL_SRCS = $(wildcard $(SRC_DIR)/kernel/*.c)
DRIVERS_SRCS = $(wildcard $(SRC_DIR)/drivers/*.c)
LIB_SRCS = $(wildcard $(SRC_DIR)/lib/*.c)

KERNEL_OBJS = $(patsubst $(SRC_DIR)/kernel/%.c, $(BUILD_DIR)/kernel/%.obj, $(KERNEL_SRCS))
DRIVERS_OBJS = $(patsubst $(SRC_DIR)/drivers/%.c, $(BUILD_DIR)/drivers/%.obj, $(DRIVERS_SRCS))
LIB_OBJS = $(patsubst $(SRC_DIR)/lib/%.c, $(BUILD_DIR)/lib/%.obj, $(LIB_SRCS))

NASKFUNC_OBJ = $(BUILD_DIR)/kernel/naskfunc.obj
FONT_OBJ = $(BUILD_DIR)/graphics/font/hankaku.obj

ALL_OBJS = $(KERNEL_OBJS) $(DRIVERS_OBJS) $(LIB_OBJS) $(NASKFUNC_OBJ) $(FONT_OBJ)

KERNEL64_C_SRCS = $(wildcard $(SRC64_DIR)/kernel/*.c) $(wildcard $(SRC64_DIR)/drivers/*.c) $(wildcard $(SRC64_DIR)/lib/*.c)
KERNEL64_ASM_SRCS = $(wildcard $(SRC64_DIR)/kernel/*.asm)
KERNEL64_C_OBJS = $(patsubst $(SRC64_DIR)/%.c, $(BUILD64_DIR)/%.o, $(KERNEL64_C_SRCS))
KERNEL64_ASM_OBJS = $(patsubst $(SRC64_DIR)/kernel/%.asm, $(BUILD64_DIR)/kernel/%.o, $(KERNEL64_ASM_SRCS))
KERNEL64_OBJS = $(KERNEL64_ASM_OBJS) $(KERNEL64_C_OBJS)
KERNEL64_ELF = $(BUILD64_DIR)/kernel/kernel64.elf
KERNEL64_BIN = $(BUILD64_DIR)/kernel/kernel64.bin
BOOT64_BIN = $(BUILD64_DIR)/boot/boot64.bin
LOADER64_BIN = $(BUILD64_DIR)/boot/loader64.bin
IMG64_FILE = $(IMG64_DIR)/mowkow64.img

# -- Application Discovery --
APP_DIRS = $(wildcard $(APP_DIR)/*/)
APP_NAMES = $(notdir $(patsubst %/,%,$(APP_DIRS)))
APPS = $(filter-out api include, $(APP_NAMES))

API_LIB = $(BUILD_DIR)/app/api/apilib.lib
APP_TARGETS = $(foreach app, $(APPS), $(BUILD_DIR)/app/$(app)/$(app).hrb)

APP64_DIRS = $(wildcard $(APP64_DIR)/*/)
APP64_NAMES = $(filter-out crt,$(notdir $(patsubst %/,%,$(APP64_DIRS))))
APP64_TARGETS = $(foreach app, $(APP64_NAMES), $(BUILD64_DIR)/app/$(app)/$(app).elf)

# -- Build Rule --
.PHONY : default clean run info x86_64 run64 clean64

default : $(IMG_DIR)/haribote.img

# Bootloader Build
$(BUILD_DIR)/boot/ipl.bin : $(SRC_DIR)/boot/ipl.nas
	@$(MKDIR) $(BUILD_DIR)/boot
	$(NASK) $< $@ $(subst .bin,.lst,$@)

$(BUILD_DIR)/boot/asmhead.bin : $(SRC_DIR)/boot/asmhead.nas
	@$(MKDIR) $(BUILD_DIR)/boot
	$(NASK) $< $@ $(subst .bin,.lst,$@)

# $(BUILD_DIR)/boot/ipl.bin : ASM/ipl.bin
# 	@$(MKDIR) $(BUILD_DIR)/boot
# 	$(COPY) $< $@

# $(BUILD_DIR)/boot/asmhead.bin : ASM/asmhead.bin
# 	@$(MKDIR) $(BUILD_DIR)/boot
# 	$(COPY) $< $@

# Kernel & Drivers Build (c -> obj)
$(BUILD_DIR)/%.obj : $(SRC_DIR)/%.c
	@$(MKDIR) $(dir $@)
	$(CC1) -o $(basename $@).gas $<
	$(GAS2NASK) $(basename $@).gas $(basename $@).nas
	$(NASK) $(basename $@).nas $@ $(basename $@).lst

# Naskfunc Build
$(BUILD_DIR)/kernel/naskfunc.obj : $(SRC_DIR)/kernel/naskfunc.nas
	@$(MKDIR) $(BUILD_DIR)/kernel
	$(NASK) $< $@ $(subst .obj,.lst,$@)

# Font Build
$(BUILD_DIR)/graphics/font/hankaku.bin : $(SRC_DIR)/graphics/font/hankaku.txt
	@$(MKDIR) $(BUILD_DIR)/graphics/font
	$(MAKEFONT) $< $@

$(BUILD_DIR)/graphics/font/hankaku.obj : $(BUILD_DIR)/graphics/font/hankaku.bin
	$(BIN2OBJ) $< $@ _hankaku

# Kernel Linking (bim -> hrb)
$(BUILD_DIR)/kernel/bootpack.bim : $(ALL_OBJS)
	$(OBJ2BIM) @$(RULEFILE) out:$@ stack:3136k map:$(BUILD_DIR)/kernel/bootpack.map $(ALL_OBJS)

$(BUILD_DIR)/kernel/bootpack.hrb : $(BUILD_DIR)/kernel/bootpack.bim
	$(BIM2HRB) $< $@ 0

# System Link (asmhead + bootpack)
$(BUILD_DIR)/haribote.sys : $(BUILD_DIR)/boot/asmhead.bin $(BUILD_DIR)/kernel/bootpack.hrb
	cat $^ > $@

# API Library Build
API_SRCS = $(wildcard $(APP_DIR)/api/*.nas)
API_OBJS = $(patsubst $(APP_DIR)/api/%.nas, $(BUILD_DIR)/app/api/%.obj, $(API_SRCS))

$(BUILD_DIR)/app/api/%.obj : $(APP_DIR)/api/%.nas
	@$(MKDIR) $(dir $@)
	$(NASK) $< $@ $(subst .obj,.lst,$@)

$(API_LIB) : $(API_OBJS)
	@$(MKDIR) $(dir $@)
	$(GOLIB) $(API_OBJS) out:$@

# Application Build
.PHONY : $(APPS)

$(APP_TARGETS) : $(API_LIB)
	@mkdir -p $(dir $@)
	$(MAKE) -C app/$(notdir $(patsubst %/,%,$(dir $@)))
	
# Image Creation
$(IMG_DIR)/haribote.img : $(BUILD_DIR)/boot/ipl.bin $(BUILD_DIR)/haribote.sys $(APP_TARGETS)
	@$(MKDIR) $(IMG_DIR)
	$(EDIMG) imgin:$(TOOLPATH)/fdimg0at.tek \
		wbinimg src:$(BUILD_DIR)/boot/ipl.bin len:512 from:0 to:0 \
		copy from:$(BUILD_DIR)/haribote.sys to:@: \
		$(foreach app, $(APP_TARGETS), copy from:$(app) to:@: ) \
		copy from:$(FONT_DIR)/E2.FNT to:@: \
		copy from:$(FONT_DIR)/H04.FNT to:@: \
		copy from:testfiles/gcd_lcm.mk to:@: \
		copy from:app/머꼬/library.scm to:@: \
		copy from:testfiles/sanjini.jpg to:@: \
		imgout:$@

# Commands
x86_64: $(IMG64_FILE)

$(BUILD64_DIR)/boot/boot64.bin : $(SRC64_DIR)/boot/boot64.asm Makefile
	@$(MKDIR) $(dir $@)
	$(X64_ASM) $(X64_BOOT_ASMFLAGS) -DSTAGE2_SECTORS=$(STAGE2_64_SECTORS) $< -o $@

$(BUILD64_DIR)/boot/loader64.bin : $(SRC64_DIR)/boot/loader64.asm Makefile
	@$(MKDIR) $(dir $@)
	$(X64_ASM) $(X64_BOOT_ASMFLAGS) -DKERNEL_LBA=$(KERNEL64_LBA) -DKERNEL_SECTORS=$(KERNEL64_SECTORS) $< -o $@

$(BUILD64_DIR)/%.o : $(SRC64_DIR)/%.c
	@$(MKDIR) $(dir $@)
	$(X64_CC) $(X64_CFLAGS) -c $< -o $@

$(BUILD64_DIR)/kernel/%.o : $(SRC64_DIR)/kernel/%.asm
	@$(MKDIR) $(dir $@)
	$(X64_ASM) $(X64_ASMFLAGS) $< -o $@

$(KERNEL64_ELF) : $(KERNEL64_OBJS) $(SRC64_DIR)/kernel/kernel64.ld
	@$(MKDIR) $(dir $@)
	$(X64_LD) $(X64_LDFLAGS) -o $@ $(KERNEL64_OBJS)

$(KERNEL64_BIN) : $(KERNEL64_ELF)
	$(X64_OBJCOPY) -O binary $< $@

$(BUILD64_DIR)/app/crt/%.o : $(APP64_DIR)/crt/%.S
	@$(MKDIR) $(dir $@)
	$(X64_CC) $(APP64_CFLAGS) -c $< -o $@

$(BUILD64_DIR)/app/crt/%.o : $(APP64_DIR)/crt/%.c
	@$(MKDIR) $(dir $@)
	$(X64_CC) $(APP64_CFLAGS) -c $< -o $@

APP64_CRT_OBJS = $(BUILD64_DIR)/app/crt/crt0.o $(BUILD64_DIR)/app/crt/syscall.o $(BUILD64_DIR)/app/crt/string.o $(BUILD64_DIR)/app/crt/malloc.o

define APP64_RULES
$(BUILD64_DIR)/app/$(1)/$(1).o : $(APP64_DIR)/$(1)/$(1).c
	@$$(MKDIR) $$(dir $$@)
	$$(X64_CC) $$(APP64_CFLAGS) -c $$< -o $$@

$(BUILD64_DIR)/app/$(1)/$(1).elf : $(BUILD64_DIR)/app/$(1)/$(1).o $$(APP64_CRT_OBJS) $(APP64_DIR)/app64.ld
	$$(X64_CC) $$(APP64_LDFLAGS) -Wl,-Map=$(BUILD64_DIR)/app/$(1)/$(1).map -o $$@ $(BUILD64_DIR)/app/$(1)/$(1).o $$(APP64_CRT_OBJS)
endef

$(foreach app,$(APP64_NAMES),$(eval $(call APP64_RULES,$(app))))

$(IMG64_FILE) : $(BOOT64_BIN) $(LOADER64_BIN) $(KERNEL64_BIN) $(FONT_DIR)/H04.FNT $(APP64_TARGETS)
	@$(MKDIR) $(IMG64_DIR)
	$(PYTHON) $(TOOLPATH)/mkfat12_64.py $@ $(BOOT64_BIN) $(LOADER64_BIN) $(KERNEL64_BIN) $(FONT_DIR)/H04.FNT $(foreach app,$(APP64_NAMES),$(app)=$(BUILD64_DIR)/app/$(app)/$(app).elf)

run64: $(IMG64_FILE)
	$(QEMU) -drive file=$(IMG64_FILE),format=raw,if=ide -boot c -no-reboot -d int -m 512M

clean64:
	$(DEL) $(BUILD64_DIR) $(IMG64_DIR)

run: $(IMG_DIR)/haribote.img
	$(QEMU) -fda $(IMG_DIR)/haribote.img -no-reboot -d int -m 512M

clean:
	$(DEL) $(BUILD_DIR) $(IMG_DIR)

iso : $(IMG_DIR)/haribote.img
	$(FDIMG2ISO) $(TOOLPATH)/makeiso/fdimg2iso.dat $(IMG_DIR)/haribote.img $(IMG_DIR)/haribote.iso

info:
	@echo "[Kernel Sources] $(KERNEL_SRCS)"
	@echo "[Driver Sources] $(DRIVERS_SRCS)"
	@echo "[Library Sources] $(LIB_SRCS)"
	@echo "[APPS] $(APPS)"
	@echo "[APP64] $(APP64_NAMES)"
