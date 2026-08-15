VERSION_MAJOR := 1
VERSION_MINOR := 3
VERSION_MICRO := 2
VERSION := v${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_MICRO}

TOPDIR ?= $(CURDIR)
include $(DEVKITARM)/3ds_rules

TARGET := $(notdir $(CURDIR))
BUILD := build
SOURCES := source/sms source/gearsystem source/gearsystem/audio source/gearsystem/audio/emu2413 source/gearsystem/miniz
DATA := data
INCLUDES := source/gearsystem source/gearsystem/audio source/gearsystem/audio/emu2413 source/gearsystem/miniz
GRAPHICS :=
GFXBUILD := $(BUILD)
ROMFS := romfs

include $(TOPDIR)/resources/AppInfo

APP_TITLE := $(shell echo "$(APP_TITLE)" | cut -c1-128)
APP_DESCRIPTION := $(shell echo "$(APP_DESCRIPTION)" | cut -c1-256)
APP_AUTHOR := $(shell echo "$(APP_AUTHOR)" | cut -c1-128)
APP_PRODUCT_CODE := $(shell echo $(APP_PRODUCT_CODE) | cut -c1-16)
APP_UNIQUE_ID := $(shell echo $(APP_UNIQUE_ID) | cut -c1-7)
APP_ENCRYPTED := $(shell echo $(APP_ENCRYPTED) | cut -c1-5)
APP_SYSTEM_MODE := $(shell echo $(APP_SYSTEM_MODE) | cut -c1-4)
APP_SYSTEM_MODE_EXT := $(shell echo $(APP_SYSTEM_MODE_EXT) | cut -c1-6)
ICON := icon.png

ARCH := -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft
GIT_HASH := $(shell git log -1 --pretty=format:"%h" 2>/dev/null || echo "source")
FULL_VERSION := "$(VERSION) - $(GIT_HASH)"

CFLAGS := -g -Wall -Wno-format-truncation -Werror -O3 -mword-relocations -Wswitch \
          -Wno-unused-variable -ffunction-sections -fdata-sections -DVERSION=\"$(FULL_VERSION)\" \
          -DEMULATOR_BUILD=\"$(FULL_VERSION)\" $(ARCH) -flto -fomit-frame-pointer -fno-unwind-tables -fno-asynchronous-unwind-tables -fno-stack-protector -frename-registers -fipa-icf
CFLAGS += $(INCLUDE) -D__3DS__ -DGS_DISABLE_DISASSEMBLER -DGS_PERF_BUILD=1 -DGS_PERF_DIAGNOSTICS=0 -DNDEBUG $(EXTRA_CFLAGS)
CXXFLAGS := $(CFLAGS) -fno-rtti -fno-exceptions -std=gnu++11
ASFLAGS := -g $(ARCH)
LDFLAGS = -specs=3dsx.specs -g $(ARCH) -flto -Wl,--gc-sections -Wl,-Map,$(notdir $*.map)

LIBS := -lcitro2d -lcitro3d -lctru -lm

LIBDIRS := $(CTRULIB) $(PORTLIBS)

ifneq ($(BUILD),$(notdir $(CURDIR)))
export OUTPUT := $(CURDIR)/$(TARGET)
export TOPDIR := $(CURDIR)
export VPATH := $(foreach dir,$(SOURCES),$(CURDIR)/$(dir))
export DEPSDIR := $(CURDIR)/$(BUILD)

CFILES := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CPPFILES := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
SFILES := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))

ifeq ($(strip $(CPPFILES)),)
export LD := $(CC)
else
export LD := $(CXX)
endif

export OFILES_SOURCES := $(CPPFILES:.cpp=.o) $(CFILES:.c=.o) $(SFILES:.s=.o)
export OFILES_BIN := $(addsuffix .o,$(BINFILES))
export OFILES := $(OFILES_BIN) $(OFILES_SOURCES)
export HFILES :=

export INCLUDE := $(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
                  $(foreach dir,$(LIBDIRS),-I$(dir)/include) -I$(CURDIR)/$(BUILD)
export LIBPATHS := $(foreach dir,$(LIBDIRS),-L$(dir)/lib)
export _3DSXDEPS := $(if $(NO_SMDH),,$(OUTPUT).smdh)

ifeq ($(strip $(ICON)),)
export APP_ICON := $(TOPDIR)/icon.png
else
export APP_ICON := $(TOPDIR)/$(ICON)
endif

ifeq ($(strip $(NO_SMDH)),)
export _3DSXFLAGS += --smdh=$(CURDIR)/$(TARGET).smdh
endif
ifneq ($(ROMFS),)
export _3DSXFLAGS += --romfs=$(CURDIR)/$(ROMFS)
endif

.PHONY: release testing debug slowdebug clean all

all: release
release: EXTRA_CFLAGS := -O3 -DDEBUGLEVEL=0
testing: EXTRA_CFLAGS := -O3 -DDEBUGLEVEL=1
debug: EXTRA_CFLAGS := -g -O0 -DDEBUGLEVEL=2
slowdebug: EXTRA_CFLAGS := -g -O0 -DDEBUGLEVEL=3

release testing debug slowdebug:
	@mkdir -p $(BUILD)
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

clean:
	@echo clean ...
	@rm -fr $(BUILD) $(TARGET).3dsx $(OUTPUT).smdh $(TARGET).elf $(TARGET).cxi $(TARGET).cia

else

ifeq ($(strip $(NO_SMDH)),)
.PHONY: all
all: $(OUTPUT).3dsx $(OUTPUT).smdh $(OUTPUT).cia

# Build an installable CIA from the 3DSX. cxitool converts the 3DSX
# into a CXI and makerom packages that CXI into a CIA. These two tools
# are not part of the standard devkitARM 3DS rules.
$(OUTPUT).cxi: $(OUTPUT).3dsx
	@command -v cxitool >/dev/null 2>&1 || { echo "ERROR: cxitool is required to build a CIA. Install cxitool and put it in PATH."; exit 1; }
	@echo converting $(notdir $<) to $(notdir $@)
	@cxitool $< $@

$(OUTPUT).cia: $(OUTPUT).cxi $(OUTPUT).smdh
	@command -v makerom >/dev/null 2>&1 || { echo "ERROR: makerom is required to build a CIA. Install makerom and put it in PATH."; exit 1; }
	@echo building $(notdir $@)
	@makerom -v -f cia -o $@ -target t -i $<:0:0 -ignoresign -icon $(OUTPUT).smdh
	@echo built ... $(notdir $@)
endif

$(OUTPUT).3dsx: $(OUTPUT).elf $(_3DSXDEPS)
$(OFILES_SOURCES): $(HFILES)
$(OUTPUT).elf: $(OFILES) $(ROMFS_T3XFILES)

.PRECIOUS: %.t3x

-include $(DEPSDIR)/*.d

endif
