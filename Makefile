# ===========================================================================
# Mowkow OS
#
# 트리가 둘이고 툴체인도 둘이다. 규칙은 mk/ 아래에 트리별로 나누어 두었다.
#
#   mk/config.mk       디렉터리, 도구, 컴파일 옵션 (공통)
#   mk/x86.mk          32비트 트리: src/, app/      -> img/haribote.img
#   mk/micropython.mk  커널에 넣는 MicroPython
#   mk/x86_64.mk       64비트 트리: src64/, app64/  -> img64/mowkow64.img
#
# include 순서에는 이유가 있다. mk/x86_64.mk의 커널 링크 규칙이 읽히는
# 시점에 MPY_* 오브젝트 목록이 이미 있어야 하므로, mk/micropython.mk를
# 먼저 읽는다.
#
# 무엇을 만들 수 있는지는 `make help`.
# ===========================================================================

include mk/config.mk
include mk/x86.mk
include mk/micropython.mk
include mk/x86_64.mk

# 32비트 이미지가 기본. include 순서와 상관없이 이것이 기본 목표가 되도록
# .DEFAULT_GOAL로 못 박는다.
.DEFAULT_GOAL := default

.PHONY : default help info \
	run iso clean $(APPS) \
	x86_64 run64 run64-ahci clean64 mpy-qstr

default : $(IMG_FILE)

help :
	@echo "Mowkow OS"
	@echo ""
	@echo "  32비트 (src/, app/  ->  img/haribote.img)"
	@echo "    make            이미지 빌드 (기본)"
	@echo "    make run        QEMU로 실행 (플로피)"
	@echo "    make iso        이미지를 ISO로 변환"
	@echo "    make clean      build/ img/ 지우기"
	@echo ""
	@echo "  64비트 (src64/, app64/  ->  img64/mowkow64.img)"
	@echo "    make x86_64     이미지 빌드"
	@echo "    make run64      QEMU로 실행 (IDE, ATA PIO 경로)"
	@echo "    make run64-ahci QEMU로 실행 (q35 + AHCI 경로)"
	@echo "    make clean64    build64/ img64/ 지우기"
	@echo ""
	@echo "  그 밖에"
	@echo "    make info       찾아낸 소스와 앱 목록 보기"
	@echo "    make mpy-qstr   MicroPython 생성 헤더만 만들기"

info :
	@echo "[32비트 커널]     $(KERNEL_SRCS)"
	@echo "[32비트 드라이버] $(DRIVERS_SRCS)"
	@echo "[32비트 라이브러리] $(LIB_SRCS)"
	@echo "[32비트 앱]       $(APPS)"
	@echo "[64비트 커널]     $(KERNEL64_C_SRCS)"
	@echo "[64비트 앱]       $(APP64_NAMES)"
	@echo "[커널 크기 상한]  $(KERNEL64_SECTORS) 섹터"
