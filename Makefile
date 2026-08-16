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
MPY_DIR = third_party/micropython
MPY_PY_DIR = $(MPY_DIR)/py

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
# -MMD -MP: without header dependencies a change to e.g. src64/include/fd64.h
# leaves stale objects linked against the old struct layout.
X64_DEPFLAGS = -MMD -MP
X64_CFLAGS = -ffreestanding -mno-red-zone -fno-pic -fno-stack-protector -Wall -Wextra -Wa,--noexecstack -I$(SRC64_DIR)/include
APP64_CFLAGS = -ffreestanding -fno-pic -fno-pie -mno-red-zone -fno-stack-protector -fno-asynchronous-unwind-tables -fno-unwind-tables -nostdlib -Wall -Wextra -I$(APP64_DIR)/crt/include
APP64_LDFLAGS = -nostdlib -static -T $(APP64_DIR)/app64.ld
X64_ASMFLAGS = -f elf64
X64_BOOT_ASMFLAGS = -f bin
X64_LDFLAGS = -nostdlib -T $(SRC64_DIR)/kernel/kernel64.ld
MKFAT32 = $(TOOLPATH)/mkfat32_64.py
# The FAT32 layout lives in one place, tools/mkfat32_64.py, and both this
# makefile and the BPB in boot64.asm read it from there: a silent mismatch
# between the image and the boot sector is unrecoverable at runtime.
$(foreach v,$(shell $(PYTHON) $(MKFAT32) --make-vars),$(eval $(v)))
KERNEL64_SECTORS = 800

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
# PY_CORE_O_BASENAME from third_party/micropython/py/py.mk, kept in sync by hand.
MPY_CORE_BASENAMES = \
	mpstate nlr nlrx86 nlrx64 nlrthumb nlraarch64 nlrmips nlrpowerpc nlrxtensa nlrrv32 \
	nlrrv64 nlrsetjmp malloc gc pystack qstr vstr mpprint unicode mpz reader lexer parse \
	scope compile emitcommon emitbc asmbase asmx64 emitnx64 asmx86 emitnx86 asmthumb \
	emitnthumb emitinlinethumb asmarm emitnarm asmxtensa emitnxtensa emitinlinextensa \
	emitnxtensawin asmrv32 emitnrv32 emitinlinerv32 emitndebug formatfloat parsenumbase \
	parsenum emitglue persistentcode runtime runtime_utils scheduler nativeglue pairheap \
	ringbuf cstack stackctrl argcheck warning profile map obj objarray objattrtuple objbool \
	objboundmeth objcell objclosure objcode objcomplex objdeque objdict objenumerate \
	objexcept objfilter objfloat objfun objgenerator objgetitemiter objint objint_longlong \
	objint_mpz objlist objmap objmodule objobject objpolyiter objproperty objnone \
	objnamedtuple objrange objreversed objringio objset objsingleton objslice objstr \
	objstrunicode objstringio objtemplate objtuple objtype objzip opmethods sequence stream \
	binary builtinimport builtinevex builtinhelp modarray modbuiltins modcollections modgc \
	modio modmath modcmath modmicropython modstring modstruct modsys moderrno modthread \
	modweakref vm bc showbc repl smallint frozenmod

MPY_CORE_SRCS = $(addprefix $(MPY_PY_DIR)/, $(addsuffix .c, $(MPY_CORE_BASENAMES)))
# shared/runtime/pyexec.c uses MP_QSTR_ tokens (e.g. MP_QSTR___file__), and
# shared/readline/readline.c has an MP_REGISTER_ROOT_POINTER() for its
# history buffer -- both need scanning too or qstrdefs.generated.h /
# genhdr/root_pointers.h won't have them.
MPY_QSTR_SRCS = $(filter-out $(MPY_PY_DIR)/nlr%.c, $(MPY_CORE_SRCS)) \
	$(MPY_DIR)/shared/runtime/pyexec.c \
	$(MPY_DIR)/shared/readline/readline.c
MPY_PORT_SRCS = $(wildcard $(SRC64_DIR)/mpport/*.c) $(wildcard $(SRC64_DIR)/mpport/libc/*.c)
MPY_GEN_DIR = $(BUILD64_DIR)/mpgen
# core files include these as "genhdr/xxx.h" (upstream's own convention),
# so the generated headers must live in a genhdr/ subdir under a -I root.
MPY_GENHDR_DIR = $(MPY_GEN_DIR)/genhdr
MPY_OBJS_DIR = $(BUILD64_DIR)/upy
MPY_OBJS = $(patsubst $(MPY_PY_DIR)/%.c, $(MPY_OBJS_DIR)/%.o, $(MPY_CORE_SRCS))
MPY_PORT_OBJS = $(patsubst $(SRC64_DIR)/mpport/%.c, $(MPY_OBJS_DIR)/mpport/%.o, $(MPY_PORT_SRCS))
# gc_helper_collect_regs_and_stack() -- portable register-capture + stack
# scan for gc_collect(), has a real x86_64 path needing no arch-specific
# asm file. Vendored, reused as-is rather than reimplemented.
# pyexec.c: shared/runtime/pyexec.c provides pyexec_friendly_repl(), the
# REPL loop itself (python_porting.md Stage 2); interrupt_char.c backs its
# Ctrl-C bookkeeping; readline.c is pyexec's line editor. All vendored as-is.
MPY_SHARED_OBJS = $(MPY_OBJS_DIR)/shared/runtime/gchelper_generic.o \
	$(MPY_OBJS_DIR)/shared/runtime/pyexec.o \
	$(MPY_OBJS_DIR)/shared/runtime/interrupt_char.o \
	$(MPY_OBJS_DIR)/shared/readline/readline.o
MPY_INCLUDES = -I$(SRC64_DIR)/mpport -I$(SRC64_DIR)/mpport/libc -I$(MPY_DIR) -I$(MPY_PY_DIR) -I$(MPY_GEN_DIR)
MPY_CFLAGS = $(X64_CFLAGS) $(MPY_INCLUDES)
MPY_QSTR_CFLAGS = $(MPY_CFLAGS) -DNO_QSTR

KERNEL64_OBJS = $(KERNEL64_ASM_OBJS) $(KERNEL64_C_OBJS) $(MPY_OBJS) $(MPY_PORT_OBJS) $(MPY_SHARED_OBJS)
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
.PHONY : default clean run info x86_64 run64 run64-ahci clean64

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

$(BUILD64_DIR)/boot/boot64.bin : $(SRC64_DIR)/boot/boot64.asm Makefile $(MKFAT32)
	@$(MKDIR) $(dir $@)
	$(X64_ASM) $(X64_BOOT_ASMFLAGS) -DSTAGE2_SECTORS=$(STAGE2_64_SECTORS) \
		-DSTAGE2_LBA=$(STAGE2_64_LBA) -DTOTAL_SECTORS=$(FAT32_64_TOTAL_SECTORS) \
		-DRESERVED_SECTORS=$(FAT32_64_RESERVED_SECTORS) -DFAT_COUNT=$(FAT32_64_FAT_COUNT) \
		-DSECTORS_PER_FAT=$(FAT32_64_SECTORS_PER_FAT) -DROOT_CLUSTER=$(FAT32_64_ROOT_CLUSTER) \
		-DFSINFO_LBA=$(FAT32_64_FSINFO_LBA) -DBACKUP_BOOT_LBA=$(FAT32_64_BACKUP_BOOT_LBA) \
		-DVOLUME_ID=$(FAT32_64_VOLUME_ID) $< -o $@

$(BUILD64_DIR)/boot/loader64.bin : $(SRC64_DIR)/boot/loader64.asm Makefile
	@$(MKDIR) $(dir $@)
	$(X64_ASM) $(X64_BOOT_ASMFLAGS) -DKERNEL_LBA=$(KERNEL64_LBA) -DKERNEL_SECTORS=$(KERNEL64_SECTORS) $< -o $@

$(BUILD64_DIR)/%.o : $(SRC64_DIR)/%.c
	@$(MKDIR) $(dir $@)
	$(X64_CC) $(X64_CFLAGS) $(X64_DEPFLAGS) -c $< -o $@

$(BUILD64_DIR)/kernel/%.o : $(SRC64_DIR)/kernel/%.asm
	@$(MKDIR) $(dir $@)
	$(X64_ASM) $(X64_ASMFLAGS) $< -o $@

$(KERNEL64_ELF) : $(KERNEL64_OBJS) $(SRC64_DIR)/kernel/kernel64.ld
	@$(MKDIR) $(dir $@)
	$(X64_LD) $(X64_LDFLAGS) -o $@ $(KERNEL64_OBJS)

$(KERNEL64_BIN) : $(KERNEL64_ELF)
	$(X64_OBJCOPY) -O binary $< $@
	@size=$$(stat -c%s $@); budget=$$(expr $(KERNEL64_SECTORS) \* 512); \
	if [ $$size -gt $$budget ]; then \
		echo "error: $@ is $$size bytes, exceeds KERNEL64_SECTORS budget of $$budget bytes"; \
		echo "raise KERNEL64_SECTORS in Makefile"; \
		exit 1; \
	fi

# -- MicroPython qstr/module/root-pointer codegen (python_porting.md Stage 1.4) --
$(MPY_GENHDR_DIR)/mpversion.h :
	@$(MKDIR) $(MPY_GENHDR_DIR)
	$(PYTHON) $(MPY_PY_DIR)/makeversionhdr.py $@

$(MPY_GEN_DIR)/qstr.i.last : $(MPY_QSTR_SRCS) $(SRC64_DIR)/mpport/mpconfigport.h | $(MPY_GENHDR_DIR)/mpversion.h
	@$(MKDIR) $(MPY_GEN_DIR)
	$(PYTHON) $(MPY_PY_DIR)/makeqstrdefs.py pp $(X64_CC) -E output $@ \
		cflags $(MPY_QSTR_CFLAGS) cxxflags \
		sources $(MPY_QSTR_SRCS) \
		dependencies $(SRC64_DIR)/mpport/mpconfigport.h \
		changed_sources $(MPY_QSTR_SRCS)

$(MPY_GEN_DIR)/qstr.split : $(MPY_GEN_DIR)/qstr.i.last
	$(PYTHON) $(MPY_PY_DIR)/makeqstrdefs.py split qstr $< $(MPY_GEN_DIR)/qstr _
	touch $@

$(MPY_GEN_DIR)/qstrdefs.collected.h : $(MPY_GEN_DIR)/qstr.split
	$(PYTHON) $(MPY_PY_DIR)/makeqstrdefs.py cat qstr _ $(MPY_GEN_DIR)/qstr $@

$(MPY_GEN_DIR)/module.split : $(MPY_GEN_DIR)/qstr.i.last
	$(PYTHON) $(MPY_PY_DIR)/makeqstrdefs.py split module $< $(MPY_GEN_DIR)/module _
	touch $@

$(MPY_GEN_DIR)/moduledefs.collected : $(MPY_GEN_DIR)/module.split
	$(PYTHON) $(MPY_PY_DIR)/makeqstrdefs.py cat module _ $(MPY_GEN_DIR)/module $@

$(MPY_GEN_DIR)/root_pointer.split : $(MPY_GEN_DIR)/qstr.i.last
	$(PYTHON) $(MPY_PY_DIR)/makeqstrdefs.py split root_pointer $< $(MPY_GEN_DIR)/root_pointer _
	touch $@

$(MPY_GEN_DIR)/root_pointers.collected : $(MPY_GEN_DIR)/root_pointer.split
	$(PYTHON) $(MPY_PY_DIR)/makeqstrdefs.py cat root_pointer _ $(MPY_GEN_DIR)/root_pointer $@

$(MPY_GENHDR_DIR)/qstrdefs.generated.h : $(MPY_GEN_DIR)/qstrdefs.collected.h $(MPY_PY_DIR)/qstrdefs.h $(MPY_PY_DIR)/makeqstrdata.py
	@$(MKDIR) $(MPY_GENHDR_DIR)
	cat $(MPY_PY_DIR)/qstrdefs.h $(MPY_GEN_DIR)/qstrdefs.collected.h | sed 's/^Q(.*)/"&"/' | $(X64_CC) -E $(MPY_CFLAGS) - | sed 's/^"\(Q(.*)\)"/\1/' > $(MPY_GEN_DIR)/qstrdefs.preprocessed.h
	$(PYTHON) $(MPY_PY_DIR)/makeqstrdata.py $(MPY_GEN_DIR)/qstrdefs.preprocessed.h > $@

$(MPY_GENHDR_DIR)/moduledefs.h : $(MPY_GEN_DIR)/moduledefs.collected $(MPY_PY_DIR)/makemoduledefs.py
	@$(MKDIR) $(MPY_GENHDR_DIR)
	$(PYTHON) $(MPY_PY_DIR)/makemoduledefs.py $< > $@

$(MPY_GENHDR_DIR)/root_pointers.h : $(MPY_GEN_DIR)/root_pointers.collected $(MPY_PY_DIR)/make_root_pointers.py
	@$(MKDIR) $(MPY_GENHDR_DIR)
	$(PYTHON) $(MPY_PY_DIR)/make_root_pointers.py $< > $@

.PHONY : mpy-qstr
mpy-qstr : $(MPY_GENHDR_DIR)/qstrdefs.generated.h $(MPY_GENHDR_DIR)/moduledefs.h $(MPY_GENHDR_DIR)/root_pointers.h

MPY_GENHDRS = $(MPY_GENHDR_DIR)/qstrdefs.generated.h $(MPY_GENHDR_DIR)/moduledefs.h $(MPY_GENHDR_DIR)/root_pointers.h

$(MPY_OBJS_DIR)/%.o : $(MPY_PY_DIR)/%.c $(SRC64_DIR)/mpport/mpconfigport.h | $(MPY_GENHDRS)
	@$(MKDIR) $(dir $@)
	$(X64_CC) $(MPY_CFLAGS) $(X64_DEPFLAGS) -c $< -o $@

$(MPY_OBJS_DIR)/mpport/%.o : $(SRC64_DIR)/mpport/%.c $(SRC64_DIR)/mpport/mpconfigport.h | $(MPY_GENHDRS)
	@$(MKDIR) $(dir $@)
	$(X64_CC) $(MPY_CFLAGS) $(X64_DEPFLAGS) -c $< -o $@

$(MPY_OBJS_DIR)/shared/runtime/%.o : $(MPY_DIR)/shared/runtime/%.c $(SRC64_DIR)/mpport/mpconfigport.h | $(MPY_GENHDRS)
	@$(MKDIR) $(dir $@)
	$(X64_CC) $(MPY_CFLAGS) $(X64_DEPFLAGS) -c $< -o $@

$(MPY_OBJS_DIR)/shared/readline/%.o : $(MPY_DIR)/shared/readline/%.c $(SRC64_DIR)/mpport/mpconfigport.h | $(MPY_GENHDRS)
	@$(MKDIR) $(dir $@)
	$(X64_CC) $(MPY_CFLAGS) $(X64_DEPFLAGS) -c $< -o $@

$(BUILD64_DIR)/app/crt/%.o : $(APP64_DIR)/crt/%.S
	@$(MKDIR) $(dir $@)
	$(X64_CC) $(APP64_CFLAGS) $(X64_DEPFLAGS) -c $< -o $@

$(BUILD64_DIR)/app/crt/%.o : $(APP64_DIR)/crt/%.c
	@$(MKDIR) $(dir $@)
	$(X64_CC) $(APP64_CFLAGS) $(X64_DEPFLAGS) -c $< -o $@

APP64_CRT_OBJS = $(BUILD64_DIR)/app/crt/crt0.o $(BUILD64_DIR)/app/crt/syscall.o $(BUILD64_DIR)/app/crt/string.o $(BUILD64_DIR)/app/crt/malloc.o

define APP64_RULES
$(BUILD64_DIR)/app/$(1)/$(1).o : $(APP64_DIR)/$(1)/$(1).c
	@$$(MKDIR) $$(dir $$@)
	$$(X64_CC) $$(APP64_CFLAGS) $$(X64_DEPFLAGS) -c $$< -o $$@

$(BUILD64_DIR)/app/$(1)/$(1).elf : $(BUILD64_DIR)/app/$(1)/$(1).o $$(APP64_CRT_OBJS) $(APP64_DIR)/app64.ld
	$$(X64_CC) $$(APP64_LDFLAGS) -Wl,-Map=$(BUILD64_DIR)/app/$(1)/$(1).map -o $$@ $(BUILD64_DIR)/app/$(1)/$(1).o $$(APP64_CRT_OBJS)
endef

$(foreach app,$(APP64_NAMES),$(eval $(call APP64_RULES,$(app))))

$(IMG64_FILE) : $(BOOT64_BIN) $(LOADER64_BIN) $(KERNEL64_BIN) $(FONT_DIR)/H04.FNT $(APP64_TARGETS) $(MKFAT32)
	@$(MKDIR) $(IMG64_DIR)
	$(PYTHON) $(MKFAT32) $@ $(BOOT64_BIN) $(LOADER64_BIN) $(KERNEL64_BIN) $(FONT_DIR)/H04.FNT $(foreach app,$(APP64_NAMES),$(app)=$(BUILD64_DIR)/app/$(app)/$(app).elf)

run64: $(IMG64_FILE)
	$(QEMU) -drive file=$(IMG64_FILE),format=raw,if=ide -boot c -no-reboot -d int -m 512M

# same image on the AHCI path: q35 has no legacy IDE, so this exercises
# src64/drivers/ahci64.c instead of the ATA PIO fallback
run64-ahci: $(IMG64_FILE)
	$(QEMU) -machine q35 -drive file=$(IMG64_FILE),format=raw,if=none,id=disk0 \
		-device ich9-ahci,id=ahci -device ide-hd,drive=disk0,bus=ahci.0 \
		-boot c -no-reboot -d int -m 512M

clean64:
	$(DEL) $(BUILD64_DIR) $(IMG64_DIR)

-include $(shell find $(BUILD64_DIR) -name '*.d' 2>/dev/null)

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
