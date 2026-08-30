# ---------------------------------------------------------------------------
# 32비트 트리(32bit/src, 32bit/app) 빌드 규칙
#
# 교재 툴체인(nask, gocc1, obj2bim, bim2hrb, edimg)으로 FAT12 플로피 이미지
# img/haribote.img를 만든다. 이 트리는 동작하는 기준 구현이므로, 요청이 없는
# 한 건드리지 않는다.
#
# 목표:  make / make run / make iso / make clean
# ---------------------------------------------------------------------------

# -- 소스 찾기 --
KERNEL_SRCS = $(wildcard $(SRC_DIR)/kernel/*.c)
DRIVERS_SRCS = $(wildcard $(SRC_DIR)/drivers/*.c)
LIB_SRCS = $(wildcard $(SRC_DIR)/lib/*.c)

KERNEL_OBJS = $(patsubst $(SRC_DIR)/kernel/%.c, $(BUILD_DIR)/kernel/%.obj, $(KERNEL_SRCS))
DRIVERS_OBJS = $(patsubst $(SRC_DIR)/drivers/%.c, $(BUILD_DIR)/drivers/%.obj, $(DRIVERS_SRCS))
LIB_OBJS = $(patsubst $(SRC_DIR)/lib/%.c, $(BUILD_DIR)/lib/%.obj, $(LIB_SRCS))

NASKFUNC_OBJ = $(BUILD_DIR)/kernel/naskfunc.obj
FONT_OBJ = $(BUILD_DIR)/graphics/font/hankaku.obj

ALL_OBJS = $(KERNEL_OBJS) $(DRIVERS_OBJS) $(LIB_OBJS) $(NASKFUNC_OBJ) $(FONT_OBJ)

# -- 앱 찾기 (app/ 아래 디렉터리 하나가 앱 하나, api와 include는 뺀다) --
APP_DIRS = $(wildcard $(APP_DIR)/*/)
APP_NAMES = $(notdir $(patsubst %/,%,$(APP_DIRS)))
APPS = $(filter-out api include, $(APP_NAMES))

API_LIB = $(BUILD_DIR)/app/api/apilib.lib
APP_TARGETS = $(foreach app, $(APPS), $(BUILD_DIR)/app/$(app)/$(app).hrb)

IMG_FILE = $(IMG_DIR)/haribote.img

# -- 부트로더 --
$(BUILD_DIR)/boot/ipl.bin : $(SRC_DIR)/boot/ipl.nas
	@$(MKDIR) $(BUILD_DIR)/boot
	$(NASK) $< $@ $(subst .bin,.lst,$@)

$(BUILD_DIR)/boot/asmhead.bin : $(SRC_DIR)/boot/asmhead.nas
	@$(MKDIR) $(BUILD_DIR)/boot
	$(NASK) $< $@ $(subst .bin,.lst,$@)

# -- 커널과 드라이버 (c -> gas -> nas -> obj) --
$(BUILD_DIR)/%.obj : $(SRC_DIR)/%.c
	@$(MKDIR) $(dir $@)
	$(CC1) -o $(basename $@).gas $<
	$(GAS2NASK) $(basename $@).gas $(basename $@).nas
	$(NASK) $(basename $@).nas $@ $(basename $@).lst

$(BUILD_DIR)/kernel/naskfunc.obj : $(SRC_DIR)/kernel/naskfunc.nas
	@$(MKDIR) $(BUILD_DIR)/kernel
	$(NASK) $< $@ $(subst .obj,.lst,$@)

# -- 글꼴 --
$(BUILD_DIR)/graphics/font/hankaku.bin : $(FONT_DIR)/hankaku.txt
	@$(MKDIR) $(BUILD_DIR)/graphics/font
	$(MAKEFONT) $< $@

$(BUILD_DIR)/graphics/font/hankaku.obj : $(BUILD_DIR)/graphics/font/hankaku.bin
	$(BIN2OBJ) $< $@ _hankaku

# -- 커널 링크 (obj -> bim -> hrb), 그리고 asmhead와 이어 붙이기 --
$(BUILD_DIR)/kernel/bootpack.bim : $(ALL_OBJS)
	$(OBJ2BIM) @$(RULEFILE) out:$@ stack:3136k map:$(BUILD_DIR)/kernel/bootpack.map $(ALL_OBJS)

$(BUILD_DIR)/kernel/bootpack.hrb : $(BUILD_DIR)/kernel/bootpack.bim
	$(BIM2HRB) $< $@ 0

$(BUILD_DIR)/haribote.sys : $(BUILD_DIR)/boot/asmhead.bin $(BUILD_DIR)/kernel/bootpack.hrb
	cat $^ > $@

# -- API 라이브러리 (앱이 링크해서 쓴다) --
API_SRCS = $(wildcard $(APP_DIR)/api/*.nas)
API_OBJS = $(patsubst $(APP_DIR)/api/%.nas, $(BUILD_DIR)/app/api/%.obj, $(API_SRCS))

$(BUILD_DIR)/app/api/%.obj : $(APP_DIR)/api/%.nas
	@$(MKDIR) $(dir $@)
	$(NASK) $< $@ $(subst .obj,.lst,$@)

$(API_LIB) : $(API_OBJS)
	@$(MKDIR) $(dir $@)
	$(GOLIB) $(API_OBJS) out:$@

# -- 앱 (각 앱 디렉터리의 Makefile에 넘긴다) --
$(APP_TARGETS) : $(API_LIB)
	@$(MKDIR) $(dir $@)
	$(MAKE) -C $(APP_DIR)/$(notdir $(patsubst %/,%,$(dir $@)))

# -- 이미지 만들기 --
$(IMG_FILE) : $(BUILD_DIR)/boot/ipl.bin $(BUILD_DIR)/haribote.sys $(APP_TARGETS)
	@$(MKDIR) $(IMG_DIR)
	$(EDIMG) imgin:$(TOOLPATH)/fdimg0at.tek \
		wbinimg src:$(BUILD_DIR)/boot/ipl.bin len:512 from:0 to:0 \
		copy from:$(BUILD_DIR)/haribote.sys to:@: \
		$(foreach app, $(APP_TARGETS), copy from:$(app) to:@: ) \
		copy from:$(FONT_DIR)/E2.FNT to:@: \
		copy from:$(FONT_DIR)/H04.FNT to:@: \
		copy from:32bit/testfiles/gcd_lcm.mk to:@: \
		copy from:$(APP_DIR)/머꼬/library.scm to:@: \
		copy from:32bit/testfiles/sanjini.jpg to:@: \
		imgout:$@

# -- 명령 --
run : $(IMG_FILE)
	$(QEMU) -fda $(IMG_FILE) -no-reboot -d int -m 512M

iso : $(IMG_FILE)
	$(FDIMG2ISO) $(TOOLPATH)/makeiso/fdimg2iso.dat $(IMG_FILE) $(IMG_DIR)/haribote.iso

clean :
	$(DEL) $(BUILD_DIR) $(IMG_DIR)
