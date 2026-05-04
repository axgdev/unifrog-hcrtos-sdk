TOOLCHAIN ?= /opt/mipsel-mti-elf
CROSS_COMPILE ?= $(TOOLCHAIN)/bin/mipsel-mti-elf-
JOBS ?= 2
CCACHE ?=
OPEN_SOURCE_PATCHES := $(sort $(wildcard patches/open-source/*.patch))

ifeq ($(filter -j% --jobs%,$(MAKEFLAGS)),)
MAKEFLAGS += -j$(JOBS)
endif
ifeq ($(filter --output-sync% -O%,$(MAKEFLAGS)),)
MAKEFLAGS += --output-sync=target
endif

.PHONY: all check apply-open-source-patches kernel-check clean

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

kernel-check: apply-open-source-patches
	$(MAKE) -C kernel check TOOLCHAIN=$(TOOLCHAIN) \
		CROSS_COMPILE=$(CROSS_COMPILE) CCACHE=$(CCACHE) JOBS=$(JOBS)

clean:
	$(MAKE) -C kernel clean TOOLCHAIN=$(TOOLCHAIN) \
		CROSS_COMPILE=$(CROSS_COMPILE) CCACHE=$(CCACHE) JOBS=$(JOBS)
