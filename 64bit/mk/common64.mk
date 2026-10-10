# ---------------------------------------------------------------------------
# 64-bit source inventories shared by x86_64 and AArch64.
#
# Keep architecture-neutral sources here.  Architecture makefiles append only
# their platform entry points and drivers; this prevents one port from silently
# omitting a new common subsystem.
# ---------------------------------------------------------------------------

COMMON64_C_SRCS = \
	$(SRC64_DIR)/drivers/block64.c \
	$(SRC64_DIR)/drivers/graphic64.c \
	$(SRC64_DIR)/kernel/cache64.c \
	$(SRC64_DIR)/kernel/console64.c \
	$(SRC64_DIR)/kernel/elf64_loader.c \
	$(SRC64_DIR)/kernel/fd64.c \
	$(SRC64_DIR)/kernel/fd64_fat.c \
	$(SRC64_DIR)/kernel/fd64_dir.c \
	$(SRC64_DIR)/kernel/gui64.c \
	$(SRC64_DIR)/kernel/memory64.c \
	$(SRC64_DIR)/kernel/mtask64.c \
	$(SRC64_DIR)/kernel/mutex64.c \
	$(SRC64_DIR)/kernel/process64.c \
	$(SRC64_DIR)/kernel/sheet64.c \
	$(SRC64_DIR)/kernel/syscall64.c \
	$(SRC64_DIR)/kernel/window64.c \
	$(SRC64_DIR)/lib/fifo64.c \
	$(SRC64_DIR)/lib/hangul64.c \
	$(SRC64_DIR)/lib/keymap64.c \
	$(SRC64_DIR)/lib/kstring64.c \
	$(SRC64_DIR)/lib/utf864.c

# app64/crt is a runtime, not an application.  Every other direct child is an
# application whose primary source and output use the directory name.
APP64_DIRS = $(wildcard $(APP64_DIR)/*/)
APP64_NAMES = $(filter-out crt,$(notdir $(patsubst %/,%,$(APP64_DIRS))))
