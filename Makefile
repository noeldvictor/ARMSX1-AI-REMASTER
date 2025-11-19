.ONESHELL:

SDL_CONFIG ?= sdl2-config

CC ?= gcc
CXX ?= g++

SDL_CFLAGS ?= $(shell $(SDL_CONFIG) --cflags)
SDL_LIBS_DYNAMIC ?= $(shell $(SDL_CONFIG) --libs)
SDL_LIBS_STATIC ?= $(shell $(SDL_CONFIG) --static --libs 2>/dev/null)
SDL_STATIC ?= 1

PLATFORM := $(shell uname -s)
IOS_TARGET ?= 0
IOS_SDK ?= iphoneos
IOS_DEPLOYMENT_TARGET ?= 13.0
IOS_SDL_FRAMEWORK ?= $(CURDIR)/ios/Frameworks/SDL2.xcframework/ios-arm64/SDL2.framework
IOS_SDL_FRAMEWORK_PARENT := $(dir $(IOS_SDL_FRAMEWORK))
SDKROOT ?=
IMGUI_FRONTEND ?= 0

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
BASE_CFLAGS += -Ofast -Wno-overflow -Wall -pedantic -Wno-address-of-packed-member -flto

BASE_CXXFLAGS = -std=c++17 $(BASE_CFLAGS)

ifeq ($(IOS_TARGET),1)
	BASE_CFLAGS += -fembed-bitcode
	BASE_CXXFLAGS += -fembed-bitcode
endif

ifeq ($(IMGUI_FRONTEND),1)
ifneq ($(IOS_TARGET),1)
ifneq ($(filter shared,$(MAKECMDGOALS)),)
$(warning IMGUI_FRONTEND is disabled for shared builds)
else
	BASE_CFLAGS += -DIMGUI_FRONTEND
	BASE_CXXFLAGS += -DIMGUI_FRONTEND
endif
endif
endif

ifneq ($(IOS_TARGET),1)
OS_INFO := $(shell uname -rmo)
else
OS_INFO := iOS
endif

ifeq ($(PLATFORM),Darwin)
ifneq ($(IOS_TARGET),1)
	BASE_CFLAGS += -mmacosx-version-min=10.9 -Wno-newline-eof
endif
endif

SHARED_EXT := .so
SHARED_LDFLAGS := -shared
SHARED_CFLAGS := $(BASE_CFLAGS) -D__DLL_BUILD -fPIC
SHARED_CXXFLAGS := $(BASE_CXXFLAGS) -D__DLL_BUILD -fPIC

ifeq ($(PLATFORM),Darwin)
	SHARED_EXT := .dylib
	SHARED_LDFLAGS := -dynamiclib
endif

ifeq ($(IOS_TARGET),1)
	SHARED_LDFLAGS += -Wl,-install_name,@rpath/libpsxe$(SHARED_EXT)
	SHARED_CFLAGS += -DIOS_TARGET
	SHARED_CXXFLAGS += -DIOS_TARGET
endif

VERSION_TAG := $(shell git describe --always --tags --abbrev=0)
COMMIT_HASH := $(shell git rev-parse --short HEAD)

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
C_SOURCES_SHARED := $(C_SOURCES)

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
	$(CXX) $(SHARED_LDFLAGS) $(ALL_OBJS_SHARED) -o $(SHARED_BIN) $(SDL_LIBS_SHARED)

clean:
	rm -rf "$(BIN_DIR)"
