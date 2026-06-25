# CMake build of the mruby-port dev harness.
#
# Builds the shared `love_mrb` library + the `love` executable (via
# cmake/LoveMruby.cmake -- the single source of truth for the mruby build, also
# used by the main CMakeLists.txt with -DLOVE_MRUBY=ON), plus the dev `harness`
# on top of the same objects. So there is ONE module list to keep in sync with
# upstream (see SYNC.md / CMAKE_MIGRATION.md).
#
#   cmake -S testing/mruby -B testing/mruby/build
#   cmake --build testing/mruby/build -j
#   ./testing/mruby/build/love_mrb_harness testing/mruby/sample.rb   # flat script
#   ./testing/mruby/build/love testing/mruby/game.rb                 # full boot
#
# Override paths:  -DMRUBY_DIR=/path/to/mruby  -DSDL_LIBDIR=/path/to/sdl/lib

cmake_minimum_required(VERSION 3.16)
project(love_mrb_harness CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
	set(CMAKE_BUILD_TYPE Release)
endif()

# Build love_mrb (+ the mrbh_* archives) and the `love` exe from the shared
# module. It computes its own paths from its location, finds a prebuilt
# libmruby.a, and exposes LOVE_MRB_LIBS / LOVE_MRB_EMBEDDED_HEADERS /
# LOVE_MRB_SDL_LIBDIR.
get_filename_component(LOVE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
include(${LOVE_ROOT}/cmake/LoveMruby.cmake)

# The dev harness: runs a flat .rb script directly, or `--boot` a game through
# the full pipeline. It links the same love_mrb objects + archives (inheriting
# the defs/includes, incl. the embedded-scripts dir) and boots from the embedded
# scripts (LOVE_MRB_EMBEDDED_SCRIPTS). delay_stub.cpp is gone -- love_mrb's
# common/delay.cpp provides the SDL-backed love::sleep.
add_executable(love_mrb_harness
	${CMAKE_CURRENT_SOURCE_DIR}/harness.cpp
	${LOVE_MRB_EMBEDDED_HEADERS})
target_compile_definitions(love_mrb_harness PRIVATE LOVE_MRB_EMBEDDED_SCRIPTS)
target_compile_options(love_mrb_harness PRIVATE -Wall)
target_link_libraries(love_mrb_harness PRIVATE ${LOVE_MRB_LIBS})
target_link_directories(love_mrb_harness PRIVATE ${LOVE_MRB_SDL_LIBDIR})
set_target_properties(love_mrb_harness PROPERTIES BUILD_RPATH ${LOVE_MRB_SDL_LIBDIR})
