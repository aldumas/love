# Makefile for the mruby-port standalone harness.
#
#   make            build the harness (testing/mruby/love_mrb_harness)
#   make run        build, then run it against sample.rb
#   make mruby      build libmruby.a via the LÖVE build config
#   make clean      remove the harness binary and intermediate objects
#
# Override paths if your checkout differs:
#   make MRUBY_DIR=/path/to/mruby SDL_LIBDIR=/path/to/sdl/lib

# --- Paths ---------------------------------------------------------------
# Directory containing this Makefile (testing/mruby).
HERE      := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
LOVE_ROOT := $(abspath $(HERE)/../..)
LOVE_SRC  := $(LOVE_ROOT)/src
MRUBY_DIR ?= $(abspath $(LOVE_ROOT)/../mruby)
OBJ_DIR   := $(HERE).obj

MRUBY_INC  := $(MRUBY_DIR)/include
MRUBY_GEN  := $(MRUBY_DIR)/build/host/include
MRUBY_LIB  := $(MRUBY_DIR)/build/host/lib
MRUBY_A    := $(MRUBY_LIB)/libmruby.a

# SDL3 (used by the physfs-backed filesystem module). Built in this tree at
# /home/adam/src/SDL/build and also installed under /usr/local/lib.
SDL_LIBDIR ?= /usr/local/lib

BIN    := $(HERE)love_mrb_harness
SCRIPT ?= $(HERE)sample.rb

# --- Toolchain -----------------------------------------------------------
CC       ?= gcc
CXX      ?= g++
CXXSTD   ?= c++17
CXXFLAGS ?= -std=$(CXXSTD) -O2 -Wall
INCLUDES := -I$(LOVE_SRC) -I$(LOVE_SRC)/modules -I$(LOVE_SRC)/libraries -I$(MRUBY_INC) -I$(MRUBY_GEN)
# So the harness can locate the ported boot scripts (arg.rb/callbacks.rb/boot.rb).
CXXFLAGS += -DLOVE_SRC_DIR='"$(LOVE_SRC)"'
LDFLAGS  := -L$(MRUBY_LIB) -L$(SDL_LIBDIR) -Wl,-rpath,$(SDL_LIBDIR)
LDLIBS   := -lmruby -lSDL3 -lGL -lpthread -lm -lz

# --- physfs (compiled as C into a static archive) ------------------------
PHYSFS_DIR  := $(LOVE_SRC)/libraries/physfs
PHYSFS_SRCS := \
	$(PHYSFS_DIR)/physfs.c \
	$(PHYSFS_DIR)/physfs_byteorder.c \
	$(PHYSFS_DIR)/physfs_unicode.c \
	$(PHYSFS_DIR)/physfs_platform_posix.c \
	$(PHYSFS_DIR)/physfs_platform_unix.c \
	$(wildcard $(PHYSFS_DIR)/physfs_archiver_*.c)
PHYSFS_OBJS := $(patsubst $(PHYSFS_DIR)/%.c,$(OBJ_DIR)/physfs_%.o,$(PHYSFS_SRCS))
PHYSFS_A    := $(OBJ_DIR)/libphysfs.a

# --- lz4 (compiled as C for the data module's Compressor) ----------------
LZ4_DIR  := $(LOVE_SRC)/libraries/lz4
LZ4_SRCS := $(LZ4_DIR)/lz4.c $(LZ4_DIR)/lz4hc.c
LZ4_OBJS := $(patsubst $(LZ4_DIR)/%.c,$(OBJ_DIR)/lz4_%.o,$(LZ4_SRCS))
LZ4_A    := $(OBJ_DIR)/liblz4.a

# --- C++ sources ---------------------------------------------------------
CORE_SRCS := \
	$(HERE)harness.cpp \
	$(HERE)delay_stub.cpp \
	$(LOVE_SRC)/common/mrb_runtime.cpp \
	$(LOVE_SRC)/common/Module.cpp \
	$(LOVE_SRC)/common/Object.cpp \
	$(LOVE_SRC)/common/types.cpp \
	$(LOVE_SRC)/common/Exception.cpp \
	$(LOVE_SRC)/common/StringMap.cpp \
	$(LOVE_SRC)/common/Data.cpp \
	$(LOVE_SRC)/common/Stream.cpp \
	$(LOVE_SRC)/common/b64.cpp \
	$(LOVE_SRC)/common/utf8.cpp

# Ported module wrappers + their native implementations.
# Append here as you port more modules.
MODULE_SRCS := \
	$(LOVE_SRC)/modules/timer/wrap_Timer_mrb.cpp \
	$(LOVE_SRC)/modules/timer/Timer.cpp \
	$(LOVE_SRC)/modules/math/wrap_Math_mrb.cpp \
	$(LOVE_SRC)/modules/math/MathModule.cpp \
	$(LOVE_SRC)/modules/math/RandomGenerator.cpp \
	$(LOVE_SRC)/modules/math/BezierCurve.cpp \
	$(LOVE_SRC)/modules/math/Transform.cpp \
	$(LOVE_SRC)/common/Matrix.cpp \
	$(LOVE_SRC)/common/Vector.cpp \
	$(LOVE_SRC)/libraries/noise1234/noise1234.cpp \
	$(LOVE_SRC)/libraries/noise1234/simplexnoise1234.cpp \
	$(LOVE_SRC)/modules/filesystem/wrap_Filesystem_mrb.cpp \
	$(LOVE_SRC)/modules/filesystem/Filesystem.cpp \
	$(LOVE_SRC)/modules/filesystem/File.cpp \
	$(LOVE_SRC)/modules/filesystem/FileData.cpp \
	$(LOVE_SRC)/modules/filesystem/NativeFile.cpp \
	$(LOVE_SRC)/modules/filesystem/physfs/Filesystem.cpp \
	$(LOVE_SRC)/modules/filesystem/physfs/File.cpp \
	$(LOVE_SRC)/modules/filesystem/physfs/PhysfsIo.cpp \
	$(LOVE_SRC)/modules/data/ByteData.cpp \
	$(LOVE_SRC)/modules/thread/threads.cpp \
	$(LOVE_SRC)/modules/thread/sdl/threads.cpp \
	$(LOVE_SRC)/modules/thread/sdl/Thread.cpp \
	$(LOVE_SRC)/modules/event/wrap_Event_mrb.cpp \
	$(LOVE_SRC)/modules/event/Event.cpp \
	$(LOVE_SRC)/common/Variant.cpp \
	$(LOVE_SRC)/modules/window/wrap_Window_mrb.cpp \
	$(LOVE_SRC)/modules/window/Window.cpp \
	$(LOVE_SRC)/modules/graphics/wrap_Graphics_mrb.cpp \
	$(LOVE_SRC)/modules/keyboard/wrap_Keyboard_mrb.cpp \
	$(LOVE_SRC)/modules/mouse/wrap_Mouse_mrb.cpp \
	$(LOVE_SRC)/modules/system/wrap_System_mrb.cpp \
	$(LOVE_SRC)/modules/system/System.cpp \
	$(LOVE_SRC)/modules/system/sdl/System.cpp \
	$(LOVE_SRC)/modules/data/wrap_DataModule_mrb.cpp \
	$(LOVE_SRC)/modules/data/DataModule.cpp \
	$(LOVE_SRC)/modules/data/DataView.cpp \
	$(LOVE_SRC)/modules/data/CompressedData.cpp \
	$(LOVE_SRC)/modules/data/Compressor.cpp \
	$(LOVE_SRC)/modules/data/HashFunction.cpp \
	$(LOVE_SRC)/common/pixelformat.cpp \
	$(LOVE_SRC)/common/floattypes.cpp \
	$(LOVE_SRC)/libraries/lodepng/lodepng.cpp \
	$(LOVE_SRC)/libraries/ddsparse/ddsparse.cpp \
	$(LOVE_SRC)/modules/image/wrap_Image_mrb.cpp \
	$(LOVE_SRC)/modules/image/Image.cpp \
	$(LOVE_SRC)/modules/image/ImageData.cpp \
	$(LOVE_SRC)/modules/image/ImageDataBase.cpp \
	$(LOVE_SRC)/modules/image/CompressedImageData.cpp \
	$(LOVE_SRC)/modules/image/CompressedSlice.cpp \
	$(LOVE_SRC)/modules/image/FormatHandler.cpp \
	$(LOVE_SRC)/modules/image/magpie/PNGHandler.cpp \
	$(LOVE_SRC)/modules/image/magpie/STBHandler.cpp \
	$(LOVE_SRC)/modules/image/magpie/EXRHandler.cpp \
	$(LOVE_SRC)/modules/image/magpie/ddsHandler.cpp \
	$(LOVE_SRC)/modules/image/magpie/PVRHandler.cpp \
	$(LOVE_SRC)/modules/image/magpie/KTXHandler.cpp \
	$(LOVE_SRC)/modules/image/magpie/PKMHandler.cpp \
	$(LOVE_SRC)/modules/image/magpie/ASTCHandler.cpp

SRCS := $(CORE_SRCS) $(MODULE_SRCS)

# --- Targets -------------------------------------------------------------
.PHONY: all run mruby clean check-ledger install-hooks

all: $(BIN)

# Reconcile the deferral ledger (PORTING.md) against inline // TODO(mruby)
# markers. Run standalone or via the pre-commit hook (see install-hooks).
check-ledger:
	$(HERE)check_porting_ledger.sh

# Install the pre-commit hook that runs check-ledger. .git/hooks is not version
# controlled, so each clone runs this once.
install-hooks:
	@hooks="$$(git -C $(LOVE_ROOT) rev-parse --absolute-git-dir)/hooks"; \
	mkdir -p "$$hooks"; \
	printf '#!/bin/sh\nexec "%scheck_porting_ledger.sh"\n' '$(HERE)' > "$$hooks/pre-commit"; \
	chmod +x "$$hooks/pre-commit"; \
	echo "installed pre-commit hook -> $$hooks/pre-commit"

$(BIN): $(SRCS) $(MRUBY_A) $(PHYSFS_A) $(LZ4_A)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(SRCS) $(PHYSFS_A) $(LZ4_A) $(LDFLAGS) $(LDLIBS) -o $@

$(OBJ_DIR)/physfs_%.o: $(PHYSFS_DIR)/%.c | $(OBJ_DIR)
	$(CC) -O2 -I$(PHYSFS_DIR) -c $< -o $@

$(PHYSFS_A): $(PHYSFS_OBJS)
	ar rcs $@ $(PHYSFS_OBJS)

$(OBJ_DIR)/lz4_%.o: $(LZ4_DIR)/%.c | $(OBJ_DIR)
	$(CC) -O2 -I$(LZ4_DIR) -c $< -o $@

$(LZ4_A): $(LZ4_OBJS)
	ar rcs $@ $(LZ4_OBJS)

$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

# Build libmruby.a if it's missing. Requires a host Ruby on PATH.
$(MRUBY_A):
	cd $(MRUBY_DIR) && MRUBY_CONFIG=love ruby ./minirake

mruby:
	cd $(MRUBY_DIR) && MRUBY_CONFIG=love ruby ./minirake

run: $(BIN)
	$(BIN) $(SCRIPT)

clean:
	rm -rf $(BIN) $(OBJ_DIR)
