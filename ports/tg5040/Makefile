SHELL := /bin/bash

CROSS_COMPILE ?= aarch64-nextui-linux-gnu-
CC := $(CROSS_COMPILE)gcc
STRIP := $(CROSS_COMPILE)strip
PLATFORM_DEFINE ?= PLATFORM_TG5040
SRC := $(shell find /workspace/src -name '*.c' -print | sort)
HDR := $(shell find /workspace/src -name '*.h' -print | sort) \
	/workspace/third_party/apostrophe/include/apostrophe.h \
	/workspace/third_party/apostrophe/include/apostrophe_widgets.h \
	/workspace/ports/tg5040/Makefile
INCLUDES := -I/workspace/third_party/apostrophe/include
PKG_CONFIG := pkg-config
SDL_CFLAGS := $(shell $(PKG_CONFIG) --cflags sdl2)
comma := ,
SDL_LDFLAGS := $(filter-out -Wl$(comma)-rpath$(comma)%,$(shell $(PKG_CONFIG) --libs sdl2)) -lSDL2_ttf -lSDL2_image

CFLAGS := -std=gnu11 -O2 -Wall -Wextra -Wno-unused-parameter \
	-mcpu=cortex-a53 -mtune=cortex-a53 \
	-D$(PLATFORM_DEFINE) \
	-ffunction-sections -fdata-sections \
	$(INCLUDES) $(SDL_CFLAGS)

LDFLAGS := $(SDL_LDFLAGS) -Wl,--gc-sections -lpthread -lm

all: $(BUILD_DIR)/nativessh

$(BUILD_DIR)/nativessh: $(SRC) $(HDR)
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ $(SRC) $(LDFLAGS)
	$(STRIP) $@
