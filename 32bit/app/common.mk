# app/common.mk
# 모든 앱이 공통으로 사용할 빌드 규칙

# -- Path --
TOOLPATH = ../../tools
INCPATH = ../../tools/haribote
APP_INC = ../include

# -- Tools --
NASK = $(TOOLPATH)/nask
CC1 = $(TOOLPATH)/gocc1 -I$(INCPATH) -I$(APP_INC) -Os -Wall -quiet
GAS2NASK = $(TOOLPATH)/gas2nask -a
OBJ2BIM = $(TOOLPATH)/obj2bim
BIM2HRB = $(TOOLPATH)/bim2hrb
BIM2BIN = $(TOOLPATH)/bim2bin
RULEFILE = $(TOOLPATH)/haribote/haribote.rul

BUILD_DIR = ../../../build/app/$(APP)
OUT_ORG = $(BUILD_DIR)/$(APP).org
OUT_HRB = $(BUILD_DIR)/$(APP).hrb
API_LIB = ../../../build/app/api/apilib.lib
MY_LIB = ../../../build/app/api/mylib.obj
HAN_LIB = ../../../build/app/api/hanlib.obj

# -- Default Memory Size --
STACK ?= 1k
MALLOC ?= 0k

SRCS = $(wildcard *.c)
OBJS = $(patsubst %.c, $(BUILD_DIR)/%.obj, $(SRCS))

default: $(OUT_HRB)

# -- c -> obj --
$(BUILD_DIR)/%.obj: %.c
	@mkdir -p $(dir $@)
	$(CC1) -o $(BUILD_DIR)/$*.gas $<
	$(GAS2NASK) $(BUILD_DIR)/$*.gas $(BUILD_DIR)/$*.nas
	$(NASK) $(BUILD_DIR)/$*.nas $@ $(BUILD_DIR)/$*.lst

$(MY_LIB): ../include/mylib.c
	@mkdir -p $(dir $@)
	$(CC1) -o $(BUILD_DIR)/mylib.gas $<
	$(GAS2NASK) $(BUILD_DIR)/mylib.gas $(BUILD_DIR)/mylib.nas
	$(NASK) $(BUILD_DIR)/mylib.nas $@ $(BUILD_DIR)/mylib.lst

$(HAN_LIB): ../include/apihan.c
	@mkdir -p $(dir $@)
	$(CC1) -o $(BUILD_DIR)/apihan.gas $<
	$(GAS2NASK) $(BUILD_DIR)/apihan.gas $(BUILD_DIR)/apihan.nas
	$(NASK) $(BUILD_DIR)/apihan.nas $@ $(BUILD_DIR)/apihan.lst

# -- obj -> bim
$(BUILD_DIR)/$(APP).bim: $(OBJS) $(API_LIB) $(MY_LIB) $(HAN_LIB)
	@mkdir -p $(dir $@)
	cd ../../../ && \
	$(abspath $(OBJ2BIM)) \
	@$(abspath $(RULEFILE)) \
	out:$(abspath $@) \
	map:$(abspath $(BUILD_DIR)/$(APP).map) \
	stack:$(STACK) \
	$(abspath $(OBJS)) \
	$(abspath $(API_LIB)) \
	$(abspath $(MY_LIB)) \
	$(abspath $(HAN_LIB))

$(OUT_ORG): $(BUILD_DIR)/$(APP).bim
	$(BIM2HRB) $< $@ $(MALLOC)

$(OUT_HRB): $(OUT_ORG)
	$(BIM2BIN) -osacmp in:$< out:$@

clean:
	rm -rf $(BUILD_DIR)
