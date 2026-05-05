TOOLCHAIN ?= /opt/mipsel-mti-elf
CROSS_COMPILE ?= $(TOOLCHAIN)/bin/mipsel-mti-elf-
CCACHE ?=
CCACHE_PREFIX := $(if $(CCACHE),$(CCACHE) )
CC := $(CCACHE_PREFIX)$(CROSS_COMPILE)gcc
LD := $(CROSS_COMPILE)ld
AR := $(CROSS_COMPILE)ar
NM := $(CROSS_COMPILE)nm
DTC ?= dtc
SD_MODE ?= safe

SD_MODES := safe wide uhs
ifneq ($(filter $(SD_MODE),$(SD_MODES)),$(SD_MODE))
$(error SD_MODE must be one of: $(SD_MODES))
endif

ifeq ($(SD_MODE),safe)
SD_BUS_WIDTH := 1
SD_CAP_HIGHSPEED := 0
SD_CAP_UHS := 0
SD_NO_1V8 := 1
else ifeq ($(SD_MODE),wide)
SD_BUS_WIDTH := 4
SD_CAP_HIGHSPEED := 1
SD_CAP_UHS := 0
SD_NO_1V8 := 1
else
SD_BUS_WIDTH := 4
SD_CAP_HIGHSPEED := 1
SD_CAP_UHS := 1
SD_NO_1V8 := 0
endif

BUILD := build
SOURCE := source
BOARD_DTS ?= ../../board/hc15xx/common/dts/sf2000_min.dts
DTS_INCLUDE ?= ../../dts/include
DTS_PRE := $(BUILD)/sf2000_min.dts.tmp
DTB := sf2000_min.dtb
DTS_MODE_STAMP := $(BUILD)/sd-mode.stamp
KERNEL_ARCHIVE := ../lib/core/libkernel.a
KERNEL_BUILTIN := $(BUILD)/built-in.o

include objects.mk

MIPS_ARCH ?= mips32
MIPS_TUNE ?= mips32
ARCH_CFLAGS ?= -march=$(MIPS_ARCH) -mtune=$(MIPS_TUNE)

CFLAGS := -EL $(ARCH_CFLAGS) -Os -msoft-float -fsigned-char -W \
	-ffunction-sections -fdata-sections -G0 \
	-Wformat=2 \
	-Wno-builtin-declaration-mismatch \
	-Wno-cast-function-type \
	-Wno-format-truncation \
	-Wno-shift-negative-value \
	-Wno-sign-compare \
	-Wno-error=attributes \
	-Wno-error=cast-function-type \
	-Wno-error=builtin-declaration-mismatch \
	-Wno-error=format-truncation= \
	-Wno-error=int-conversion \
	-D_FORTIFY_SOURCE=0 \
	-D__HCRTOS__ \
	-I$(SOURCE)/include \
	-I$(SOURCE)/include/uapi \
	-I$(SOURCE)/freertos/include \
	-I$(SOURCE)/freertos/include/freertos \
	-I$(SOURCE)/freertos/include/freertos/portable \
	-I$(SOURCE)/arch/mips/include \
	-I$(SOURCE)/arch/mips/include/uapi \
	-I$(SOURCE)/lib/easylogger/inc \
	-I$(SOURCE)/lib/zlib \
	-I$(SOURCE)/third_party/nuttx/include \
	-I$(SOURCE)/third_party/nuttx/fs/partition \
	-I$(SOURCE)/third_party/linux/include \
	-I../include/hcrtos \
	-I../include/newlib \
	-I../include \
	-I../include/generated

DTS_CPPFLAGS := \
	-I$(DTS_INCLUDE) \
	-I$(SOURCE)/include \
	-I../include/hcrtos \
	-DUNIFROG_SD_BUS_WIDTH=$(SD_BUS_WIDTH) \
	-DUNIFROG_SD_CAP_HIGHSPEED=$(SD_CAP_HIGHSPEED) \
	-DUNIFROG_SD_CAP_UHS=$(SD_CAP_UHS) \
	-DUNIFROG_SD_NO_1V8=$(SD_NO_1V8) \
	-nostdinc \
	-undef \
	-D__ASSEMBLY__ \
	-D__DTS__ \
	-x assembler-with-cpp

DTCFLAGS := \
	-Wno-reg_format \
	-Wno-unit_address_vs_reg \
	-Wno-unit_address_format \
	-Wno-avoid_default_addr_size \
	-Wno-avoid_unnecessary_addr_size \
	-Wno-alias_paths \
	-Wno-graph_child_address \
	-Wno-graph_port \
	-Wno-gpios_property \
	-Wno-interrupts_property \
	-Wno-i2c_bus_reg \
	-Wno-spi_bus_reg \
	-Wno-unique_unit_address \
	-Wno-simple_bus_reg \
	-Wno-pci_device_reg \
	-Wno-pci_bridge \
	-Wno-pci_device_bus_num

KERNEL_OBJECTS := $(addprefix $(BUILD)/,$(addsuffix .o,$(basename $(KERNEL_SOURCES))))
FDT_OBJECT := $(BUILD)/lib/fdt/fdt_api.o

.PHONY: all check clean FORCE

all: $(KERNEL_ARCHIVE)

check: $(KERNEL_ARCHIVE)
	@$(NM) $(KERNEL_ARCHIVE) >/dev/null
	@echo "OK"

$(KERNEL_ARCHIVE): $(KERNEL_OBJECTS) $(DTB)
	@echo "  LD      $(KERNEL_BUILTIN)"
	@mkdir -p $(dir $(KERNEL_BUILTIN))
	$(LD) -EL -r --allow-multiple-definition $(KERNEL_OBJECTS) -o $(KERNEL_BUILTIN)
	@echo "  AR      $@"
	$(AR) rcs $@ $(KERNEL_BUILTIN)

$(BUILD)/%.o: $(SOURCE)/%.c
	@echo "  CC      $<"
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -MD -MP -c $< -o $@

$(FDT_OBJECT): $(DTB)

$(BUILD)/%.o: $(SOURCE)/%.S
	@echo "  AS      $<"
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -D__ASSEMBLY__ -MD -MP -c $< -o $@

$(DTS_MODE_STAMP): FORCE
	@mkdir -p $(dir $@)
	@value=$$(printf '%s\n' \
		"SD_MODE=$(SD_MODE)" \
		"SD_BUS_WIDTH=$(SD_BUS_WIDTH)" \
		"SD_CAP_HIGHSPEED=$(SD_CAP_HIGHSPEED)" \
		"SD_CAP_UHS=$(SD_CAP_UHS)" \
		"SD_NO_1V8=$(SD_NO_1V8)"); \
	old=$$(cat $@ 2>/dev/null || true); \
	if test "$$value" != "$$old"; then printf '%s\n' "$$value" > $@; fi

FORCE:

$(DTS_PRE): $(BOARD_DTS) $(DTS_MODE_STAMP)
	@echo "  CPP     $<"
	@mkdir -p $(dir $@)
	$(CC) $(DTS_CPPFLAGS) -Wp,-MD,$@.d -E -o $@ $<

$(DTB): $(DTS_PRE)
	@echo "  DTC     $@"
	$(DTC) -O dtb -o $@ -b 0 $(DTCFLAGS) -d $@.d $<

clean:
	rm -rf $(BUILD) $(DTB) $(DTB).d $(KERNEL_ARCHIVE)
