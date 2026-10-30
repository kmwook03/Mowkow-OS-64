# Private freestanding nano foundation; no hosted configure or generated config.
# Each header is audited directly, with the same strict policy as the CRT.
# The archives and executable use the same inventory on both architectures.
X64_AR ?= x86_64-elf-ar
A64_AR ?= aarch64-none-elf-ar
NANO64_COMPAT_BUILD = $(BUILD64_DIR)/nano-compat
NANO64_X64_OBJS = $(patsubst 64bit/%.c,$(NANO64_COMPAT_BUILD)/x86_64/%.o,\
	$(NANO64_COMPAT_SRCS))
NANO64_A64_OBJS = $(patsubst 64bit/%.c,$(NANO64_COMPAT_BUILD)/aarch64/%.o,\
	$(NANO64_COMPAT_SRCS))
NANO64_MAKE_INPUTS = 64bit/mk/nano64.mk 64bit/mk/common64.mk common/mk/config.mk
NANO64_INCLUDES = -I$(NANO64_DIR)/include -I$(NANO64_DIR)/libc -I$(NANO64_DIR)

$(NANO64_COMPAT_BUILD)/x86_64/%.o : 64bit/%.c $(NANO64_MAKE_INPUTS)
	@$(MKDIR) $(dir $@)
	$(X64_CC) $(APP64_CFLAGS) -O2 $(X64_DEPFLAGS) \
		-I$(SRC64_DIR)/include $(NANO64_INCLUDES) -c $< -o $@

$(NANO64_COMPAT_BUILD)/aarch64/%.o : 64bit/%.c $(NANO64_MAKE_INPUTS)
	@$(MKDIR) $(dir $@)
	$(A64_CC) $(A64_APP_CFLAGS) -MMD -MP \
		-I$(SRC64_DIR)/include $(NANO64_INCLUDES) -c $< -o $@

$(NANO64_COMPAT_BUILD)/x86_64/compat64.a : $(NANO64_X64_OBJS)
	$(DEL) $@
	$(X64_AR) rcs $@ $^

$(NANO64_COMPAT_BUILD)/aarch64/compat64.a : $(NANO64_A64_OBJS)
	$(DEL) $@
	$(A64_AR) rcs $@ $^

.PHONY : nano64-compat
nano64-compat : $(NANO64_COMPAT_BUILD)/x86_64/compat64.a \
	$(NANO64_COMPAT_BUILD)/aarch64/compat64.a

-include $(NANO64_X64_OBJS:.o=.d) $(NANO64_A64_OBJS:.o=.d)

# Build once, then install the same ELF under the public editor name.
NANO64_STAGE = $(BUILD64_DIR)/nano-upstream
NANO64_STAGE_STAMP = $(NANO64_STAGE)/.prepared.json
NANO64_VENDOR_INPUTS = $(wildcard third_party/nano/src/*) third_party/nano/source.json
NANO64_PATCH = $(NANO64_DIR)/patches/0001-freestanding-boundaries.patch
NANO64_APP_BUILD = $(BUILD64_DIR)/nano-app
NANO64_X64_PORT_OBJS = $(patsubst $(NANO64_DIR)/%.c,$(NANO64_APP_BUILD)/x86_64/%.o,\
	$(NANO64_PORT_SRCS))
NANO64_A64_PORT_OBJS = $(patsubst $(NANO64_DIR)/%.c,$(NANO64_APP_BUILD)/aarch64/%.o,\
	$(NANO64_PORT_SRCS))
NANO64_X64_VENDOR_OBJS = $(addprefix $(NANO64_APP_BUILD)/x86_64/vendor/,\
	$(addsuffix .o,$(NANO64_VENDOR_NAMES)))
NANO64_A64_VENDOR_OBJS = $(addprefix $(NANO64_APP_BUILD)/aarch64/vendor/,\
	$(addsuffix .o,$(NANO64_VENDOR_NAMES)))

$(NANO64_STAGE_STAMP) : $(NANO64_VENDOR_INPUTS) $(NANO64_PATCH) 64bit/tools/prepare_nano64.py
	$(PYTHON) 64bit/tools/prepare_nano64.py $(NANO64_STAGE)

$(NANO64_APP_BUILD)/x86_64/%.o : $(NANO64_DIR)/%.c $(NANO64_MAKE_INPUTS)
	@$(MKDIR) $(dir $@)
	$(X64_CC) $(APP64_CFLAGS) -O2 $(X64_DEPFLAGS) \
		$(NANO64_INCLUDES) -I$(SRC64_DIR)/include -c $< -o $@

$(NANO64_APP_BUILD)/aarch64/%.o : $(NANO64_DIR)/%.c $(NANO64_MAKE_INPUTS)
	@$(MKDIR) $(dir $@)
	$(A64_CC) $(A64_APP_CFLAGS) -MMD -MP \
		$(NANO64_INCLUDES) -I$(SRC64_DIR)/include -c $< -o $@

$(NANO64_APP_BUILD)/x86_64/vendor/%.o : third_party/nano/src/%.c \
	$(NANO64_STAGE_STAMP) $(NANO64_DIR)/config.h $(NANO64_DIR)/revision.h $(NANO64_MAKE_INPUTS)
	@$(MKDIR) $(dir $@)
	$(X64_CC) $(APP64_BASE_CFLAGS) -O2 $(VENDOR64_WARNINGS) $(X64_DEPFLAGS) \
		$(NANO64_INCLUDES) -I$(SRC64_DIR)/include -DHAVE_CONFIG_H \
		-c $(NANO64_STAGE)/src/$(notdir $<) -o $@

$(NANO64_APP_BUILD)/aarch64/vendor/%.o : third_party/nano/src/%.c \
	$(NANO64_STAGE_STAMP) $(NANO64_DIR)/config.h $(NANO64_DIR)/revision.h $(NANO64_MAKE_INPUTS)
	@$(MKDIR) $(dir $@)
	$(A64_CC) $(A64_APP_BASE_CFLAGS) $(VENDOR64_WARNINGS) -MMD -MP \
		$(NANO64_INCLUDES) -I$(SRC64_DIR)/include -DHAVE_CONFIG_H \
		-c $(NANO64_STAGE)/src/$(notdir $<) -o $@

$(NANO64_APP_BUILD)/x86_64/nano.elf : $(NANO64_X64_VENDOR_OBJS) $(NANO64_X64_PORT_OBJS) \
	$(NANO64_COMPAT_BUILD)/x86_64/compat64.a $(APP64_CRT_OBJS) $(APP64_DIR)/app64.ld
	$(X64_CC) $(APP64_LDFLAGS) -Wl,-Map=$(@:.elf=.map) -o $@ \
		$(NANO64_X64_VENDOR_OBJS) $(NANO64_X64_PORT_OBJS) \
		$(NANO64_COMPAT_BUILD)/x86_64/compat64.a $(APP64_CRT_OBJS)

$(NANO64_APP_BUILD)/aarch64/nano.elf : $(NANO64_A64_VENDOR_OBJS) $(NANO64_A64_PORT_OBJS) \
	$(NANO64_COMPAT_BUILD)/aarch64/compat64.a $(A64_APP_CRT_OBJS) \
	$(A64_APP_BUILD_DIR)/malloc.o $(APP64_DIR)/app64.ld
	$(A64_CC) -nostdlib -static -T $(APP64_DIR)/app64.ld -Wl,-Map=$(@:.elf=.map) -o $@ \
		$(NANO64_A64_VENDOR_OBJS) $(NANO64_A64_PORT_OBJS) \
		$(NANO64_COMPAT_BUILD)/aarch64/compat64.a $(A64_APP_CRT_OBJS) \
		$(A64_APP_BUILD_DIR)/malloc.o

.PHONY : nano64-apps
nano64-apps : $(NANO64_APP_BUILD)/x86_64/nano.elf $(NANO64_APP_BUILD)/aarch64/nano.elf

$(BUILD64_DIR)/app/나노/나노.elf : $(NANO64_APP_BUILD)/x86_64/nano.elf
	@$(MKDIR) $(dir $@)
	$(COPY) $< $@

$(A64_APP_BUILD_DIR)/나노.elf : $(NANO64_APP_BUILD)/aarch64/nano.elf
	@$(MKDIR) $(dir $@)
	$(COPY) $< $@

-include $(NANO64_X64_PORT_OBJS:.o=.d) $(NANO64_A64_PORT_OBJS:.o=.d) \
	$(NANO64_X64_VENDOR_OBJS:.o=.d) $(NANO64_A64_VENDOR_OBJS:.o=.d)
