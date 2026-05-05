TOOLCHAIN ?= /opt/mipsel-mti-elf
CROSS_COMPILE ?= $(TOOLCHAIN)/bin/mipsel-mti-elf-
JOBS ?= 2
CCACHE ?=
SD_MODE ?= safe
BOARD_DTS ?= $(abspath ../board/hc15xx/common/dts/sf2000_min.dts)
DTS_INCLUDE ?= $(abspath ../dts/include)
OPEN_SOURCE_PATCHES := $(sort $(wildcard patches/open-source/*.patch))

ifeq ($(filter -j% --jobs%,$(MAKEFLAGS)),)
MAKEFLAGS += -j$(JOBS)
endif
ifeq ($(filter --output-sync% -O%,$(MAKEFLAGS)),)
MAKEFLAGS += --output-sync=target
endif

.PHONY: all check apply-open-source-patches unapply-open-source-patches kernel-check clean

all: check

check: kernel-check

apply-open-source-patches:
	@for patch_file in $(OPEN_SOURCE_PATCHES); do \
		if git apply --reverse --check "$$patch_file" >/dev/null 2>&1; then \
			echo "  PATCH   $$patch_file (already applied)"; \
		else \
			echo "  PATCH   $$patch_file"; \
			git apply "$$patch_file"; \
		fi; \
	done

unapply-open-source-patches:
	@for patch_file in $(shell printf '%s\n' $(OPEN_SOURCE_PATCHES) | sort -r); do \
		if git apply --reverse --check "$$patch_file" >/dev/null 2>&1; then \
			echo "  UNPATCH $$patch_file"; \
			git apply --reverse "$$patch_file"; \
		fi; \
	done

kernel-check:
	@$(MAKE) --no-print-directory apply-open-source-patches
	@status=0; \
	$(MAKE) -C kernel check TOOLCHAIN=$(TOOLCHAIN) \
		CROSS_COMPILE=$(CROSS_COMPILE) CCACHE=$(CCACHE) JOBS=$(JOBS) \
		SD_MODE=$(SD_MODE) BOARD_DTS=$(BOARD_DTS) \
		DTS_INCLUDE=$(DTS_INCLUDE) || status=$$?; \
	$(MAKE) --no-print-directory unapply-open-source-patches; \
	exit $$status

clean:
	$(MAKE) -C kernel clean TOOLCHAIN=$(TOOLCHAIN) \
		CROSS_COMPILE=$(CROSS_COMPILE) CCACHE=$(CCACHE) JOBS=$(JOBS) \
		SD_MODE=$(SD_MODE) BOARD_DTS=$(BOARD_DTS) \
		DTS_INCLUDE=$(DTS_INCLUDE)
