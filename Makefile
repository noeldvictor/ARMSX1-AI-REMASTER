.ONESHELL:
.SHELLFLAGS := -ec



SDL_CONFIG ?= sdl2-config

CC ?= gcc
CXX ?= g++
AR := /usr/bin/ar
RANLIB := /usr/bin/ranlib

SDL_CFLAGS ?= $(shell $(SDL_CONFIG) --cflags 2>/dev/null)
SDL_LIBS_DYNAMIC ?= $(shell $(SDL_CONFIG) --libs 2>/dev/null)
SDL_LIBS_STATIC ?= $(shell $(SDL_CONFIG) --static-libs 2>/dev/null || $(SDL_CONFIG) --libs --static 2>/dev/null)
SDL_STATIC ?= 1
USE_CHD ?= 1
HW_DEBUG ?= 0

# Prefer PATH cmake; fall back to the gitignored vendored binary so chd/zip
# tests run when the host has no system cmake.
CMAKE ?= $(shell command -v cmake 2>/dev/null)
ifeq ($(strip $(CMAKE)),)
  ifneq ($(wildcard third_party/cmake/bin/cmake),)
    CMAKE := third_party/cmake/bin/cmake
  else
    CMAKE := cmake
  endif
endif

PLATFORM := $(shell uname -s)
MACOS_DEPLOYMENT_TARGET ?= 10.15
WINDOWS_TARGET ?= 0
SDKROOT ?=
CONTROLLER_GENERIC ?= 0
FSUI_DIR := third_party/fuse-lib
FSUI_BUILD_DIR ?= build/fsui/native
FSUI_LINK_SYSTEM_GL ?= 1



BASE_CFLAGS = -g -DLOG_USE_COLOR -I"." -I"psx" $(SDL_CFLAGS)
BASE_CFLAGS += -O3 -ffast-math -Wno-overflow -Wall -pedantic -Wno-address-of-packed-member -flto

FSUI_INCLUDE_FLAGS = \
	-I$(FSUI_DIR)/include \
	-I$(FSUI_DIR)/third_party/imgui \
	-I$(FSUI_DIR)/third_party/imgui/backends \
	-I$(FSUI_DIR)/third_party/stb

FSUI_INCLUDE_FLAGS += -I$(FSUI_BUILD_DIR)/gladsources/fsui_glad/include

FSUI_COMPILE_DEFS = -DIMGUI_FRONTEND -DFSUI_HAS_SDL2_PLATFORM -DFSUI_HAS_SDL2SURFACE_RENDERER -DFSUI_HAS_SDL2RENDERER_RENDERER -DSDL_MAIN_HANDLED

LIBCHDR_DIR := third_party/libchdr
LIBCHDR_BUILD_DIR :=
LIBCHDR_INCLUDE_FLAGS :=
LIBCHDR_INPUTS :=
LIBCHDR_ARCHIVE :=
LIBCHDR_LIBS :=
LIBCHDR_CONFIGURE :=
LIBCHDR_CMAKE_ARGS :=
LIBCHDR_CMAKE_TOOLCHAIN_FILE ?=
LIBCHDR_ANDROID_ABI ?=
LIBCHDR_ANDROID_PLATFORM ?=
LIBCHDR_ANDROID_FLEXIBLE_PAGE_SIZES ?=
CHD_COMPILE_DEFS :=
CHD_BUILD_DEPS :=
CHD_LINK_LIBS :=
ifeq ($(USE_CHD),1)
		LIBCHDR_BUILD_DIR := build/libchdr/native

	LIBCHDR_INCLUDE_FLAGS := -I$(LIBCHDR_DIR)/include -I$(LIBCHDR_DIR)/deps/miniz-3.1.1
	LIBCHDR_INPUTS := $(shell find $(LIBCHDR_DIR) -type f 2>/dev/null)
	LIBCHDR_ARCHIVE := $(LIBCHDR_BUILD_DIR)/libchdr-static.a
	LIBCHDR_LIBS := \
		$(LIBCHDR_ARCHIVE) \
		$(LIBCHDR_BUILD_DIR)/deps/lzma-25.01/libchdr-lzma.a \
		$(LIBCHDR_BUILD_DIR)/deps/miniz-3.1.1/libminiz.a \
		$(LIBCHDR_BUILD_DIR)/deps/zstd-1.5.7/libzstd.a
	LIBCHDR_CONFIGURE := $(CMAKE)

	LIBCHDR_CMAKE_ARGS := -S $(LIBCHDR_DIR) -B $(LIBCHDR_BUILD_DIR) -DBUILD_SHARED_LIBS=OFF -DCHDR_WANT_RAW_DATA_SECTOR=ON -DCHDR_WANT_SUBCODE=ON -DMINIZ_ARCHIVE_APIS=ON -DMINIZ_DEFLATE_APIS=ON -DMINIZ_STDIO=ON -DCMAKE_BUILD_TYPE=Release
	ifneq ($(strip $(LIBCHDR_CMAKE_TOOLCHAIN_FILE)),)
		LIBCHDR_CMAKE_ARGS += -DCMAKE_TOOLCHAIN_FILE=$(LIBCHDR_CMAKE_TOOLCHAIN_FILE)
	endif
	ifneq ($(strip $(LIBCHDR_ANDROID_ABI)),)
		LIBCHDR_CMAKE_ARGS += -DANDROID_ABI=$(LIBCHDR_ANDROID_ABI)
	endif
	ifneq ($(strip $(LIBCHDR_ANDROID_PLATFORM)),)
		LIBCHDR_CMAKE_ARGS += -DANDROID_PLATFORM=$(LIBCHDR_ANDROID_PLATFORM)
	endif
	ifneq ($(strip $(LIBCHDR_ANDROID_FLEXIBLE_PAGE_SIZES)),)
		LIBCHDR_CMAKE_ARGS += -DANDROID_SUPPORT_FLEXIBLE_PAGE_SIZES=$(LIBCHDR_ANDROID_FLEXIBLE_PAGE_SIZES)
	endif
	ifneq ($(strip $(CC)),)
		LIBCHDR_CMAKE_ARGS += -DCMAKE_C_COMPILER=$(CC)
	endif
	ifneq ($(strip $(CXX)),)
		LIBCHDR_CMAKE_ARGS += -DCMAKE_CXX_COMPILER=$(CXX)
	endif
	ifneq ($(strip $(AR)),)
		LIBCHDR_CMAKE_ARGS += -DCMAKE_AR=$(AR)
	endif
	ifneq ($(strip $(RANLIB)),)
		LIBCHDR_CMAKE_ARGS += -DCMAKE_RANLIB=$(RANLIB)
	endif
	ifeq ($(PLATFORM),Darwin)
		LIBCHDR_CMAKE_ARGS += -DCMAKE_OSX_DEPLOYMENT_TARGET=$(MACOS_DEPLOYMENT_TARGET)
	endif

	CHD_COMPILE_DEFS := -DUSE_CHD
	CHD_BUILD_DEPS := $(LIBCHDR_ARCHIVE)
	CHD_LINK_LIBS := $(LIBCHDR_LIBS)
endif

BASE_CXXFLAGS = -std=c++20 $(BASE_CFLAGS) $(FSUI_INCLUDE_FLAGS) $(FSUI_COMPILE_DEFS)
BASE_CFLAGS += $(LIBCHDR_INCLUDE_FLAGS) $(CHD_COMPILE_DEFS)
BASE_CXXFLAGS += $(LIBCHDR_INCLUDE_FLAGS) $(CHD_COMPILE_DEFS)

ifeq ($(CONTROLLER_GENERIC),1)
	BASE_CFLAGS += -DCONTROLLER_GENERIC
	BASE_CXXFLAGS += -DCONTROLLER_GENERIC
endif

# Triangle-dispatch hook + SDL presentation backend. Not GPU rasterization.
BASE_CFLAGS += -DUSE_GPU_BACKEND
BASE_CXXFLAGS += -DUSE_GPU_BACKEND

ifeq ($(HW_DEBUG),1)
	BASE_CFLAGS += -DHW_DEBUG
	BASE_CXXFLAGS += -DHW_DEBUG
endif






OS_INFO := $(shell uname -rmo)

ifeq ($(PLATFORM),Darwin)
	BASE_CFLAGS += -mmacosx-version-min=$(MACOS_DEPLOYMENT_TARGET) -Wno-newline-eof
	BASE_CXXFLAGS += -mmacosx-version-min=$(MACOS_DEPLOYMENT_TARGET)
endif

SHARED_EXT := .so
SHARED_LDFLAGS := -shared
SHARED_CFLAGS := $(BASE_CFLAGS) -D__DLL_BUILD -fPIC
SHARED_CXXFLAGS := $(BASE_CXXFLAGS) -D__DLL_BUILD -fPIC

ifeq ($(PLATFORM),Darwin)
	SHARED_EXT := .dylib
	SHARED_LDFLAGS := -dynamiclib
else ifeq ($(WINDOWS_TARGET),1)
	SHARED_EXT := .dll
endif


VERSION_TAG := $(shell git describe --always --tags --abbrev=0)
COMMIT_HASH := $(shell git rev-parse --short HEAD)

BIN_DIR := bin
OBJ_DIR := $(BIN_DIR)/obj

BIN      := $(BIN_DIR)/armsx
ifeq ($(WINDOWS_TARGET),1)
BIN      := $(BIN_DIR)/armsx.exe
endif
SHARED_BIN := $(BIN_DIR)/libarmsx$(SHARED_EXT)
RUNTIME_ICON_SRC := icons/FsuiAppIcon.png
RUNTIME_ICON_DEST := $(BIN_DIR)/icons/FsuiAppIcon.png
WINDRES ?= windres

C_SOURCES := $(wildcard psx/*.c) \
             $(wildcard psx/dev/*.c) \
             $(wildcard psx/dev/cdrom/*.c) \
             $(wildcard psx/input/*.c) \
             $(wildcard psx/disc/*.c) \
             frontend/argparse.c \
             frontend/config.c \
             frontend/diagnostics.c \
             frontend/platform_file.c \
             frontend/toml.c
C_SOURCES := $(filter-out psx/dev/cdrom/chd.c,$(C_SOURCES))
C_SOURCES += frontend/gpu_hw.c
ifeq ($(USE_CHD),1)
C_SOURCES += psx/dev/cdrom/chd.c
endif
C_SOURCES_SHARED := $(C_SOURCES)

CPP_SOURCES := frontend/archive.cpp frontend/main.cpp

FSUI_LIBS := \
	$(FSUI_BUILD_DIR)/libfsui-donor.a \
	$(FSUI_BUILD_DIR)/libfsui-backend-sdl.a \
	$(FSUI_BUILD_DIR)/libfsui-platform-sdl2.a \
	$(FSUI_BUILD_DIR)/libfsui-renderer-sdl2.a \
	$(FSUI_BUILD_DIR)/libfsui-renderer-sdl2surface.a \
	$(FSUI_BUILD_DIR)/libfsui-core.a \
	$(FSUI_BUILD_DIR)/libfsui_imgui.a \
	$(FSUI_BUILD_DIR)/libfsui_resources.a
FSUI_LIBS += \
	$(FSUI_BUILD_DIR)/libfsui-renderer-opengl.a \
	$(FSUI_BUILD_DIR)/libfsui_glad.a

PLATFORM_EXTRA_LDFLAGS :=
PLATFORM_EXTRA_LIBS :=

ifeq ($(PLATFORM),Darwin)
	FSUI_LIBS += $(FSUI_BUILD_DIR)/libfsui-renderer-metal.a
		PLATFORM_EXTRA_LDFLAGS += -mmacosx-version-min=$(MACOS_DEPLOYMENT_TARGET)
		PLATFORM_EXTRA_LIBS += -framework Cocoa -framework Foundation -framework IOKit -framework Metal -framework OpenGL -framework QuartzCore
else ifeq ($(WINDOWS_TARGET),1)
	PLATFORM_EXTRA_LIBS += -lsetupapi -limm32 -lversion -lwinmm -lgdi32 -lole32 -loleaut32 -lshell32 -luuid -lopengl32
	PLATFORM_EXTRA_LIBS += -ldbghelp
else
	PLATFORM_EXTRA_LIBS += -ldl
ifeq ($(FSUI_LINK_SYSTEM_GL),1)
	PLATFORM_EXTRA_LIBS += -lGL
endif
endif

C_OBJS := $(patsubst %.c,$(OBJ_DIR)/%.o,$(C_SOURCES))
C_OBJS_SHARED := $(patsubst %.c,$(OBJ_DIR)/%.o,$(C_SOURCES_SHARED))
CPP_OBJS := $(patsubst %.cpp,$(OBJ_DIR)/%.o,$(CPP_SOURCES))
RC_SOURCES :=
ifeq ($(WINDOWS_TARGET),1)
RC_SOURCES += resources/windows/armsx.rc
endif
RC_OBJS := $(patsubst %.rc,$(OBJ_DIR)/%.o,$(RC_SOURCES))
ALL_OBJS := $(C_OBJS) $(CPP_OBJS) $(RC_OBJS)
ALL_OBJS_SHARED := $(C_OBJS_SHARED) $(CPP_OBJS)

# Force dynamic SDL when building the shared library
ifneq (,$(filter shared,$(MAKECMDGOALS)))
override SDL_STATIC := 0
BASE_CFLAGS += -D__DLL_BUILD -fPIC
BASE_CXXFLAGS += -D__DLL_BUILD -fPIC
endif

ifeq ($(SDL_STATIC),1)
	ifeq ($(SDL_LIBS_STATIC),)
$(warning Static SDL2 libraries not found; falling back to dynamic SDL2)
		SDL_STATIC := 0
	endif
endif

SDL_LIBS := $(if $(filter 1,$(SDL_STATIC)),$(SDL_LIBS_STATIC),$(SDL_LIBS_DYNAMIC))
SDL_LIBS_SHARED := $(SDL_LIBS_DYNAMIC)

.PHONY: all clean shared qa test-qa test-boot test-boot-see test-see test-vk test test-cpu test-gpu test-audio test-sdl-audio test-chd test-zip test-sdl-runtime disc-probe

all: $(BIN)

shared: $(SHARED_BIN)

QA_BIN := build/tools/armsx-qa

qa: $(QA_BIN)

# Exercises the capture path (PNG writer, hashing, argument handling) without
# needing a BIOS or a disc, so it can run in the deterministic gate.
test-qa: $(QA_BIN)
	mkdir -p build/tests/qa
	./$(QA_BIN) --selftest=build/tests/qa/selftest.png
	python3 tests/validate_qa.py build/tests/qa/selftest.png

# Optional: boot local BIOS+discs and check golden frame hashes.
# Skips cleanly when bios/ and roms/ are empty (they are gitignored).
test-boot: $(QA_BIN)
	python3 tests/boot_local.py

# Two enhancement-off + two enhancement-on boots of one local title.
# Skips (exit 0) when bios/ or roms/ are missing.
test-boot-see: $(QA_BIN)
	python3 tests/boot_see_gate.py

TEST_CORE_SOURCES := $(wildcard psx/*.c) \
                     $(wildcard psx/dev/*.c) \
                     $(wildcard psx/dev/cdrom/*.c) \
                     $(wildcard psx/input/*.c)
TEST_CORE_SOURCES := $(filter-out psx/dev/cdrom/chd.c,$(TEST_CORE_SOURCES))
TEST_CPU_BIN := build/tests/cpu_differential
TEST_GPU_BIN := build/tests/gpu_renderer_parity
TEST_AUDIO_BIN := build/tests/audio_timing
TEST_SDL_AUDIO_BIN := build/tests/sdl_audio_queue_smoke
TEST_CHD_BIN := build/tests/chd_logic
TEST_ZIP_BIN := build/tests/zip_integration
TEST_SDL_BIN := build/tests/sdl_renderer_smoke
DISC_PROBE_BIN := build/tests/disc_probe
TEST_PLATFORM_FILE_OBJ := build/tests/platform_file.o

$(TEST_PLATFORM_FILE_OBJ): frontend/platform_file.c frontend/platform_file.h
	mkdir -p $(dir $@)
	$(CC) -std=c11 -O2 -g -I. -c frontend/platform_file.c -o $@

# miniz is vendored inside libchdr. Compile it directly so the QA driver
# (PNG capture) does not require cmake or libchdr-static.a. CHD remains
# available to the full app; the harness boots BIN/CUE/ISO.
QA_MINIZ_C := third_party/libchdr/deps/miniz-3.1.1/miniz.c
QA_MINIZ_I := -Ithird_party/libchdr/deps/miniz-3.1.1
SEE_SOURCES := $(wildcard enhance/*.c)

$(QA_BIN): tools/armsx_qa.c $(TEST_CORE_SOURCES) $(SEE_SOURCES) $(TEST_PLATFORM_FILE_OBJ) $(QA_MINIZ_C)
	mkdir -p $(dir $@)
	$(CC) -std=gnu11 -O2 -g -DPSXE_DIAG_STDIO_DISABLE -I. -Ipsx $(QA_MINIZ_I) \
		tools/armsx_qa.c $(TEST_CORE_SOURCES) $(SEE_SOURCES) $(TEST_PLATFORM_FILE_OBJ) $(QA_MINIZ_C) -lm -o $@

TEST_SEE_BIN := build/tests/see_replacement
TEST_SEE_PACK_BIN := build/tests/see_pack_export

$(TEST_SEE_BIN): tests/see_replacement.c $(SEE_SOURCES) $(QA_MINIZ_C)
	mkdir -p $(dir $@)
	$(CC) -std=gnu11 -O2 -g -I. -Ipsx $(QA_MINIZ_I) $^ -lm -o $@

$(TEST_SEE_PACK_BIN): tests/see_pack_export.c $(SEE_SOURCES) $(QA_MINIZ_C)
	mkdir -p $(dir $@)
	$(CC) -std=gnu11 -O2 -g -I. -Ipsx $(QA_MINIZ_I) $^ -lm -o $@

test-see: $(TEST_SEE_BIN) $(TEST_SEE_PACK_BIN)
	./$(TEST_SEE_BIN)
	./$(TEST_SEE_PACK_BIN)

VK_INCLUDE := -Ithird_party/vulkan-headers/include
TEST_VK_BIN := build/tests/vk_vram_blit

$(TEST_VK_BIN): tests/vk_vram_blit.c vk/blit.c vk/raster.c psx/dev/gpu.c
	mkdir -p $(dir $@)
	$(CC) -std=c11 -O2 -g -DPSXE_DIAG_STDIO_DISABLE -I. -Ipsx $(VK_INCLUDE) $^ -l:libvulkan.so.1 -lm -o $@

test-vk: $(TEST_VK_BIN)
	./$(TEST_VK_BIN)

$(TEST_CPU_BIN): tests/cpu_differential.c $(TEST_CORE_SOURCES) $(TEST_PLATFORM_FILE_OBJ)
	mkdir -p $(dir $@)
	$(CC) -std=c11 -O2 -g -DPSXE_DIAG_STDIO_DISABLE -I. -Ipsx $^ -lm -o $@

test-cpu: $(TEST_CPU_BIN)
	./$(TEST_CPU_BIN)

$(TEST_GPU_BIN): tests/gpu_renderer_parity.c psx/dev/gpu.c frontend/gpu_hw.c
	mkdir -p $(dir $@)
	$(CC) -std=c11 -O2 -g -DUSE_GPU_BACKEND -DPSXE_DIAG_STDIO_DISABLE -I. -Ipsx $(SDL_CFLAGS) $^ -lm -o $@

test-gpu: $(TEST_GPU_BIN)
	./$(TEST_GPU_BIN)

$(TEST_AUDIO_BIN): tests/audio_timing.c
	mkdir -p $(dir $@)
	$(CC) -std=c11 -O2 -g -I. -Ipsx $< -lm -o $@

test-audio: $(TEST_AUDIO_BIN)
	./$(TEST_AUDIO_BIN)

$(TEST_SDL_AUDIO_BIN): tests/sdl_audio_queue_smoke.c
	mkdir -p $(dir $@)
	$(CC) -std=c11 -O2 -g $(SDL_CFLAGS) $< $(SDL_LIBS) -o $@

test-sdl-audio: $(TEST_SDL_AUDIO_BIN)
	./$(TEST_SDL_AUDIO_BIN)

$(TEST_CHD_BIN): tests/chd_logic.c $(TEST_PLATFORM_FILE_OBJ) $(CHD_BUILD_DEPS)
	mkdir -p $(dir $@)
	$(CC) -std=c11 -O2 -g -DUSE_CHD -DPSXE_DIAG_STDIO_DISABLE -I. -Ipsx $(LIBCHDR_INCLUDE_FLAGS) $< $(TEST_PLATFORM_FILE_OBJ) $(CHD_LINK_LIBS) -lm -o $@

test-chd: $(TEST_CHD_BIN)
	./$(TEST_CHD_BIN)

$(TEST_ZIP_BIN): tests/zip_integration.cpp frontend/archive.cpp $(TEST_PLATFORM_FILE_OBJ) $(CHD_BUILD_DEPS)
	mkdir -p $(dir $@)
	$(CXX) -std=c++20 -O0 -DUSE_CHD -I. $(LIBCHDR_INCLUDE_FLAGS) \
		tests/zip_integration.cpp frontend/archive.cpp $(TEST_PLATFORM_FILE_OBJ) $(CHD_LINK_LIBS) -o $@

test-zip: $(TEST_ZIP_BIN)
	python3 tests/validate_zip.py ./$(TEST_ZIP_BIN)

$(TEST_SDL_BIN): tests/sdl_renderer_smoke.c
	mkdir -p $(dir $@)
	$(CC) -std=c11 -O2 -g $(SDL_CFLAGS) $< $(SDL_LIBS) -o $@

test-sdl-runtime: $(TEST_SDL_BIN)
	./$(TEST_SDL_BIN)

$(DISC_PROBE_BIN): tests/disc_probe.c psx/dev/cdrom/disc.c psx/dev/cdrom/cue.c psx/dev/cdrom/list.c psx/dev/cdrom/chd.c $(CHD_BUILD_DEPS)
	mkdir -p $(dir $@)
	$(CC) -std=c11 -O2 -g -DUSE_CHD -DPSXE_DIAG_STDIO_DISABLE -I. -Ipsx $(LIBCHDR_INCLUDE_FLAGS) \
		tests/disc_probe.c psx/dev/cdrom/disc.c psx/dev/cdrom/cue.c psx/dev/cdrom/list.c psx/dev/cdrom/chd.c \
		$(CHD_LINK_LIBS) -lm -o $@

disc-probe: $(DISC_PROBE_BIN)

test:
	python3 tests/run_validation.py

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
		-DOS_INFO="$(OS_INFO)" \
		-DREP_VERSION="$(VERSION_TAG)" \
		-DREP_COMMIT_HASH="$(COMMIT_HASH)"

$(OBJ_DIR)/%.o: %.rc | $(OBJ_DIR)
	mkdir -p $(dir $@)
	$(WINDRES) -I. $< $@

ifeq ($(USE_CHD),1)
$(LIBCHDR_ARCHIVE): $(LIBCHDR_INPUTS)
	mkdir -p $(LIBCHDR_BUILD_DIR)
	$(LIBCHDR_CONFIGURE) $(LIBCHDR_CMAKE_ARGS)
	$(CMAKE) --build $(LIBCHDR_BUILD_DIR) -j$(BUILD_JOBS)
endif

$(RUNTIME_ICON_DEST): $(RUNTIME_ICON_SRC) | $(BIN_DIR)
	mkdir -p $(dir $@)
	cp $< $@

$(BIN): $(ALL_OBJS) $(RUNTIME_ICON_DEST) $(CHD_BUILD_DEPS) | $(BIN_DIR)
	$(CXX) $(ALL_OBJS) $(CHD_LINK_LIBS) $(FSUI_LIBS) -o $(BIN) $(SDL_LIBS) $(PLATFORM_EXTRA_LDFLAGS) $(PLATFORM_EXTRA_LIBS)

$(SHARED_BIN): $(ALL_OBJS_SHARED) $(CHD_BUILD_DEPS) | $(BIN_DIR)
	$(CXX) $(SHARED_LDFLAGS) $(ALL_OBJS_SHARED) $(CHD_LINK_LIBS) $(FSUI_LIBS) -o $(SHARED_BIN) $(SDL_LIBS_SHARED) $(PLATFORM_EXTRA_LDFLAGS) $(PLATFORM_EXTRA_LIBS)

clean:
	rm -rf "$(BIN_DIR)"
