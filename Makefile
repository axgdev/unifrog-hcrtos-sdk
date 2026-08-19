TOOLCHAIN ?= /opt/mipsel-mti-elf
CROSS_COMPILE ?= $(TOOLCHAIN)/bin/mipsel-mti-elf-
JOBS ?= 2
CCACHE ?=
SD_MODE ?= safe
BOARD_DTS ?= $(abspath ../board/hc15xx/common/dts/sf2000_min.dts)
DTS_INCLUDE ?= $(abspath ../dts/include)

ifeq ($(MAKELEVEL),0)
ifeq ($(filter -j% --jobs%,$(MAKEFLAGS)),)
MAKEFLAGS += -j$(JOBS)
endif
endif
ifeq ($(filter --output-sync% -O%,$(MAKEFLAGS)),)
MAKEFLAGS += --output-sync=target
endif

.PHONY: all check kernel-check clean

all: check

check: kernel-check

kernel-check:
	$(MAKE) -C kernel check TOOLCHAIN=$(TOOLCHAIN) \
		CROSS_COMPILE=$(CROSS_COMPILE) CCACHE=$(CCACHE) JOBS=$(JOBS) \
		SD_MODE=$(SD_MODE) BOARD_DTS=$(BOARD_DTS) \
		DTS_INCLUDE=$(DTS_INCLUDE)

clean:
	$(MAKE) -C kernel clean TOOLCHAIN=$(TOOLCHAIN) \
		CROSS_COMPILE=$(CROSS_COMPILE) CCACHE=$(CCACHE) JOBS=$(JOBS) \
		SD_MODE=$(SD_MODE) BOARD_DTS=$(BOARD_DTS) \
		DTS_INCLUDE=$(DTS_INCLUDE)
