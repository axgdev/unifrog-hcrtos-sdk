JOBS ?= 2

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
	$(MAKE) -C kernel check

clean:
	$(MAKE) -C kernel clean
