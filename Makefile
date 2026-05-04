TOOLCHAIN ?= /opt/mipsel-mti-elf
CROSS_COMPILE ?= $(TOOLCHAIN)/bin/mipsel-mti-elf-
JOBS ?= 2
CCACHE ?=

ifeq ($(filter -j% --jobs%,$(MAKEFLAGS)),)
MAKEFLAGS += -j$(JOBS)
endif
ifeq ($(filter --output-sync% -O%,$(MAKEFLAGS)),)
MAKEFLAGS += --output-sync=target
endif

.PHONY: all check kernel-check clean

all: check

check: kernel-check

kernel-check:
	$(MAKE) -C kernel check TOOLCHAIN=$(TOOLCHAIN) \
		CROSS_COMPILE=$(CROSS_COMPILE) CCACHE=$(CCACHE) JOBS=$(JOBS)

clean:
	$(MAKE) -C kernel clean TOOLCHAIN=$(TOOLCHAIN) \
		CROSS_COMPILE=$(CROSS_COMPILE) CCACHE=$(CCACHE) JOBS=$(JOBS)
