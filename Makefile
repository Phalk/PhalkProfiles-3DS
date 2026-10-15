# Phalk Profiles for Nintendo 3DS (devkitARM + libctru)
.SUFFIXES:

ifeq ($(strip $(DEVKITARM)),)
$(error Set DEVKITARM to your devkitPro/devkitARM directory)
endif

TOPDIR ?= $(CURDIR)
APP_ICON := $(TOPDIR)/icon.png
include $(DEVKITARM)/3ds_rules
export LD := $(CXX)

TARGET       := PhalkProfiles-3DS
BUILD        := build
SOURCES      := source
DATA         :=
INCLUDES     := include
ROMFS        := $(TOPDIR)/romfs
APP_TITLE    := Phalk Profiles 3DS
APP_DESCRIPTION := Phalk Profiles playtime uploader
APP_AUTHOR   := Phalk

ARCH     := -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft
CFLAGS   := -g -Wall -Wextra -O2 -mword-relocations -ffunction-sections $(ARCH) $(INCLUDE) -D__3DS__
CXXFLAGS := $(CFLAGS) -fno-rtti -fno-exceptions -std=gnu++11
ASFLAGS  := -g $(ARCH)
LDFLAGS  := -specs=3dsx.specs -g $(ARCH) -Wl,-Map,$(notdir $*.map)
LIBS     := -lctru -lm
LIBDIRS  := $(CTRULIB)

ifneq ($(BUILD),$(notdir $(CURDIR)))

export OUTPUT   := $(CURDIR)/$(TARGET)
export TOPDIR   := $(CURDIR)
export VPATH    := $(foreach dir,$(SOURCES),$(CURDIR)/$(dir))
export DEPSDIR  := $(CURDIR)/$(BUILD)

CFILES         := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CPPFILES       := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
SFILES         := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))
export OFILES  := $(CPPFILES:.cpp=.o) $(CFILES:.c=.o) $(SFILES:.s=.o)
export INCLUDE := $(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) $(foreach dir,$(LIBDIRS),-I$(dir)/include) -I$(CURDIR)/$(BUILD)
export LIBPATHS := $(foreach dir,$(LIBDIRS),-L$(dir)/lib)
export _3DSXDEPS := $(OUTPUT).smdh
export _3DSXFLAGS += --smdh=$(OUTPUT).smdh --romfs=$(ROMFS)

.PHONY: all clean $(BUILD)
all: $(BUILD)

$(BUILD):
	@mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

clean:
	@rm -rf $(BUILD) $(TARGET).3dsx $(TARGET).smdh $(TARGET).elf

else

.PHONY: all
all: $(OUTPUT).3dsx
$(OUTPUT).3dsx: $(OUTPUT).elf $(_3DSXDEPS) $(ROMFS)/3ds_db.json
$(OUTPUT).elf: $(OFILES)
-include $(DEPSDIR)/*.d

endif
