# The mruby build of LÖVE: a self-contained module that builds the `love`
# executable from the ported mruby bindings, bypassing the Lua machinery in the
# top-level CMakeLists.txt.
#
# Included from CMakeLists.txt when -DLOVE_MRUBY=ON (which then return()s), and
# from testing/mruby/CMakeLists.txt to build the dev harness against the same
# `love_mrb` static library -- so there is ONE source list to keep in sync with
# upstream (see testing/mruby/SYNC.md). Kept out of the main file so upstream
# Lua-side CMake churn doesn't conflict (the parallel-path migration; see
# testing/mruby/CMAKE_MIGRATION.md).
#
# Provides: the `love_mrb_objs` OBJECT library (engine modules + runtime) + the
# seven bundled `mrbh_*` archives, the `liblove.so` shared library (one-symbol
# ABI: love_mrb_main only) and the `love` executable, plus these variables for
# other includers:
#   LOVE_MRB_LIBS              harness link list (love_mrb_objs + engine deps)
#   LOVE_MRB_EMBED_DIR         dir holding the generated <name>_rb.h headers
#   LOVE_MRB_EMBEDDED_HEADERS  the generated headers (add to each exe's sources)
#   LOVE_MRB_SDL_LIBDIR        SDL3 link dir (for BUILD_RPATH)

include_guard(GLOBAL)

get_filename_component(LOVE_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(LOVE_SRC "${LOVE_ROOT}/src")

set(MRUBY_DIR  "${LOVE_ROOT}/../mruby" CACHE PATH "Path to the mruby tree")
set(SDL_LIBDIR "/usr/local/lib"        CACHE PATH "Directory containing libSDL3")
set(LOVE_MRB_SDL_LIBDIR "${SDL_LIBDIR}")

set(MRUBY_INC     "${MRUBY_DIR}/include")
set(MRUBY_GEN     "${MRUBY_DIR}/build/host/include")
set(MRUBY_LIB_DIR "${MRUBY_DIR}/build/host/lib")
find_library(MRUBY_LIB mruby PATHS "${MRUBY_LIB_DIR}" NO_DEFAULT_PATH)
if(NOT MRUBY_LIB)
	message(FATAL_ERROR
		"libmruby.a not found in ${MRUBY_LIB_DIR}.\n"
		"Build it:  make -C ${LOVE_ROOT}/testing/mruby mruby   (needs a host Ruby)")
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(FREETYPE REQUIRED freetype2)
pkg_check_modules(HARFBUZZ REQUIRED harfbuzz)
pkg_check_modules(OPENAL   REQUIRED openal)

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Defines/includes for the love TUs (engine .cpp + _mrb bindings + gfx archive).
#   LOVE_MRUBY            - guards Lua-only sections out
#   LOVE_MRUBY_NO_VULKAN  - OpenGL-only renderer
# (No LOVE_SRC_DIR: the boot scripts are embedded, not read from disk.)
set(LOVE_MRB_DEFS LOVE_MRUBY LOVE_MRUBY_NO_VULKAN)
set(LOVE_MRB_INCS
	${LOVE_SRC} ${LOVE_SRC}/modules ${LOVE_SRC}/libraries
	${MRUBY_INC} ${MRUBY_GEN}
	${FREETYPE_INCLUDE_DIRS} ${HARFBUZZ_INCLUDE_DIRS} ${OPENAL_INCLUDE_DIRS})

# --- bundled static archives (C/C++); own include needs only, no LOVE_DEFS ----
file(GLOB PHYSFS_ARCHIVERS ${LOVE_SRC}/libraries/physfs/physfs_archiver_*.c)
add_library(mrbh_physfs STATIC
	${LOVE_SRC}/libraries/physfs/physfs.c
	${LOVE_SRC}/libraries/physfs/physfs_byteorder.c
	${LOVE_SRC}/libraries/physfs/physfs_unicode.c
	${LOVE_SRC}/libraries/physfs/physfs_platform_posix.c
	${LOVE_SRC}/libraries/physfs/physfs_platform_unix.c
	${PHYSFS_ARCHIVERS})
target_include_directories(mrbh_physfs PRIVATE ${LOVE_SRC}/libraries/physfs)

add_library(mrbh_lz4 STATIC
	${LOVE_SRC}/libraries/lz4/lz4.c ${LOVE_SRC}/libraries/lz4/lz4hc.c)
target_include_directories(mrbh_lz4 PRIVATE ${LOVE_SRC}/libraries/lz4)

add_library(mrbh_wuff STATIC
	${LOVE_SRC}/libraries/Wuff/wuff.c ${LOVE_SRC}/libraries/Wuff/wuff_convert.c
	${LOVE_SRC}/libraries/Wuff/wuff_internal.c ${LOVE_SRC}/libraries/Wuff/wuff_memory.c)
target_include_directories(mrbh_wuff PRIVATE ${LOVE_SRC}/libraries/Wuff)

add_library(mrbh_xxhash STATIC ${LOVE_SRC}/libraries/xxHash/xxhash.c)
target_include_directories(mrbh_xxhash PRIVATE ${LOVE_SRC}/libraries/xxHash)

file(GLOB_RECURSE BOX2D_SRCS ${LOVE_SRC}/libraries/box2d/*.cpp)
add_library(mrbh_box2d STATIC ${BOX2D_SRCS})
target_include_directories(mrbh_box2d PRIVATE
	${LOVE_SRC} ${LOVE_SRC}/libraries ${LOVE_SRC}/libraries/box2d)

file(GLOB_RECURSE GLSLANG_SRCS ${LOVE_SRC}/libraries/glslang/*.cpp)
list(FILTER GLSLANG_SRCS EXCLUDE REGEX "/OSDependent/Web/")
list(FILTER GLSLANG_SRCS EXCLUDE REGEX "/OSDependent/Windows/")
add_library(mrbh_glslang STATIC ${GLSLANG_SRCS})
target_include_directories(mrbh_glslang PRIVATE ${LOVE_SRC} ${LOVE_SRC}/libraries/glslang)

file(GLOB GFX_CORE   ${LOVE_SRC}/modules/graphics/*.cpp)
file(GLOB GFX_OPENGL ${LOVE_SRC}/modules/graphics/opengl/*.cpp)
set(GFX_SRCS ${GFX_CORE} ${GFX_OPENGL}
	${LOVE_SRC}/libraries/glad/glad.cpp ${LOVE_SRC}/modules/video/VideoStream.cpp)
list(FILTER GFX_SRCS EXCLUDE REGEX "wrap_Graphics_mrb\\.cpp$")
list(FILTER GFX_SRCS EXCLUDE REGEX
	"wrap_(Buffer|Font|Graphics|GraphicsReadback|Mesh|ParticleSystem|Quad|Shader|SpriteBatch|TextBatch|Texture|Video)\\.cpp$")
add_library(mrbh_gfx STATIC ${GFX_SRCS})
target_compile_definitions(mrbh_gfx PRIVATE ${LOVE_MRB_DEFS})
target_include_directories(mrbh_gfx PRIVATE ${LOVE_MRB_INCS})

# --- embed the ported boot scripts (robust hex generator) ---------------------
set(LOVE_MODULE_DIR "${LOVE_SRC}/modules/love")
set(LOVE_MRB_EMBED_DIR "${CMAKE_CURRENT_BINARY_DIR}/love_mrb_embedded")
file(MAKE_DIRECTORY ${LOVE_MRB_EMBED_DIR})
set(LOVE_MRB_EMBEDDED_HEADERS "")
foreach(script arg callbacks boot nogame)
	set(out "${LOVE_MRB_EMBED_DIR}/${script}_rb.h")
	add_custom_command(
		OUTPUT "${out}"
		COMMAND ${CMAKE_COMMAND}
			-DIN=${LOVE_MODULE_DIR}/${script}.rb -DOUT=${out} -DVAR=${script}_rb
			-P ${LOVE_ROOT}/testing/mruby/embed_script.cmake
		DEPENDS "${LOVE_MODULE_DIR}/${script}.rb"
			"${LOVE_ROOT}/testing/mruby/embed_script.cmake"
		COMMENT "Embedding ${script}.rb")
	list(APPEND LOVE_MRB_EMBEDDED_HEADERS "${out}")
endforeach()

# --- love_mrb: the ported modules + common runtime (the "liblove" of mruby) ----
# common/delay.cpp gives the SDL-backed love::sleep (the harness's delay_stub is
# dropped now that SDL is always linked).
set(LOVE_MRB_MODULE_SRCS
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
	${LOVE_SRC}/common/delay.cpp

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

# --- engine deps shared by liblove and the harness ----------------------------
# The bundled archives + mruby + system libs. Both the shipped liblove and the
# dev harness embed the engine objects, so both need this exact link set.
set(LOVE_MRB_ENGINE_DEPS
	mrbh_physfs mrbh_lz4 mrbh_wuff mrbh_gfx mrbh_glslang mrbh_xxhash mrbh_box2d
	${MRUBY_LIB}
	SDL3 GL pthread m z
	${FREETYPE_LIBRARIES} ${HARFBUZZ_LIBRARIES}
	vorbisfile vorbis ogg modplug ${OPENAL_LIBRARIES} theoradec)

# --- love_mrb_objs: the ported modules + common runtime, compiled once --------
# An OBJECT library (every object is always pulled, like direct sources -- so the
# gfx<->modules cycle, gfx pulling common/memory.cpp's alignUp, still resolves
# with all module objects present). Built hidden so the objects carry no exported
# symbols of their own; liblove then bakes them behind a love_mrb_main-only ABI,
# while the dev harness links these objects DIRECTLY to reach the per-module
# openers (mrb_love_*_init) and the shared mruby state -- no exported entry
# points required. PUBLIC usage requirements flow to both consumers.
add_library(love_mrb_objs OBJECT ${LOVE_MRB_MODULE_SRCS})
target_compile_definitions(love_mrb_objs PUBLIC ${LOVE_MRB_DEFS})
target_include_directories(love_mrb_objs PUBLIC ${LOVE_MRB_INCS} ${LOVE_MRB_EMBED_DIR})
set_target_properties(love_mrb_objs PROPERTIES
	C_VISIBILITY_PRESET hidden
	CXX_VISIBILITY_PRESET hidden
	VISIBILITY_INLINES_HIDDEN ON)

# --- liblove: the shared engine library, one-symbol ABI -----------------------
# The engine objects + the boot driver (src/love_mrb.cpp, which marks
# love_mrb_main LOVE_EXPORT). Hidden presets keep our own code in; the version
# script (cmake/liblove.map) localizes everything else -- including the mruby and
# static-archive symbols that come in with default visibility -- so the only
# symbol exported from liblove.so is love_mrb_main.
include(GNUInstallDirs)

set(LOVE_MRB_VERSION_SCRIPT ${LOVE_ROOT}/cmake/liblove.map)
add_library(liblove SHARED
	$<TARGET_OBJECTS:love_mrb_objs>
	${LOVE_SRC}/love_mrb.cpp
	${LOVE_MRB_EMBEDDED_HEADERS})
set_target_properties(liblove PROPERTIES
	PREFIX "" OUTPUT_NAME liblove
	C_VISIBILITY_PRESET hidden
	CXX_VISIBILITY_PRESET hidden
	VISIBILITY_INLINES_HIDDEN ON)
target_compile_definitions(liblove PRIVATE ${LOVE_MRB_DEFS})
target_include_directories(liblove PRIVATE ${LOVE_MRB_INCS} ${LOVE_MRB_EMBED_DIR})
target_link_libraries(liblove PRIVATE ${LOVE_MRB_ENGINE_DEPS})
target_link_options(liblove PRIVATE
	"LINKER:--version-script=${LOVE_MRB_VERSION_SCRIPT}")
set_target_properties(liblove PROPERTIES LINK_DEPENDS ${LOVE_MRB_VERSION_SCRIPT})
target_link_directories(liblove PRIVATE ${SDL_LIBDIR})
set_target_properties(liblove PROPERTIES BUILD_RPATH ${SDL_LIBDIR})

# The dev harness links the engine objects directly (the openers are hidden in
# liblove.so), so it shares the module/runtime code and mruby explicitly.
set(LOVE_MRB_LIBS love_mrb_objs ${LOVE_MRB_ENGINE_DEPS})

# --- the thin love executable -------------------------------------------------
# Forwards to liblove's love_mrb_main (mirrors the Lua src/love.cpp -> liblove).
add_executable(love ${LOVE_SRC}/love_mrb_exe.cpp)
target_compile_options(love PRIVATE -Wall)
target_link_libraries(love PRIVATE liblove)
# CMake auto-adds liblove's build dir to the exe RPATH; add SDL for runtime, and
# $ORIGIN/../lib so the installed love finds the installed liblove.
set_target_properties(love PROPERTIES
	BUILD_RPATH ${SDL_LIBDIR}
	INSTALL_RPATH "${SDL_LIBDIR};$ORIGIN/../${CMAKE_INSTALL_LIBDIR}")

install(TARGETS love liblove
	RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
	LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})
