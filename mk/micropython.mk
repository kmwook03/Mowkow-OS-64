# ---------------------------------------------------------------------------
# 커널에 넣는 MicroPython (third_party/micropython, 포팅 계층은 src64/mpport)
#
# 업스트림 소스는 그대로 쓰고, 빌드만 여기서 다시 짠다. 업스트림 py/py.mk를
# 그대로 부르지 않는 이유는 우리 툴체인 옵션(-ffreestanding, -mno-red-zone,
# 크로스 컴파일러)을 쓰면서 필요한 파일만 고르기 위해서다.
#
# mk/x86_64.mk가 여기서 만든 MPY_* 오브젝트 목록을 커널에 링크하므로, 루트
# Makefile은 이 파일을 mk/x86_64.mk보다 먼저 include한다.
# ---------------------------------------------------------------------------

# third_party/micropython/py/py.mk의 PY_CORE_O_BASENAME과 같은 목록. 업스트림을
# 새로 받으면 손으로 맞춰 줘야 한다.
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

# qstr 수집 대상. shared/runtime/pyexec.c는 MP_QSTR_ 토큰(MP_QSTR___file__ 등)을
# 쓰고, shared/readline/readline.c는 기록 버퍼용 MP_REGISTER_ROOT_POINTER()가
# 있다. 둘 다 훑지 않으면 qstrdefs.generated.h / genhdr/root_pointers.h에
# 빠진 채로 생성된다.
MPY_QSTR_SRCS = $(filter-out $(MPY_PY_DIR)/nlr%.c, $(MPY_CORE_SRCS)) \
	$(MPY_DIR)/shared/runtime/pyexec.c \
	$(MPY_DIR)/shared/readline/readline.c

MPY_PORT_SRCS = $(wildcard $(SRC64_DIR)/mpport/*.c) $(wildcard $(SRC64_DIR)/mpport/libc/*.c)

MPY_GEN_DIR = $(BUILD64_DIR)/mpgen
# 코어 파일들이 "genhdr/xxx.h" 꼴로 include한다(업스트림 관례). 그래서 생성한
# 헤더는 -I 경로 아래 genhdr/ 디렉터리에 있어야 한다.
MPY_GENHDR_DIR = $(MPY_GEN_DIR)/genhdr
MPY_OBJS_DIR = $(BUILD64_DIR)/upy

MPY_OBJS = $(patsubst $(MPY_PY_DIR)/%.c, $(MPY_OBJS_DIR)/%.o, $(MPY_CORE_SRCS))
MPY_PORT_OBJS = $(patsubst $(SRC64_DIR)/mpport/%.c, $(MPY_OBJS_DIR)/mpport/%.o, $(MPY_PORT_SRCS))

# gchelper_generic.c: gc_collect()가 쓰는 레지스터 저장 + 스택 훑기. x86_64용
#   경로가 이미 있어서 아키텍처별 어셈블리를 새로 쓸 필요가 없다.
# pyexec.c: REPL 루프 자체(pyexec_friendly_repl, python_porting.md Stage 2).
# interrupt_char.c: pyexec의 Ctrl-C 처리.
# readline.c: pyexec가 쓰는 줄 편집기.
# 넷 다 업스트림 그대로 쓴다.
MPY_SHARED_OBJS = $(MPY_OBJS_DIR)/shared/runtime/gchelper_generic.o \
	$(MPY_OBJS_DIR)/shared/runtime/pyexec.o \
	$(MPY_OBJS_DIR)/shared/runtime/interrupt_char.o \
	$(MPY_OBJS_DIR)/shared/readline/readline.o

MPY_INCLUDES = -I$(SRC64_DIR)/mpport -I$(SRC64_DIR)/mpport/libc -I$(MPY_DIR) -I$(MPY_PY_DIR) -I$(MPY_GEN_DIR)
MPY_CFLAGS = $(X64_CFLAGS) $(MPY_INCLUDES)
MPY_QSTR_CFLAGS = $(MPY_CFLAGS) -DNO_QSTR

MPY_GENHDRS = $(MPY_GENHDR_DIR)/qstrdefs.generated.h \
	$(MPY_GENHDR_DIR)/moduledefs.h \
	$(MPY_GENHDR_DIR)/root_pointers.h

# -- qstr / module / root-pointer 코드 생성 (python_porting.md Stage 1.4) --
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

mpy-qstr : $(MPY_GENHDRS)

# -- 컴파일 (코어 / 포팅 계층 / 공용 런타임) --
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
