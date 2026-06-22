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
# freetype + harfbuzz headers for the font module (system-installed).
INCLUDES += $(shell pkg-config --cflags freetype2 harfbuzz)
# OpenAL headers for the audio module's real backend (openal/*.cpp use <alc.h>).
INCLUDES += $(shell pkg-config --cflags openal)
# So the harness can locate the ported boot scripts (arg.rb/callbacks.rb/boot.rb).
CXXFLAGS += -DLOVE_SRC_DIR='"$(LOVE_SRC)"'
# The real graphics backend is OpenGL-only here; keep the Vulkan renderer (and
# its dynamic_casts in the SDL window backend) out of the link. Must be defined
# consistently for every TU that includes common/config.h.
CXXFLAGS += -DLOVE_MRUBY_NO_VULKAN
LDFLAGS  := -L$(MRUBY_LIB) -L$(SDL_LIBDIR) -Wl,-rpath,$(SDL_LIBDIR)
# The sound module's lullaby backend links system libvorbis + libmodplug (the
# Wave/FLAC/MP3 decoders are bundled — Wuff and dr_flac/dr_mp3, built below).
# The audio module's real backend is OpenAL (system libopenal). The video
# module's theora decode backend links system libtheoradec (+ libogg, above).
LDLIBS   := -lmruby -lSDL3 -lGL -lpthread -lm -lz -lfreetype -lharfbuzz \
            -lvorbisfile -lvorbis -logg -lmodplug -lopenal -ltheoradec

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

# --- Wuff (compiled as C for the sound module's WaveDecoder) --------------
WUFF_DIR  := $(LOVE_SRC)/libraries/Wuff
WUFF_SRCS := \
	$(WUFF_DIR)/wuff.c \
	$(WUFF_DIR)/wuff_convert.c \
	$(WUFF_DIR)/wuff_internal.c \
	$(WUFF_DIR)/wuff_memory.c
WUFF_OBJS := $(patsubst $(WUFF_DIR)/%.c,$(OBJ_DIR)/wuff_%.o,$(WUFF_SRCS))
WUFF_A    := $(OBJ_DIR)/libwuff.a

# --- xxHash (compiled as C; the graphics backend hashes FBO/shader keys) --
XXHASH_DIR  := $(LOVE_SRC)/libraries/xxHash
XXHASH_OBJS := $(OBJ_DIR)/xxhash.o
XXHASH_A    := $(OBJ_DIR)/libxxhash.a

# --- glslang (compiled as C++ for the graphics module's shader compiler) --
# The real graphics backend validates/reflects shaders through the bundled
# glslang (same source list as love's CMake love_3p_glslang target, Unix
# OSDependent only; the Web/Windows ossource variants are excluded).
GLSLANG_DIR  := $(LOVE_SRC)/libraries/glslang
GLSLANG_SRCS := $(shell find $(GLSLANG_DIR) -name '*.cpp' \
                  ! -path '*/OSDependent/Web/*' \
                  ! -path '*/OSDependent/Windows/*')
GLSLANG_OBJS := $(patsubst $(GLSLANG_DIR)/%.cpp,$(OBJ_DIR)/glslang/%.o,$(GLSLANG_SRCS))
GLSLANG_A    := $(OBJ_DIR)/libglslang.a

# --- graphics (real backend: core + opengl + glad + video base) ----------
# The real shader-based batched renderer. Built as an archive (selective
# linking) so the harness only recompiles the Ruby binding (wrap_*_mrb.cpp)
# on each rebuild. glad is the GL loader; video/VideoStream is the abstract
# base graphics/Video.cpp links against (no theora).
GFX_SRCS := \
	$(filter-out %wrap_Graphics_mrb.cpp,$(wildcard $(LOVE_SRC)/modules/graphics/*.cpp) ) \
	$(wildcard $(LOVE_SRC)/modules/graphics/opengl/*.cpp) \
	$(LOVE_SRC)/libraries/glad/glad.cpp \
	$(LOVE_SRC)/modules/video/VideoStream.cpp
# Drop the other Lua wrap_*.cpp (Buffer/Font/Graphics/Mesh/... ) — those are
# the unported Lua bindings; only the _mrb binding (in MODULE_SRCS) is used.
GFX_SRCS := $(filter-out %wrap_Buffer.cpp %wrap_Font.cpp %wrap_Graphics.cpp \
	%wrap_GraphicsReadback.cpp %wrap_Mesh.cpp %wrap_ParticleSystem.cpp \
	%wrap_Quad.cpp %wrap_Shader.cpp %wrap_SpriteBatch.cpp %wrap_TextBatch.cpp \
	%wrap_Texture.cpp %wrap_Video.cpp,$(GFX_SRCS))
GFX_OBJS := $(patsubst $(LOVE_SRC)/%.cpp,$(OBJ_DIR)/gfx/%.o,$(GFX_SRCS))
GFX_A    := $(OBJ_DIR)/libgfx.a

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
	$(LOVE_SRC)/common/utf8.cpp \
	$(LOVE_SRC)/common/memory.cpp \
	$(LOVE_SRC)/common/deprecation.cpp

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
	$(LOVE_SRC)/modules/thread/ThreadModule.cpp \
	$(LOVE_SRC)/modules/thread/Channel.cpp \
	$(LOVE_SRC)/modules/thread/LuaThread_mrb.cpp \
	$(LOVE_SRC)/modules/thread/wrap_ThreadModule_mrb.cpp \
	$(LOVE_SRC)/modules/event/wrap_Event_mrb.cpp \
	$(LOVE_SRC)/modules/event/Event.cpp \
	$(LOVE_SRC)/common/Variant.cpp \
	$(LOVE_SRC)/modules/window/wrap_Window_mrb.cpp \
	$(LOVE_SRC)/modules/window/Window.cpp \
	$(LOVE_SRC)/modules/window/sdl/Window.cpp \
	$(LOVE_SRC)/modules/graphics/wrap_Graphics_mrb.cpp \
	$(LOVE_SRC)/modules/keyboard/wrap_Keyboard_mrb.cpp \
	$(LOVE_SRC)/modules/keyboard/Keyboard.cpp \
	$(LOVE_SRC)/modules/keyboard/sdl/Keyboard.cpp \
	$(LOVE_SRC)/modules/mouse/wrap_Mouse_mrb.cpp \
	$(LOVE_SRC)/modules/mouse/Cursor.cpp \
	$(LOVE_SRC)/modules/mouse/sdl/Cursor.cpp \
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
	$(LOVE_SRC)/modules/image/magpie/ASTCHandler.cpp \
	$(LOVE_SRC)/modules/font/wrap_Font_mrb.cpp \
	$(LOVE_SRC)/modules/font/Font.cpp \
	$(LOVE_SRC)/modules/font/GlyphData.cpp \
	$(LOVE_SRC)/modules/font/Rasterizer.cpp \
	$(LOVE_SRC)/modules/font/TrueTypeRasterizer.cpp \
	$(LOVE_SRC)/modules/font/BMFontRasterizer.cpp \
	$(LOVE_SRC)/modules/font/ImageRasterizer.cpp \
	$(LOVE_SRC)/modules/font/TextShaper.cpp \
	$(LOVE_SRC)/modules/font/GenericShaper.cpp \
	$(LOVE_SRC)/modules/font/freetype/Font.cpp \
	$(LOVE_SRC)/modules/font/freetype/TrueTypeRasterizer.cpp \
	$(LOVE_SRC)/modules/font/freetype/HarfbuzzShaper.cpp \
	$(LOVE_SRC)/modules/data/DataStream.cpp \
	$(LOVE_SRC)/modules/sound/wrap_Sound_mrb.cpp \
	$(LOVE_SRC)/modules/sound/Sound.cpp \
	$(LOVE_SRC)/modules/sound/SoundData.cpp \
	$(LOVE_SRC)/modules/sound/Decoder.cpp \
	$(LOVE_SRC)/modules/sound/lullaby/Sound.cpp \
	$(LOVE_SRC)/modules/sound/lullaby/WaveDecoder.cpp \
	$(LOVE_SRC)/modules/sound/lullaby/FLACDecoder.cpp \
	$(LOVE_SRC)/modules/sound/lullaby/VorbisDecoder.cpp \
	$(LOVE_SRC)/modules/sound/lullaby/MP3Decoder.cpp \
	$(LOVE_SRC)/modules/sound/lullaby/ModPlugDecoder.cpp \
	$(LOVE_SRC)/modules/audio/wrap_Audio_mrb.cpp \
	$(LOVE_SRC)/modules/audio/Audio.cpp \
	$(LOVE_SRC)/modules/audio/Source.cpp \
	$(LOVE_SRC)/modules/audio/Effect.cpp \
	$(LOVE_SRC)/modules/audio/Filter.cpp \
	$(LOVE_SRC)/modules/audio/RecordingDevice.cpp \
	$(LOVE_SRC)/modules/audio/openal/Audio.cpp \
	$(LOVE_SRC)/modules/audio/openal/Source.cpp \
	$(LOVE_SRC)/modules/audio/openal/Pool.cpp \
	$(LOVE_SRC)/modules/audio/openal/Effect.cpp \
	$(LOVE_SRC)/modules/audio/openal/Filter.cpp \
	$(LOVE_SRC)/modules/audio/openal/RecordingDevice.cpp \
	$(LOVE_SRC)/modules/audio/null/Audio.cpp \
	$(LOVE_SRC)/modules/audio/null/Source.cpp \
	$(LOVE_SRC)/modules/audio/null/RecordingDevice.cpp \
	$(LOVE_SRC)/modules/touch/wrap_Touch_mrb.cpp \
	$(LOVE_SRC)/modules/touch/Touch.cpp \
	$(LOVE_SRC)/modules/touch/sdl/Touch.cpp \
	$(LOVE_SRC)/modules/sensor/wrap_Sensor_mrb.cpp \
	$(LOVE_SRC)/modules/sensor/Sensor.cpp \
	$(LOVE_SRC)/modules/sensor/sdl/Sensor.cpp \
	$(LOVE_SRC)/modules/joystick/wrap_JoystickModule_mrb.cpp \
	$(LOVE_SRC)/modules/joystick/Joystick.cpp \
	$(LOVE_SRC)/modules/joystick/sdl/Joystick.cpp \
	$(LOVE_SRC)/modules/joystick/sdl/JoystickModule.cpp \
	$(LOVE_SRC)/modules/video/wrap_Video_mrb.cpp \
	$(LOVE_SRC)/modules/video/theora/Video.cpp \
	$(LOVE_SRC)/modules/video/theora/TheoraVideoStream.cpp \
	$(LOVE_SRC)/modules/video/theora/OggDemuxer.cpp

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

$(BIN): $(SRCS) $(MRUBY_A) $(PHYSFS_A) $(LZ4_A) $(WUFF_A) $(GFX_A) $(GLSLANG_A) $(XXHASH_A)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(SRCS) $(PHYSFS_A) $(LZ4_A) $(WUFF_A) $(GFX_A) $(GLSLANG_A) $(XXHASH_A) $(LDFLAGS) $(LDLIBS) -o $@

$(OBJ_DIR)/xxhash.o: $(XXHASH_DIR)/xxhash.c | $(OBJ_DIR)
	$(CC) -O2 -I$(XXHASH_DIR) -c $< -o $@

$(XXHASH_A): $(XXHASH_OBJS)
	ar rcs $@ $(XXHASH_OBJS)

# glslang objects keep their source subdir layout under $(OBJ_DIR)/glslang.
$(OBJ_DIR)/glslang/%.o: $(GLSLANG_DIR)/%.cpp | $(OBJ_DIR)
	@mkdir -p $(dir $@)
	$(CXX) -std=$(CXXSTD) -O2 -I$(LOVE_SRC) -I$(GLSLANG_DIR) -c $< -o $@

$(GLSLANG_A): $(GLSLANG_OBJS)
	ar rcs $@ $(GLSLANG_OBJS)

# graphics objects keep their source subdir layout under $(OBJ_DIR)/gfx.
$(OBJ_DIR)/gfx/%.o: $(LOVE_SRC)/%.cpp | $(OBJ_DIR)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

$(GFX_A): $(GFX_OBJS)
	ar rcs $@ $(GFX_OBJS)

$(OBJ_DIR)/physfs_%.o: $(PHYSFS_DIR)/%.c | $(OBJ_DIR)
	$(CC) -O2 -I$(PHYSFS_DIR) -c $< -o $@

$(PHYSFS_A): $(PHYSFS_OBJS)
	ar rcs $@ $(PHYSFS_OBJS)

$(OBJ_DIR)/lz4_%.o: $(LZ4_DIR)/%.c | $(OBJ_DIR)
	$(CC) -O2 -I$(LZ4_DIR) -c $< -o $@

$(LZ4_A): $(LZ4_OBJS)
	ar rcs $@ $(LZ4_OBJS)

$(OBJ_DIR)/wuff_%.o: $(WUFF_DIR)/%.c | $(OBJ_DIR)
	$(CC) -O2 -I$(WUFF_DIR) -c $< -o $@

$(WUFF_A): $(WUFF_OBJS)
	ar rcs $@ $(WUFF_OBJS)

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
