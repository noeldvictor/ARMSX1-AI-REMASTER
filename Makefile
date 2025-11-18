.ONESHELL:

CC := gcc
CXX := g++

SDL_CFLAGS := $(shell sdl2-config --cflags)
SDL_LIBS   := $(shell sdl2-config --libs)

BASE_CFLAGS := -g -DLOG_USE_COLOR -I"." -I"psx" $(SDL_CFLAGS)
BASE_CFLAGS += -Ofast -Wno-overflow -Wall -pedantic -Wno-address-of-packed-member -flto

BASE_CXXFLAGS := -std=c++17 $(BASE_CFLAGS)

PLATFORM := $(shell uname -s)

ifeq ($(PLATFORM),Darwin)
	BASE_CFLAGS += -mmacosx-version-min=10.9 -Wno-newline-eof
endif

SHARED_EXT := .so
SHARED_LDFLAGS := -shared
SHARED_CFLAGS := $(BASE_CFLAGS) -D__DLL_BUILD -fPIC
SHARED_CXXFLAGS := $(BASE_CXXFLAGS) -D__DLL_BUILD -fPIC

ifeq ($(PLATFORM),Darwin)
	SHARED_EXT := .dylib
	SHARED_LDFLAGS := -dynamiclib
endif

VERSION_TAG := $(shell git describe --always --tags --abbrev=0)
COMMIT_HASH := $(shell git rev-parse --short HEAD)
OS_INFO := $(shell uname -rmo)

BIN_DIR := bin
OBJ_DIR := $(BIN_DIR)/obj

BIN      := $(BIN_DIR)/psxe
SHARED_BIN := $(BIN_DIR)/libpsxe$(SHARED_EXT)

IMGUI_DIR := third_party/imgui

C_SOURCES := $(wildcard psx/*.c) \
             $(wildcard psx/dev/*.c) \
             $(wildcard psx/dev/cdrom/*.c) \
             $(wildcard psx/input/*.c) \
             $(wildcard psx/disc/*.c) \
             $(wildcard frontend/*.c)
C_SOURCES_SHARED := $(filter-out frontend/main.c,$(C_SOURCES))

CPP_SOURCES := frontend/imgui_layer.cpp \
               $(IMGUI_DIR)/imgui.cpp \
               $(IMGUI_DIR)/imgui_draw.cpp \
               $(IMGUI_DIR)/imgui_tables.cpp \
               $(IMGUI_DIR)/imgui_widgets.cpp \
               $(IMGUI_DIR)/backends/imgui_impl_sdl2.cpp \
               $(IMGUI_DIR)/backends/imgui_impl_sdlrenderer2.cpp

C_OBJS := $(patsubst %.c,$(OBJ_DIR)/%.o,$(C_SOURCES))
C_OBJS_SHARED := $(patsubst %.c,$(OBJ_DIR)/%.o,$(C_SOURCES_SHARED))
CPP_OBJS := $(patsubst %.cpp,$(OBJ_DIR)/%.o,$(CPP_SOURCES))
ALL_OBJS := $(C_OBJS) $(CPP_OBJS)
ALL_OBJS_SHARED := $(C_OBJS_SHARED) $(CPP_OBJS)

.PHONY: all clean shared

all: $(BIN)

shared: $(SHARED_BIN)

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

$(OBJ_DIR)/%.o: %.c | $(OBJ_DIR)
	mkdir -p $(dir $@)
	$(CC) -c $< -o $@ $(BASE_CFLAGS) \
		-DOS_INFO="$(OS_INFO)" \
		-DREP_VERSION="$(VERSION_TAG)" \
		-DREP_COMMIT_HASH="$(COMMIT_HASH)"

$(OBJ_DIR)/%.o: %.cpp | $(OBJ_DIR)
	mkdir -p $(dir $@)
	$(CXX) -c $< -o $@ $(BASE_CXXFLAGS) \
		-I$(IMGUI_DIR) \
		-I$(IMGUI_DIR)/backends \
		-DOS_INFO="$(OS_INFO)" \
		-DREP_VERSION="$(VERSION_TAG)" \
		-DREP_COMMIT_HASH="$(COMMIT_HASH)"

$(BIN): $(ALL_OBJS) | $(BIN_DIR)
	$(CXX) $(ALL_OBJS) -o $(BIN) $(SDL_LIBS)

$(SHARED_BIN): $(ALL_OBJS_SHARED) | $(BIN_DIR)
	$(CXX) $(SHARED_LDFLAGS) $(ALL_OBJS_SHARED) -o $(SHARED_BIN) $(SDL_LIBS)

clean:
	rm -rf "$(BIN_DIR)"
