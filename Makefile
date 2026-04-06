.ONESHELL:

WASM_TARGET := $(filter wasm,$(MAKECMDGOALS))

ifeq ($(WASM_TARGET),wasm)
	CC := emcc
	CXX := em++
	SDL_STATIC := 0
	SDL_CFLAGS := -sUSE_SDL=2
	SDL_LIBS_DYNAMIC :=
	SDL_LIBS_STATIC :=
	WASM_LDFLAGS := -sUSE_SDL=2 -sALLOW_MEMORY_GROWTH=1 -sASSERTIONS=1 -sFORCE_FILESYSTEM=1 -sFULL_ES3=1 -sMIN_WEBGL_VERSION=2 -sMAX_WEBGL_VERSION=2 -sNO_EXIT_RUNTIME=0 -sEXPORTED_RUNTIME_METHODS='["FS","ccall","cwrap"]' -sEXPORTED_FUNCTIONS='["_main","_psxe_run","_external_main","_psxe_wasm_on_file"]' --preload-file icons@/icons
endif

SDL_CONFIG ?= sdl2-config

CC ?= gcc
CXX ?= g++

SDL_CFLAGS ?= $(shell $(SDL_CONFIG) --cflags 2>/dev/null)
SDL_LIBS_DYNAMIC ?= $(shell $(SDL_CONFIG) --libs 2>/dev/null)
SDL_LIBS_STATIC ?= $(shell $(SDL_CONFIG) --static-libs 2>/dev/null || $(SDL_CONFIG) --libs --static 2>/dev/null)
SDL_STATIC ?= 1
WASM_LDFLAGS ?=

PLATFORM := $(shell uname -s)
ifeq ($(WASM_TARGET),wasm)
PLATFORM := Emscripten
endif
IOS_TARGET ?= 0
IOS_SDK ?= iphoneos
IOS_DEPLOYMENT_TARGET ?= 14.0
MACOS_DEPLOYMENT_TARGET ?= 10.15
WINDOWS_TARGET ?= 0
UWP_TARGET ?= 0
PSVITA_TARGET ?= 0
VITASDK ?=
IOS_SDL_FRAMEWORK ?= $(CURDIR)/ios/Frameworks/SDL2.xcframework/ios-arm64/SDL2.framework
IOS_SDL_FRAMEWORK_PARENT := $(dir $(IOS_SDL_FRAMEWORK))
SDKROOT ?=
CONTROLLER_GENERIC ?= 0
FSUI_DIR := third_party/fsui-lib
FSUI_BUILD_DIR ?= build/fsui/native
WASM_HTML_POSTPROCESS := $(FSUI_DIR)/cmake/postprocess_web_html.cmake

ifeq ($(PSVITA_TARGET),1)
	ifeq ($(strip $(VITASDK)),)
$(error VITASDK must be set when PSVITA_TARGET=1)
	endif
	SDL_CONFIG := $(VITASDK)/bin/arm-vita-eabi-pkg-config
	CC := $(VITASDK)/bin/arm-vita-eabi-gcc
	CXX := $(VITASDK)/bin/arm-vita-eabi-g++
	AR ?= $(VITASDK)/bin/arm-vita-eabi-ar
	RANLIB ?= $(VITASDK)/bin/arm-vita-eabi-ranlib
	PLATFORM := Vita
	SDL_STATIC := 1
endif

ifeq ($(IOS_TARGET),1)
	SDKROOT ?= $(shell xcrun --sdk $(IOS_SDK) --show-sdk-path)
	CC ?= $(shell xcrun --sdk $(IOS_SDK) --find clang)
	CXX ?= $(shell xcrun --sdk $(IOS_SDK) --find clang++)

	# Force dynamic SDL when targeting iOS
	SDL_STATIC := 0
	SDL_CFLAGS := -isysroot $(SDKROOT) -arch arm64 -miphoneos-version-min=$(IOS_DEPLOYMENT_TARGET) \
		-F$(IOS_SDL_FRAMEWORK_PARENT) -I$(IOS_SDL_FRAMEWORK)/Headers -DIOS_TARGET -D_THREAD_SAFE
	SDL_LIBS_DYNAMIC := -isysroot $(SDKROOT) -arch arm64 -miphoneos-version-min=$(IOS_DEPLOYMENT_TARGET) \
		-F$(IOS_SDL_FRAMEWORK_PARENT) -framework SDL2
	SDL_LIBS_STATIC :=
endif

BASE_CFLAGS = -g -DLOG_USE_COLOR -I"." -I"psx" $(SDL_CFLAGS)
BASE_CFLAGS += -O3 -ffast-math -Wno-overflow -Wall -pedantic -Wno-address-of-packed-member -flto

FSUI_INCLUDE_FLAGS = \
	-I$(FSUI_DIR)/include \
	-I$(FSUI_DIR)/third_party/imgui \
	-I$(FSUI_DIR)/third_party/imgui/backends \
	-I$(FSUI_DIR)/third_party/stb

ifneq ($(WASM_TARGET),wasm)
FSUI_INCLUDE_FLAGS += -I$(FSUI_BUILD_DIR)/gladsources/fsui_glad/include
endif

FSUI_COMPILE_DEFS = -DIMGUI_FRONTEND -DFSUI_HAS_SDL2_PLATFORM -DFSUI_HAS_SDL2SURFACE_RENDERER -DFSUI_HAS_SDL2RENDERER_RENDERER -DSDL_MAIN_HANDLED

BASE_CXXFLAGS = -std=c++20 $(BASE_CFLAGS) $(FSUI_INCLUDE_FLAGS) $(FSUI_COMPILE_DEFS)

ifeq ($(CONTROLLER_GENERIC),1)
	BASE_CFLAGS += -DCONTROLLER_GENERIC
	BASE_CXXFLAGS += -DCONTROLLER_GENERIC
endif

ifeq ($(UWP_TARGET),1)
		BASE_CFLAGS += -DUWP_TARGET
		BASE_CXXFLAGS += -DUWP_TARGET
endif

ifeq ($(PSVITA_TARGET),1)
	BASE_CFLAGS += -DPSVITA_TARGET -D__DLL_BUILD
	BASE_CXXFLAGS += -DPSVITA_TARGET -D__DLL_BUILD
endif

ifeq ($(WASM_TARGET),wasm)
		BASE_CFLAGS := $(filter-out -flto,$(BASE_CFLAGS))
		BASE_CXXFLAGS := $(filter-out -flto,$(BASE_CXXFLAGS))
endif

ifeq ($(PSVITA_TARGET),1)
	BASE_CFLAGS := $(filter-out -flto,$(BASE_CFLAGS))
	BASE_CXXFLAGS := $(filter-out -flto,$(BASE_CXXFLAGS))
endif

ifeq ($(IOS_TARGET),1)
	BASE_CFLAGS += -fembed-bitcode
	BASE_CXXFLAGS += -fembed-bitcode
endif

ifeq ($(WASM_TARGET),wasm)
OS_INFO := Emscripten
else ifeq ($(PSVITA_TARGET),1)
OS_INFO := PS Vita
else ifneq ($(IOS_TARGET),1)
OS_INFO := $(shell uname -rmo)
else
OS_INFO := iOS
endif

ifeq ($(PLATFORM),Darwin)
ifneq ($(IOS_TARGET),1)
	BASE_CFLAGS += -mmacosx-version-min=$(MACOS_DEPLOYMENT_TARGET) -Wno-newline-eof
	BASE_CXXFLAGS += -mmacosx-version-min=$(MACOS_DEPLOYMENT_TARGET)
endif
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

ifeq ($(IOS_TARGET),1)
	SHARED_LDFLAGS += -Wl,-install_name,@rpath/libarmsx$(SHARED_EXT)
	SHARED_CFLAGS += -DIOS_TARGET
	SHARED_CXXFLAGS += -DIOS_TARGET
endif

VERSION_TAG := $(shell git describe --always --tags --abbrev=0)
COMMIT_HASH := $(shell git rev-parse --short HEAD)

BIN_DIR := bin
ifeq ($(WASM_TARGET),wasm)
BIN_DIR := bin/wasm
else ifeq ($(PSVITA_TARGET),1)
BIN_DIR := bin/psvita
endif
OBJ_DIR := $(BIN_DIR)/obj

BIN      := $(BIN_DIR)/armsx
ifeq ($(WASM_TARGET),wasm)
BIN      := $(BIN_DIR)/armsx.html
else ifeq ($(WINDOWS_TARGET),1)
BIN      := $(BIN_DIR)/armsx.exe
endif
SHARED_BIN := $(BIN_DIR)/libarmsx$(SHARED_EXT)
VITA_NATIVE_LIB := $(BIN_DIR)/libarmsx_vita.a
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
             frontend/toml.c
C_SOURCES_SHARED := $(C_SOURCES)

CPP_SOURCES := frontend/main.cpp

FSUI_LIBS := \
	$(FSUI_BUILD_DIR)/libfsui-donor.a \
	$(FSUI_BUILD_DIR)/libfsui-backend-sdl.a \
	$(FSUI_BUILD_DIR)/libfsui-platform-sdl2.a \
	$(FSUI_BUILD_DIR)/libfsui-renderer-sdl2.a \
	$(FSUI_BUILD_DIR)/libfsui-renderer-sdl2surface.a \
	$(FSUI_BUILD_DIR)/libfsui-core.a \
	$(FSUI_BUILD_DIR)/libfsui_imgui.a \
	$(FSUI_BUILD_DIR)/libfsui_resources.a

ifeq ($(PSVITA_TARGET),1)
FSUI_LIBS += \
	$(FSUI_BUILD_DIR)/libfsui-renderer-opengl.a \
	$(FSUI_BUILD_DIR)/libfsui_glad.a
else

ifneq ($(WASM_TARGET),wasm)
FSUI_LIBS += \
	$(FSUI_BUILD_DIR)/libfsui-renderer-opengl.a \
	$(FSUI_BUILD_DIR)/libfsui_glad.a
endif
endif

PLATFORM_EXTRA_LDFLAGS :=
PLATFORM_EXTRA_LIBS :=

ifeq ($(PLATFORM),Darwin)
	FSUI_LIBS += $(FSUI_BUILD_DIR)/libfsui-renderer-metal.a
	ifeq ($(IOS_TARGET),1)
		PLATFORM_EXTRA_LIBS += -framework Foundation -framework Metal -framework OpenGLES -framework QuartzCore -framework UIKit
	else
		PLATFORM_EXTRA_LDFLAGS += -mmacosx-version-min=$(MACOS_DEPLOYMENT_TARGET)
		PLATFORM_EXTRA_LIBS += -framework Cocoa -framework Foundation -framework IOKit -framework Metal -framework OpenGL -framework QuartzCore
	endif
else ifeq ($(WINDOWS_TARGET),1)
	PLATFORM_EXTRA_LIBS += -lsetupapi -limm32 -lversion -lwinmm -lgdi32 -lole32 -loleaut32 -lshell32 -luuid -lopengl32
ifneq ($(UWP_TARGET),1)
	PLATFORM_EXTRA_LIBS += -ldbghelp
endif
else ifneq ($(WASM_TARGET),wasm)
	PLATFORM_EXTRA_LIBS += -ldl -lGL
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
endif

ifeq ($(SDL_STATIC),1)
	ifeq ($(SDL_LIBS_STATIC),)
$(warning Static SDL2 libraries not found; falling back to dynamic SDL2)
		SDL_STATIC := 0
	endif
endif

SDL_LIBS := $(if $(filter 1,$(SDL_STATIC)),$(SDL_LIBS_STATIC),$(SDL_LIBS_DYNAMIC))
SDL_LIBS_SHARED := $(SDL_LIBS_DYNAMIC)

.PHONY: all clean shared wasm psvita-lib

all: $(BIN)

shared: $(SHARED_BIN)

wasm: $(BIN)

psvita-lib: $(VITA_NATIVE_LIB)

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

$(RUNTIME_ICON_DEST): $(RUNTIME_ICON_SRC) | $(BIN_DIR)
	mkdir -p $(dir $@)
	cp $< $@

$(BIN): $(ALL_OBJS) $(RUNTIME_ICON_DEST) | $(BIN_DIR)
	$(CXX) $(ALL_OBJS) $(FSUI_LIBS) -o $(BIN) $(SDL_LIBS) $(PLATFORM_EXTRA_LDFLAGS) $(PLATFORM_EXTRA_LIBS) $(WASM_LDFLAGS)
ifeq ($(WASM_TARGET),wasm)
	cmake -DINPUT_FILE="$(BIN)" -P "$(WASM_HTML_POSTPROCESS)"
endif

$(VITA_NATIVE_LIB): $(ALL_OBJS) $(RUNTIME_ICON_DEST) | $(BIN_DIR)
	$(AR) rcs $@ $(ALL_OBJS)
	$(RANLIB) $@

$(SHARED_BIN): $(ALL_OBJS_SHARED) | $(BIN_DIR)
	$(CXX) $(SHARED_LDFLAGS) $(ALL_OBJS_SHARED) $(FSUI_LIBS) -o $(SHARED_BIN) $(SDL_LIBS_SHARED) $(PLATFORM_EXTRA_LDFLAGS) $(PLATFORM_EXTRA_LIBS)

clean:
	rm -rf "$(BIN_DIR)"
