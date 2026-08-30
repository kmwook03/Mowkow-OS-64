APP64_ROOT ?= ..
BUILD64_DIR ?= ../../../build64
X64_CC ?= x86_64-elf-gcc
APP64_CFLAGS ?= -ffreestanding -fno-pic -fno-pie -mno-red-zone -fno-stack-protector -fno-asynchronous-unwind-tables -fno-unwind-tables -nostdlib -Wall -Wextra -I$(APP64_ROOT)/crt/include
APP64_LDFLAGS ?= -nostdlib -static -T $(APP64_ROOT)/app64.ld

APP_OBJ = $(BUILD64_DIR)/app/$(APP)/$(APP).o
APP_ELF = $(BUILD64_DIR)/app/$(APP)/$(APP).elf
CRT_OBJS = $(BUILD64_DIR)/app/crt/crt0.o $(BUILD64_DIR)/app/crt/syscall.o $(BUILD64_DIR)/app/crt/string.o $(BUILD64_DIR)/app/crt/malloc.o

all: $(APP_ELF)

$(APP_ELF): $(APP_OBJ) $(CRT_OBJS) $(APP64_ROOT)/app64.ld
	$(X64_CC) $(APP64_LDFLAGS) -Wl,-Map=$(BUILD64_DIR)/app/$(APP)/$(APP).map -o $@ $(APP_OBJ) $(CRT_OBJS)

$(APP_OBJ): $(APP).c
	@mkdir -p $(dir $@)
	$(X64_CC) $(APP64_CFLAGS) -c $< -o $@

$(BUILD64_DIR)/app/crt/%.o: $(APP64_ROOT)/crt/%.S
	@mkdir -p $(dir $@)
	$(X64_CC) $(APP64_CFLAGS) -c $< -o $@

$(BUILD64_DIR)/app/crt/%.o: $(APP64_ROOT)/crt/%.c
	@mkdir -p $(dir $@)
	$(X64_CC) $(APP64_CFLAGS) -c $< -o $@
