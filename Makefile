.ONESHELL:

CFLAGS := -g -DLOG_USE_COLOR `sdl2-config --cflags --libs`
CFLAGS += -Ofast -Wno-overflow -Wall -pedantic -Wno-address-of-packed-member -flto

PLATFORM := $(shell uname -s)

ifeq ($(PLATFORM),Darwin)
	CFLAGS += -mmacosx-version-min=10.9 -Wno-newline-eof
endif

SHARED_EXT := .so
SHARED_LDFLAGS := -shared
SHARED_CFLAGS := $(CFLAGS) -D__DLL_BUILD -fPIC

ifeq ($(PLATFORM),Darwin)
	SHARED_EXT := .dylib
	SHARED_LDFLAGS := -dynamiclib
endif

VERSION_TAG := $(shell git describe --always --tags --abbrev=0)
COMMIT_HASH := $(shell git rev-parse --short HEAD)
OS_INFO := $(shell uname -rmo)

SOURCES := $(wildcard psx/*.c)
SOURCES += $(wildcard psx/dev/*.c)
SOURCES += $(wildcard psx/dev/cdrom/*.c)
SOURCES += $(wildcard psx/input/*.c)
SOURCES += $(wildcard psx/disc/*.c)
SOURCES += $(wildcard frontend/*.c)
SHARED_SOURCES := $(filter-out frontend/main.c,$(SOURCES))

BIN_DIR := bin
BIN      := $(BIN_DIR)/psxe
SHARED_BIN := $(BIN_DIR)/libpsxe$(SHARED_EXT)

.PHONY: all clean shared

all: $(BIN)

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

$(BIN): $(SOURCES) | $(BIN_DIR)
	gcc $(SOURCES) -o $(BIN) \
		-I"." \
		-I"psx" \
		-DOS_INFO="$(OS_INFO)" \
		-DREP_VERSION="$(VERSION_TAG)" \
		-DREP_COMMIT_HASH="$(COMMIT_HASH)" \
		$(CFLAGS)

shared: $(SHARED_BIN)

$(SHARED_BIN): $(SHARED_SOURCES) | $(BIN_DIR)
	gcc $(SHARED_LDFLAGS) $(SHARED_SOURCES) -o $(SHARED_BIN) \
		-I"." \
		-I"psx" \
		-DOS_INFO="$(OS_INFO)" \
		-DREP_VERSION="$(VERSION_TAG)" \
		-DREP_COMMIT_HASH="$(COMMIT_HASH)" \
		$(SHARED_CFLAGS)

clean:
	rm -rf "$(BIN_DIR)"
