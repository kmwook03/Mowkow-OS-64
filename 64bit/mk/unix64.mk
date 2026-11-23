# Separate SDK seed; no libc, legacy CRT, or automatic OS image installation.
UNIX64_DIR = 64bit/unix64
UNIX64_BUILD = $(BUILD64_DIR)/unix64
UNIX64_UAPI = $(SRC64_DIR)/include/uapi/unix64.h
UNIX64_FLAGS = -O2 -ffreestanding -fno-pic -fno-pie -fno-stack-protector \
	-fno-asynchronous-unwind-tables $(WARN64_CFLAGS)

define UNIX64_SDK_RULES
UNIX64_$(1)_ROOT = $(UNIX64_BUILD)/$(1)/sysroot
UNIX64_$(1)_INCLUDE = $(UNIX64_BUILD)/$(1)/sysroot/usr/include
UNIX64_$(1)_LIB = $(UNIX64_BUILD)/$(1)/sysroot/usr/lib

$$(UNIX64_$(1)_INCLUDE)/mowkow/unix64.h : $(UNIX64_UAPI)
	@$$(MKDIR) $$(dir $$@)
	cp $$< $$@

$$(UNIX64_$(1)_INCLUDE)/mowkow/syscall64.h : $(UNIX64_DIR)/include/syscall64.h
	@$$(MKDIR) $$(dir $$@)
	cp $$< $$@

$$(UNIX64_$(1)_LIB)/crt1.o : $(UNIX64_DIR)/crt/crt1.S \
	$$(UNIX64_$(1)_INCLUDE)/mowkow/unix64.h 64bit/mk/unix64.mk
	@$$(MKDIR) $$(dir $$@)
	$$($(2)) $$(UNIX64_FLAGS) $(3) -I$$(UNIX64_$(1)_INCLUDE) -c $$< -o $$@

$$(UNIX64_$(1)_LIB)/syscall64.o : $(UNIX64_DIR)/crt/syscall64.c \
	$$(UNIX64_$(1)_INCLUDE)/mowkow/unix64.h \
	$$(UNIX64_$(1)_INCLUDE)/mowkow/syscall64.h 64bit/mk/unix64.mk
	@$$(MKDIR) $$(dir $$@)
	$$($(2)) $$(UNIX64_FLAGS) $(3) -I$$(UNIX64_$(1)_INCLUDE) -c $$< -o $$@

$$(UNIX64_$(1)_LIB)/unix64.ld : $(UNIX64_DIR)/unix64.ld
	@$$(MKDIR) $$(dir $$@)
	cp $$< $$@

.PHONY : unix64-sdk-$(1)
unix64-sdk-$(1) : $$(UNIX64_$(1)_LIB)/crt1.o \
	$$(UNIX64_$(1)_LIB)/syscall64.o $$(UNIX64_$(1)_LIB)/unix64.ld
endef

$(eval $(call UNIX64_SDK_RULES,x86_64,X64_CC,-mno-red-zone))
$(eval $(call UNIX64_SDK_RULES,aarch64,A64_CC,-mgeneral-regs-only))

.PHONY : unix64-sdk
unix64-sdk : unix64-sdk-x86_64 unix64-sdk-aarch64

.PHONY : test64-unix-exec test64-unix-exec-ahci
test64-unix-exec : $(IMG64_FILE) unix64-sdk
	$(PYTHON) 64bit/tools/unix_exec_regression64.py \
		$(if $(UNIX_EXEC_LOG64),--log $(UNIX_EXEC_LOG64))

test64-unix-exec-ahci : $(IMG64_FILE) unix64-sdk
	$(PYTHON) 64bit/tools/unix_exec_regression64.py --ahci \
		$(if $(UNIX_EXEC_LOG64),--log $(UNIX_EXEC_LOG64))

# Explicit offline build; archives and Meson/Ninja are environment prerequisites.
UNIX64_MLIBC_ARCH ?= x86_64
.PHONY : unix64-mlibc test64-unix-libc test64-unix-libc-ahci
unix64-mlibc : unix64-sdk
	@test -n "$(UNIX64_ARCHIVES)" -a -n "$(UNIX64_WORK)" || \
		{ echo 'Set UNIX64_ARCHIVES and a new UNIX64_WORK directory'; exit 1; }
	$(PYTHON) 64bit/tools/unix_mlibc64.py --archives "$(UNIX64_ARCHIVES)" \
		--work "$(UNIX64_WORK)" --architecture $(UNIX64_MLIBC_ARCH) \
		--sdk "$(UNIX64_BUILD)/$(UNIX64_MLIBC_ARCH)/sysroot"

test64-unix-libc : $(IMG64_FILE) unix64-sdk
	@test -n "$(UNIX64_LIBC_SYSROOT)" || { echo 'Set UNIX64_LIBC_SYSROOT'; exit 1; }
	$(PYTHON) 64bit/tools/unix_libc_regression64.py --sysroot "$(UNIX64_LIBC_SYSROOT)" \
		$(if $(UNIX64_A64_LIBC_SYSROOT),--aarch64-sysroot "$(UNIX64_A64_LIBC_SYSROOT)") \
		$(if $(UNIX_LIBC_LOG64),--log "$(UNIX_LIBC_LOG64)")

test64-unix-libc-ahci : $(IMG64_FILE) unix64-sdk
	@test -n "$(UNIX64_LIBC_SYSROOT)" || { echo 'Set UNIX64_LIBC_SYSROOT'; exit 1; }
	$(PYTHON) 64bit/tools/unix_libc_regression64.py --ahci \
		--sysroot "$(UNIX64_LIBC_SYSROOT)" \
		$(if $(UNIX64_A64_LIBC_SYSROOT),--aarch64-sysroot "$(UNIX64_A64_LIBC_SYSROOT)") \
		$(if $(UNIX_LIBC_LOG64),--log "$(UNIX_LIBC_LOG64)")
