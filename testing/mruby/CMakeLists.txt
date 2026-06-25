# CMake-driven build of the mruby-port harness.
#
# Stage 0 of the CMake migration (see CMAKE_MIGRATION.md): reproduce the
# standalone harness Makefile under CMake, to validate the mruby CMake target +
# flag handling in isolation before folding it into the main CMakeLists.txt.
# It builds the exact same sources / archives / libs the Makefile does.
#
#   cmake -S testing/mruby -B testing/mruby/build
#   cmake --build testing/mruby/build -j
#   ./testing/mruby/build/love_mrb_harness testing/mruby/sample.rb
#
# Override paths:  -DMRUBY_DIR=/path/to/mruby  -DSDL_LIBDIR=/path/to/sdl/lib

cmake_minimum_required(VERSION 3.16)
project(love_mrb_harness CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# mruby is built -fPIC and CMake defaults to PIE executables; make every love
# object PIC too so the static archives link cleanly into the PIE harness.
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
if(NOT CMAKE_BUILD_TYPE)
	set(CMAKE_BUILD_TYPE Release)
endif()

# --- Paths ---------------------------------------------------------------
get_filename_component(LOVE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(LOVE_SRC "${LOVE_ROOT}/src")
set(HERE "${CMAKE_CURRENT_SOURCE_DIR}")

set(MRUBY_DIR  "${LOVE_ROOT}/../mruby" CACHE PATH "Path to the mruby tree")
set(SDL_LIBDIR "/usr/local/lib"        CACHE PATH "Directory containing libSDL3")

set(MRUBY_INC     "${MRUBY_DIR}/include")
set(MRUBY_GEN     "${MRUBY_DIR}/build/host/include")
set(MRUBY_LIB_DIR "${MRUBY_DIR}/build/host/lib")

find_library(MRUBY_LIB mruby PATHS "${MRUBY_LIB_DIR}" NO_DEFAULT_PATH)
if(NOT MRUBY_LIB)
	message(FATAL_ERROR
		"libmruby.a not found in ${MRUBY_LIB_DIR}.\n"
		"Build it first:  make -C ${HERE} mruby   (needs a host Ruby on PATH)")
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(FREETYPE REQUIRED freetype2)
pkg_check_modules(HARFBUZZ REQUIRED harfbuzz)
pkg_check_modules(OPENAL   REQUIRED openal)

# Defines + includes shared by every love TU (the engine .cpp + the _mrb
# bindings + the graphics archive). Mirrors the Makefile's CXXFLAGS/INCLUDES.
#   LOVE_MRUBY            - "this is the mruby build" (guards Lua-only sections)
#   LOVE_MRUBY_NO_VULKAN  - OpenGL-only renderer (no Vulkan dynamic_casts)
#   LOVE_SRC_DIR          - so the harness finds the ported boot scripts (dev-only)
set(LOVE_DEFS LOVE_MRUBY LOVE_MRUBY_NO_VULKAN "LOVE_SRC_DIR=\"${LOVE_SRC}\"")
set(LOVE_INCS
	${LOVE_SRC} ${LOVE_SRC}/modules ${LOVE_SRC}/libraries
	${MRUBY_INC} ${MRUBY_GEN}
	${FREETYPE_INCLUDE_DIRS} ${HARFBUZZ_INCLUDE_DIRS} ${OPENAL_INCLUDE_DIRS})

# =========================================================================
# Bundled libraries compiled into static archives (selective linking), each
# with only its own include needs -- exactly as the Makefile builds them.
# These deliberately do NOT get LOVE_DEFS (they don't include love's config in
# an ABI-relevant way; this matches the working Makefile per-target flags).
# =========================================================================

# physfs (C) -- the filesystem backend.
file(GLOB PHYSFS_ARCHIVERS ${LOVE_SRC}/libraries/physfs/physfs_archiver_*.c)
add_library(mrbh_physfs STATIC
	${LOVE_SRC}/libraries/physfs/physfs.c
	${LOVE_SRC}/libraries/physfs/physfs_byteorder.c
	${LOVE_SRC}/libraries/physfs/physfs_unicode.c
	${LOVE_SRC}/libraries/physfs/physfs_platform_posix.c
	${LOVE_SRC}/libraries/physfs/physfs_platform_unix.c
	${PHYSFS_ARCHIVERS})
target_include_directories(mrbh_physfs PRIVATE ${LOVE_SRC}/libraries/physfs)

# lz4 (C) -- the data module's Compressor.
add_library(mrbh_lz4 STATIC
	${LOVE_SRC}/libraries/lz4/lz4.c
	${LOVE_SRC}/libraries/lz4/lz4hc.c)
target_include_directories(mrbh_lz4 PRIVATE ${LOVE_SRC}/libraries/lz4)

# Wuff (C) -- the sound module's WaveDecoder.
add_library(mrbh_wuff STATIC
	${LOVE_SRC}/libraries/Wuff/wuff.c
	${LOVE_SRC}/libraries/Wuff/wuff_convert.c
	${LOVE_SRC}/libraries/Wuff/wuff_internal.c
	${LOVE_SRC}/libraries/Wuff/wuff_memory.c)
target_include_directories(mrbh_wuff PRIVATE ${LOVE_SRC}/libraries/Wuff)

# xxHash (C) -- the graphics backend hashes FBO/shader keys.
add_library(mrbh_xxhash STATIC ${LOVE_SRC}/libraries/xxHash/xxhash.c)
target_include_directories(mrbh_xxhash PRIVATE ${LOVE_SRC}/libraries/xxHash)

# box2d (C++) -- the physics module. b2_settings is patched to throw
# love::Exception, hence the love src include.
file(GLOB_RECURSE BOX2D_SRCS ${LOVE_SRC}/libraries/box2d/*.cpp)
add_library(mrbh_box2d STATIC ${BOX2D_SRCS})
target_include_directories(mrbh_box2d PRIVATE
	${LOVE_SRC} ${LOVE_SRC}/libraries ${LOVE_SRC}/libraries/box2d)

# glslang (C++) -- the graphics shader compiler. Unix OSDependent only.
file(GLOB_RECURSE GLSLANG_SRCS ${LOVE_SRC}/libraries/glslang/*.cpp)
list(FILTER GLSLANG_SRCS EXCLUDE REGEX "/OSDependent/Web/")
list(FILTER GLSLANG_SRCS EXCLUDE REGEX "/OSDependent/Windows/")
add_library(mrbh_glslang STATIC ${GLSLANG_SRCS})
target_include_directories(mrbh_glslang PRIVATE ${LOVE_SRC} ${LOVE_SRC}/libraries/glslang)

# graphics real backend (C++): core + opengl + glad + VideoStream. Drop the
# _mrb binding (built with the harness) and the unported Lua wrap_*.cpp.
file(GLOB GFX_CORE   ${LOVE_SRC}/modules/graphics/*.cpp)
file(GLOB GFX_OPENGL ${LOVE_SRC}/modules/graphics/opengl/*.cpp)
set(GFX_SRCS ${GFX_CORE} ${GFX_OPENGL}
	${LOVE_SRC}/libraries/glad/glad.cpp
	${LOVE_SRC}/modules/video/VideoStream.cpp)
list(FILTER GFX_SRCS EXCLUDE REGEX "wrap_Graphics_mrb\\.cpp$")
list(FILTER GFX_SRCS EXCLUDE REGEX
	"wrap_(Buffer|Font|Graphics|GraphicsReadback|Mesh|ParticleSystem|Quad|Shader|SpriteBatch|TextBatch|Texture|Video)\\.cpp$")
add_library(mrbh_gfx STATIC ${GFX_SRCS})
target_compile_definitions(mrbh_gfx PRIVATE ${LOVE_DEFS})
target_include_directories(mrbh_gfx PRIVATE ${LOVE_INCS})

# =========================================================================
# The harness executable: the harness driver + common runtime + every ported
# module wrapper and its engine .cpp. (The Makefile's CORE_SRCS + MODULE_SRCS.)
# =========================================================================
set(HARNESS_SRCS
	${HERE}/harness.cpp
	${HERE}/delay_stub.cpp
	${LOVE_SRC}/common/mrb_runtime.cpp
	${LOVE_SRC}/common/Module.cpp
	${LOVE_SRC}/common/Object.cpp
	${LOVE_SRC}/common/types.cpp
	${LOVE_SRC}/common/Exception.cpp
	${LOVE_SRC}/common/StringMap.cpp
	${LOVE_SRC}/common/Data.cpp
	${LOVE_SRC}/common/Stream.cpp
	${LOVE_SRC}/common/b64.cpp
	${LOVE_SRC}/common/utf8.cpp
	${LOVE_SRC}/common/memory.cpp
	${LOVE_SRC}/common/deprecation.cpp
	${LOVE_SRC}/common/Variant.cpp
	${LOVE_SRC}/common/Matrix.cpp
	${LOVE_SRC}/common/Vector.cpp
	${LOVE_SRC}/common/pixelformat.cpp
	${LOVE_SRC}/common/floattypes.cpp

	${LOVE_SRC}/modules/timer/wrap_Timer_mrb.cpp
	${LOVE_SRC}/modules/timer/Timer.cpp

	${LOVE_SRC}/modules/math/wrap_Math_mrb.cpp
	${LOVE_SRC}/modules/math/MathModule.cpp
	${LOVE_SRC}/modules/math/RandomGenerator.cpp
	${LOVE_SRC}/modules/math/BezierCurve.cpp
	${LOVE_SRC}/modules/math/Transform.cpp
	${LOVE_SRC}/libraries/noise1234/noise1234.cpp
	${LOVE_SRC}/libraries/noise1234/simplexnoise1234.cpp

	${LOVE_SRC}/modules/filesystem/wrap_Filesystem_mrb.cpp
	${LOVE_SRC}/modules/filesystem/Filesystem.cpp
	${LOVE_SRC}/modules/filesystem/File.cpp
	${LOVE_SRC}/modules/filesystem/FileData.cpp
	${LOVE_SRC}/modules/filesystem/NativeFile.cpp
	${LOVE_SRC}/modules/filesystem/physfs/Filesystem.cpp
	${LOVE_SRC}/modules/filesystem/physfs/File.cpp
	${LOVE_SRC}/modules/filesystem/physfs/PhysfsIo.cpp

	${LOVE_SRC}/modules/data/wrap_DataModule_mrb.cpp
	${LOVE_SRC}/modules/data/DataModule.cpp
	${LOVE_SRC}/modules/data/ByteData.cpp
	${LOVE_SRC}/modules/data/DataView.cpp
	${LOVE_SRC}/modules/data/CompressedData.cpp
	${LOVE_SRC}/modules/data/Compressor.cpp
	${LOVE_SRC}/modules/data/HashFunction.cpp
	${LOVE_SRC}/modules/data/DataStream.cpp

	${LOVE_SRC}/modules/thread/threads.cpp
	${LOVE_SRC}/modules/thread/sdl/threads.cpp
	${LOVE_SRC}/modules/thread/sdl/Thread.cpp
	${LOVE_SRC}/modules/thread/ThreadModule.cpp
	${LOVE_SRC}/modules/thread/Channel.cpp
	${LOVE_SRC}/modules/thread/LuaThread_mrb.cpp
	${LOVE_SRC}/modules/thread/wrap_ThreadModule_mrb.cpp

	${LOVE_SRC}/modules/event/wrap_Event_mrb.cpp
	${LOVE_SRC}/modules/event/Event.cpp
	${LOVE_SRC}/modules/event/sdl/Event.cpp

	${LOVE_SRC}/modules/window/wrap_Window_mrb.cpp
	${LOVE_SRC}/modules/window/Window.cpp
	${LOVE_SRC}/modules/window/sdl/Window.cpp

	${LOVE_SRC}/modules/graphics/wrap_Graphics_mrb.cpp

	${LOVE_SRC}/modules/keyboard/wrap_Keyboard_mrb.cpp
	${LOVE_SRC}/modules/keyboard/Keyboard.cpp
	${LOVE_SRC}/modules/keyboard/sdl/Keyboard.cpp

	${LOVE_SRC}/modules/mouse/wrap_Mouse_mrb.cpp
	${LOVE_SRC}/modules/mouse/sdl/Mouse.cpp
	${LOVE_SRC}/modules/mouse/Cursor.cpp
	${LOVE_SRC}/modules/mouse/sdl/Cursor.cpp

	${LOVE_SRC}/modules/system/wrap_System_mrb.cpp
	${LOVE_SRC}/modules/system/System.cpp
	${LOVE_SRC}/modules/system/sdl/System.cpp

	${LOVE_SRC}/libraries/lodepng/lodepng.cpp
	${LOVE_SRC}/libraries/ddsparse/ddsparse.cpp
	${LOVE_SRC}/modules/image/wrap_Image_mrb.cpp
	${LOVE_SRC}/modules/image/Image.cpp
	${LOVE_SRC}/modules/image/ImageData.cpp
	${LOVE_SRC}/modules/image/ImageDataBase.cpp
	${LOVE_SRC}/modules/image/CompressedImageData.cpp
	${LOVE_SRC}/modules/image/CompressedSlice.cpp
	${LOVE_SRC}/modules/image/FormatHandler.cpp
	${LOVE_SRC}/modules/image/magpie/PNGHandler.cpp
	${LOVE_SRC}/modules/image/magpie/STBHandler.cpp
	${LOVE_SRC}/modules/image/magpie/EXRHandler.cpp
	${LOVE_SRC}/modules/image/magpie/ddsHandler.cpp
	${LOVE_SRC}/modules/image/magpie/PVRHandler.cpp
	${LOVE_SRC}/modules/image/magpie/KTXHandler.cpp
	${LOVE_SRC}/modules/image/magpie/PKMHandler.cpp
	${LOVE_SRC}/modules/image/magpie/ASTCHandler.cpp

	${LOVE_SRC}/modules/font/wrap_Font_mrb.cpp
	${LOVE_SRC}/modules/font/Font.cpp
	${LOVE_SRC}/modules/font/GlyphData.cpp
	${LOVE_SRC}/modules/font/Rasterizer.cpp
	${LOVE_SRC}/modules/font/TrueTypeRasterizer.cpp
	${LOVE_SRC}/modules/font/BMFontRasterizer.cpp
	${LOVE_SRC}/modules/font/ImageRasterizer.cpp
	${LOVE_SRC}/modules/font/TextShaper.cpp
	${LOVE_SRC}/modules/font/GenericShaper.cpp
	${LOVE_SRC}/modules/font/freetype/Font.cpp
	${LOVE_SRC}/modules/font/freetype/TrueTypeRasterizer.cpp
	${LOVE_SRC}/modules/font/freetype/HarfbuzzShaper.cpp

	${LOVE_SRC}/modules/sound/wrap_Sound_mrb.cpp
	${LOVE_SRC}/modules/sound/Sound.cpp
	${LOVE_SRC}/modules/sound/SoundData.cpp
	${LOVE_SRC}/modules/sound/Decoder.cpp
	${LOVE_SRC}/modules/sound/lullaby/Sound.cpp
	${LOVE_SRC}/modules/sound/lullaby/WaveDecoder.cpp
	${LOVE_SRC}/modules/sound/lullaby/FLACDecoder.cpp
	${LOVE_SRC}/modules/sound/lullaby/VorbisDecoder.cpp
	${LOVE_SRC}/modules/sound/lullaby/MP3Decoder.cpp
	${LOVE_SRC}/modules/sound/lullaby/ModPlugDecoder.cpp

	${LOVE_SRC}/modules/audio/wrap_Audio_mrb.cpp
	${LOVE_SRC}/modules/audio/Audio.cpp
	${LOVE_SRC}/modules/audio/Source.cpp
	${LOVE_SRC}/modules/audio/Effect.cpp
	${LOVE_SRC}/modules/audio/Filter.cpp
	${LOVE_SRC}/modules/audio/RecordingDevice.cpp
	${LOVE_SRC}/modules/audio/openal/Audio.cpp
	${LOVE_SRC}/modules/audio/openal/Source.cpp
	${LOVE_SRC}/modules/audio/openal/Pool.cpp
	${LOVE_SRC}/modules/audio/openal/Effect.cpp
	${LOVE_SRC}/modules/audio/openal/Filter.cpp
	${LOVE_SRC}/modules/audio/openal/RecordingDevice.cpp
	${LOVE_SRC}/modules/audio/null/Audio.cpp
	${LOVE_SRC}/modules/audio/null/Source.cpp
	${LOVE_SRC}/modules/audio/null/RecordingDevice.cpp

	${LOVE_SRC}/modules/touch/wrap_Touch_mrb.cpp
	${LOVE_SRC}/modules/touch/Touch.cpp
	${LOVE_SRC}/modules/touch/sdl/Touch.cpp

	${LOVE_SRC}/modules/sensor/wrap_Sensor_mrb.cpp
	${LOVE_SRC}/modules/sensor/Sensor.cpp
	${LOVE_SRC}/modules/sensor/sdl/Sensor.cpp

	${LOVE_SRC}/modules/joystick/wrap_JoystickModule_mrb.cpp
	${LOVE_SRC}/modules/joystick/Joystick.cpp
	${LOVE_SRC}/modules/joystick/sdl/Joystick.cpp
	${LOVE_SRC}/modules/joystick/sdl/JoystickModule.cpp

	${LOVE_SRC}/modules/video/wrap_Video_mrb.cpp
	${LOVE_SRC}/modules/video/theora/Video.cpp
	${LOVE_SRC}/modules/video/theora/TheoraVideoStream.cpp
	${LOVE_SRC}/modules/video/theora/OggDemuxer.cpp

	${LOVE_SRC}/modules/physics/Body.cpp
	${LOVE_SRC}/modules/physics/Shape.cpp
	${LOVE_SRC}/modules/physics/Joint.cpp
	${LOVE_SRC}/modules/physics/box2d/wrap_Physics_mrb.cpp
	${LOVE_SRC}/modules/physics/box2d/Physics.cpp
	${LOVE_SRC}/modules/physics/box2d/World.cpp
	${LOVE_SRC}/modules/physics/box2d/Body.cpp
	${LOVE_SRC}/modules/physics/box2d/Shape.cpp
	${LOVE_SRC}/modules/physics/box2d/CircleShape.cpp
	${LOVE_SRC}/modules/physics/box2d/PolygonShape.cpp
	${LOVE_SRC}/modules/physics/box2d/EdgeShape.cpp
	${LOVE_SRC}/modules/physics/box2d/ChainShape.cpp
	${LOVE_SRC}/modules/physics/box2d/Joint.cpp
	${LOVE_SRC}/modules/physics/box2d/MouseJoint.cpp
	${LOVE_SRC}/modules/physics/box2d/DistanceJoint.cpp
	${LOVE_SRC}/modules/physics/box2d/PrismaticJoint.cpp
	${LOVE_SRC}/modules/physics/box2d/RevoluteJoint.cpp
	${LOVE_SRC}/modules/physics/box2d/PulleyJoint.cpp
	${LOVE_SRC}/modules/physics/box2d/GearJoint.cpp
	${LOVE_SRC}/modules/physics/box2d/FrictionJoint.cpp
	${LOVE_SRC}/modules/physics/box2d/WeldJoint.cpp
	${LOVE_SRC}/modules/physics/box2d/WheelJoint.cpp
	${LOVE_SRC}/modules/physics/box2d/RopeJoint.cpp
	${LOVE_SRC}/modules/physics/box2d/MotorJoint.cpp
	${LOVE_SRC}/modules/physics/box2d/Contact.cpp)

# Bake the ported boot scripts into the binary (Stage-3 embedding de-risk; see
# CMAKE_MIGRATION.md). Each .rb -> a generated <name>_rb.h C string via the
# robust hex generator, so the harness boots without reading them from disk.
set(LOVE_MODULE_DIR "${LOVE_SRC}/modules/love")
set(EMBED_DIR "${CMAKE_CURRENT_BINARY_DIR}/embedded")
file(MAKE_DIRECTORY ${EMBED_DIR})
set(EMBEDDED_HEADERS "")
foreach(script arg callbacks boot)
	set(out "${EMBED_DIR}/${script}_rb.h")
	add_custom_command(
		OUTPUT "${out}"
		COMMAND ${CMAKE_COMMAND}
			-DIN=${LOVE_MODULE_DIR}/${script}.rb -DOUT=${out} -DVAR=${script}_rb
			-P ${CMAKE_CURRENT_SOURCE_DIR}/embed_script.cmake
		DEPENDS "${LOVE_MODULE_DIR}/${script}.rb" "${CMAKE_CURRENT_SOURCE_DIR}/embed_script.cmake"
		COMMENT "Embedding ${script}.rb")
	list(APPEND EMBEDDED_HEADERS "${out}")
endforeach()

add_executable(love_mrb_harness ${HARNESS_SRCS} ${EMBEDDED_HEADERS})
target_compile_definitions(love_mrb_harness PRIVATE ${LOVE_DEFS} LOVE_MRB_EMBEDDED_SCRIPTS)
target_include_directories(love_mrb_harness PRIVATE ${LOVE_INCS} ${EMBED_DIR})
target_compile_options(love_mrb_harness PRIVATE -Wall)

target_link_libraries(love_mrb_harness PRIVATE
	# Archive order matches the Makefile: a static lib must follow the objects
	# that reference it (gfx -> glslang/xxhash; everything -> box2d/physfs/...).
	mrbh_physfs mrbh_lz4 mrbh_wuff mrbh_gfx mrbh_glslang mrbh_xxhash mrbh_box2d
	${MRUBY_LIB}
	SDL3 GL pthread m z
	${FREETYPE_LIBRARIES} ${HARFBUZZ_LIBRARIES}
	vorbisfile vorbis ogg modplug ${OPENAL_LIBRARIES} theoradec)
target_link_directories(love_mrb_harness PRIVATE ${SDL_LIBDIR})
set_target_properties(love_mrb_harness PROPERTIES BUILD_RPATH ${SDL_LIBDIR})

# Second exe: the production mruby `love` entry (src/love_mrb.cpp), validated
# here before folding into the main CMakeLists.txt (parallel-path migration).
# Same module sources, with harness.cpp swapped for love_mrb.cpp; the boot
# scripts are baked in (embedded headers), so it runs a game given on argv.
set(LOVE_EXE_SRCS ${HARNESS_SRCS})
list(REMOVE_ITEM LOVE_EXE_SRCS ${HERE}/harness.cpp)
add_executable(love ${LOVE_SRC}/love_mrb.cpp ${LOVE_EXE_SRCS} ${EMBEDDED_HEADERS})
target_compile_definitions(love PRIVATE ${LOVE_DEFS} LOVE_BUILD_EXE)
target_include_directories(love PRIVATE ${LOVE_INCS} ${EMBED_DIR})
target_compile_options(love PRIVATE -Wall)
target_link_libraries(love PRIVATE
	mrbh_physfs mrbh_lz4 mrbh_wuff mrbh_gfx mrbh_glslang mrbh_xxhash mrbh_box2d
	${MRUBY_LIB}
	SDL3 GL pthread m z
	${FREETYPE_LIBRARIES} ${HARFBUZZ_LIBRARIES}
	vorbisfile vorbis ogg modplug ${OPENAL_LIBRARIES} theoradec)
target_link_directories(love PRIVATE ${SDL_LIBDIR})
set_target_properties(love PROPERTIES BUILD_RPATH ${SDL_LIBDIR})
