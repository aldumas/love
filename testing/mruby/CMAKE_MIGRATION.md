# CMake migration plan — replace Lua/LuaJIT with mruby

Scoping doc for the §C ledger item: *"CMake: build/link `libmruby.a` instead of
`lovedep::Lua`; drop `src/libraries/lua53` and the LuaJIT path."* This is the
last structural piece of the mruby port — every module, object type, and feature
is already ported (see `PORTING.md`). What remains is making the **real** build
(`CMakeLists.txt` → `love` / `liblove`) use mruby, so a shipped binary boots Ruby
games. Today only the standalone harness Makefile (`testing/mruby/Makefile`)
builds the ported code.

## What we already know works (the harness is the reference)

`testing/mruby/Makefile` links a working mruby LÖVE on Linux/OpenGL. It is the
authoritative recipe — the CMake build must reproduce its inputs:

- **mruby**: `libmruby.a` built by `MRUBY_CONFIG=love ruby ./minirake` in the
  mruby tree (config: `mruby/build_config/love.rb`, already `-fPIC` so it can go
  into the shared `liblove`). Include dirs: `<mruby>/include` and
  `<mruby>/build/host/include`.
- **compile defs** (every TU): `-DLOVE_MRUBY`, `-DLOVE_MRUBY_NO_VULKAN`
  (OpenGL-only for now), `-DLOVE_SRC_DIR=...` (dev-only; goes away once scripts
  are embedded — see Stage 3).
- **sources**: the `wrap_*_mrb.cpp` bindings + engine `.cpp` (the harness's
  `CORE_SRCS` + `MODULE_SRCS` is the exact ported set), `common/mrb_runtime.cpp`
  (not `common/runtime.cpp`), `thread/LuaThread_mrb.cpp` (not `LuaThread.cpp`).
- **sub-archives**: physfs, lz4, Wuff, xxHash (C); box2d, glslang, graphics core
  +opengl+glad (C++). All already in the engine tree; CMake already builds these
  for the Lua build too.
- **system libs**: SDL3, GL, freetype, harfbuzz, vorbis(file), ogg, modplug,
  openal, theoradec, z, pthread, m — all already required by the Lua build.

So the migration is **not** about new dependencies; it is about swapping the
scripting layer (Lua→mruby) and the bindings (`wrap_*.cpp`→`wrap_*_mrb.cpp`).

## The CMake delta, staged

### Stage 0 — mruby as a CMake dependency (de-risk first)
- Replace the `lovedep::Lua` INTERFACE target (CMakeLists.txt ~73–221) with an
  mruby target: include dirs + `libmruby.a`. Two options:
  - **prebuilt** (fast, matches current dev flow): `find_library(MRUBY_LIB mruby)`
    + a cache var for the mruby tree; fail with a clear message pointing at
    `make -C testing/mruby mruby`.
  - **ExternalProject_Add** running `minirake` with `MRUBY_CONFIG=love`
    (self-contained, needs a host Ruby at configure/build time).
  Recommend prebuilt first, ExternalProject as a follow-up.
- Add `LOVE_MRUBY` (+ `LOVE_MRUBY_NO_VULKAN`) as global `add_compile_definitions`.
- Remove the `LOVE_JIT` option and the `find_package(LuaJIT/Lua51)` /
  MEGA_LUAJIT / MEGA_LUA51 branches.
- **De-risking idea**: first author a `testing/mruby/CMakeLists.txt` that builds
  the *harness* exactly as the Makefile does. This validates the mruby CMake
  target + flags in isolation before touching the 2176-line main file, and gives
  a CMake-driven harness we can keep for tests.

### Stage 1 — swap the module bindings
For each of the ~21 module source groups in CMakeLists.txt:
- swap `wrap_<X>.cpp` → `wrap_<X>_mrb.cpp`; drop the embedded `wrap_<X>.lua`.
- graphics is special: the many object-type `wrap_*.cpp` (Buffer/Font/Mesh/
  Quad/Shader/SpriteBatch/TextBatch/Texture/Video/Graphics/GraphicsReadback/
  ParticleSystem) collapse into the single `wrap_Graphics_mrb.cpp`; the engine
  `.cpp` stay.
- swap `common/runtime.cpp` → `common/mrb_runtime.cpp`, and
  `thread/LuaThread.cpp` → `thread/LuaThread_mrb.cpp`.
- Use the harness `MODULE_SRCS` as the authoritative file list (it is current and
  builds).

### Stage 2 — drop the Lua libraries
- Remove `src/libraries/lua53` from the build and the LuaJIT megasource path.
- **Lua-only / hybrid modules that are NOT ported** and must be excluded or
  stubbed: `love.socket` (luasocket, `src/libraries/luasocket/*.lua.h`) and
  `love.enet` (`src/libraries/enet`, `love_3p_enet`). Decide explicitly: ship
  without them initially (document as a known gap), or write thin mruby stubs.
- Grep to confirm zero remaining `lovedep::Lua` references.

### Stage 3 — the liblove entry + boot (the biggest piece)
The Lua boot glue is heavily coupled (`src/love.cpp` ~57 Lua refs;
`src/modules/love/love.cpp` has the `luaopen_love_*` table + embedded
`boot.lua`/`arg.lua`/`callbacks.lua`/`nogame.lua`). The mruby path already exists
as a prototype in `testing/mruby/harness.cpp` (`open_love` + `run_boot`):
- Create the mruby liblove entry — `src/modules/love/love_mrb.cpp` or
  `#ifdef LOVE_MRUBY` forks — that opens mruby, registers every module via
  `mrb_love_<X>_init`, installs `love::thread::g_threadVMOpener`, and runs the
  Fiber boot loop (`$LOVE_MAIN.resume` until done).
- **Embed the ported scripts**: `boot.rb` / `arg.rb` / `callbacks.rb` (and a
  ported `nogame.rb`) must be baked into the binary the way `boot.lua` is, so a
  shipped `love` doesn't read them from `LOVE_SRC_DIR` (the harness's
  disk-read is a dev shortcut). Add a small `.rb`→C-string step (CMake
  `file(READ)`+`configure_file`, or reuse the existing `.lua`→`.lua.h` trick).
- `nogame.lua` → `nogame.rb` still needs porting (the no-game fallback screen).

### Stage 4 — the exe
`src/love.cpp` (the `love`/`lovec` entry) needs `#ifdef LOVE_MRUBY` edits where
it touches `lua_State` / `common/runtime.h` / preloads; otherwise it just links
liblove.

### Stage 5 — parity + cleanup
- Build `love`, run the 25 `testing/mruby/*.rb` scripts through the real binary
  (replacing the harness path), confirm identical behavior.
- Then run the **memory audit** (the other open §C item) under ASan/valgrind —
  now possible because CMake controls all flags. Note: the texture-creation and
  other helpers use a retain-then-release-after-build pattern with a known
  longjmp-on-error caveat; the audit should target those.
- Retire or keep the harness Makefile as a dev tool.

## Risks / open questions
1. **mruby build integration**: prebuilt vs ExternalProject; minirake needs a
   host Ruby + the `love` config; CI and cross-compile implications.
2. **Script embedding**: need a `.rb`→C-string tool/step; decide the mechanism.
3. **Unported network modules** (socket/enet): exclude vs stub — a product
   decision.
4. **Platforms**: the harness is Linux/OpenGL-only; the main CMake also targets
   Windows/macOS/Android. Stay Linux-first and guard the rest.
5. **Vulkan/Metal**: currently compiled out (`LOVE_MRUBY_NO_VULKAN`); re-enabling
   is a separate effort.
6. **`nogame.rb`**: the fallback screen script isn't ported yet.

## Recommended first concrete step
Stage 0's de-risking move: write `testing/mruby/CMakeLists.txt` that reproduces
the harness build under CMake (same sources/flags/libs, prebuilt `libmruby.a`).
It is low-risk, self-contained, proves the mruby CMake target + flag handling,
and yields a CMake-driven harness to run the existing `.rb` tests — the
foundation Stages 1–5 fold into the main `CMakeLists.txt`.
