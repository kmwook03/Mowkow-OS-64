# Private freestanding nano foundation; no hosted configure or generated config.
# Each header is audited directly, with the same strict policy as the CRT.
# These archives are not a complete nano executable or a runtime acceptance gate.
X64_AR ?= x86_64-elf-ar
A64_AR ?= aarch64-none-elf-ar
NANO64_COMPAT_BUILD = $(BUILD64_DIR)/nano-compat
NANO64_X64_OBJS = $(patsubst 64bit/%.c,$(NANO64_COMPAT_BUILD)/x86_64/%.o,\
	$(NANO64_COMPAT_SRCS))
NANO64_A64_OBJS = $(patsubst 64bit/%.c,$(NANO64_COMPAT_BUILD)/aarch64/%.o,\
	$(NANO64_COMPAT_SRCS))
NANO64_MAKE_INPUTS = 64bit/mk/nano64.mk 64bit/mk/common64.mk common/mk/config.mk

$(NANO64_COMPAT_BUILD)/x86_64/%.o : 64bit/%.c $(NANO64_MAKE_INPUTS)
	@$(MKDIR) $(dir $@)
	$(X64_CC) $(APP64_CFLAGS) -O2 $(X64_DEPFLAGS) \
		-I$(SRC64_DIR)/include -c $< -o $@

$(NANO64_COMPAT_BUILD)/aarch64/%.o : 64bit/%.c $(NANO64_MAKE_INPUTS)
	@$(MKDIR) $(dir $@)
	$(A64_CC) $(A64_APP_CFLAGS) -MMD -MP \
		-I$(SRC64_DIR)/include -c $< -o $@

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
