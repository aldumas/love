# mruby port — proof-of-concept

This directory contains the foundation slice of the `mruby-port` branch, which
replaces LÖVE's Lua/LuaJIT scripting layer with [mruby](https://mruby.org/) and
changes the API calling convention from **positional parameters** to **keyword
arguments**.

## What's here

| Piece | File |
| --- | --- |
| mruby build config (produces `libmruby.a`) | `../../../mruby/build_config/love.rb` |
| Runtime layer (`mrbx_*` helpers, Object/Type binding, module registration) | `src/common/mrb_runtime.{h,cpp}` |
| Ported module: timer (module functions) | `src/modules/timer/wrap_Timer_mrb.cpp` |
| Ported module: math (functions + RandomGenerator, BezierCurve, Transform object types) | `src/modules/math/wrap_Math_mrb.cpp` |
| Ported module: filesystem (functions + File, FileData; real physfs backend) | `src/modules/filesystem/wrap_Filesystem_mrb.cpp` |
| Ported module: event (queue + lean window-independent SDL backend) | `src/modules/event/wrap_Event_mrb.cpp` |
| Ported module: window (lean graphics-independent SDL backend) | `src/modules/window/wrap_Window_mrb.cpp` |
| Ported boot scripts (arg/callbacks/boot) | `src/modules/love/{arg,callbacks,boot}.rb` |
| Standalone demo harness | `testing/mruby/harness.cpp` |
| nanosleep/deprecation stubs (avoid linking SDL for the demo) | `testing/mruby/delay_stub.cpp` |
| Build (`make`, `make run`, `make mruby`) | `testing/mruby/Makefile` |

The full `love` executable can't link until all 74 module wrappers are ported,
so this harness exercises the ported modules (`timer`, `math`, `filesystem`,
`event`, `window`) end-to-end. The filesystem module links the bundled physfs
library (compiled as C) and SDL3 (`/usr/local/lib`), so the harness depends on
`libSDL3` (also used by the event and window backends).

## API shape

Decided convention: a top-level `Love` namespace, modules as Ruby modules,
**snake_case** method names, and **keyword arguments**.

```ruby
Love::Timer.get_time                  # was love.timer.getTime()
Love::Timer.sleep(seconds: 0.05)      # was love.timer.sleep(0.05)

Love::Math.random(min: 1, max: 6)     # was love.math.random(1, 6)
rng = Love::Math.new_random_generator(seed: 42)   # an object with methods:
rng.random(min: 1, max: 6)            # was rng:random(1, 6)

Love::Graphics.rectangle(mode: "fill", x: 0, y: 0, width: 100, height: 50)
```

Settings tables that the Lua API packed into a trailing table become flattened
keyword arguments. For example `love.window.setMode(800, 600, {resizable=true})`
becomes `Love::Window.set_mode(width: 800, height: 600, resizable: true)`, and
multi-value returns (`love.window.getMode`) return a Hash with symbol keys.

Object types (e.g. `Love::RandomGenerator`) map to Ruby classes via
`mrbx_pushtype`/`mrbx_checktype`; their instance methods are registered with
`mrbx_register_type`. The C++ object is retained while a Ruby object references
it and released on garbage collection.

Missing required keywords raise `ArgumentError`; omitted optional keywords
arrive as mruby `undef` and are handled by the `mrbx_opt*` helpers.

## Building mruby

```sh
cd mruby
MRUBY_CONFIG=love ruby ./minirake
# -> mruby/build/host/lib/libmruby.a
# -> generated headers in mruby/build/host/include
```

Note: a *clean* mruby build (after `rm -rf build/host`, e.g. to pick up a
build_config change) shells out to `rake`. If `rake` resolves to an asdf shim
with no version set, point it at the bundled minirake first:

```sh
printf '#!/bin/sh\nexec ruby '"$PWD"'/minirake "$@"\n' > /tmp/rake && chmod +x /tmp/rake
PATH="/usr/bin:/tmp:$PATH" MRUBY_CONFIG=love ruby ./minirake
```

## Building and running the harness

```sh
cd testing/mruby
make            # build (auto-builds libmruby.a if missing)
make run        # build + run against sample.rb (flat playground)
./love_mrb_harness myscript.rb   # run your own flat script

# Boot pipeline: load arg.rb/callbacks.rb/boot.rb, then run a game that
# defines Love.load/update/draw, driving the boot Fiber per frame.
./love_mrb_harness --boot game.rb
```

Adding a newly ported module: append its sources to `MODULE_SRCS` in the
Makefile and call its `mrb_love_<name>_init` from `harness.cpp`.

## The boot pipeline

`--boot` reproduces how `love.cpp` starts a game, but on mruby:

- `arg.rb` — `Love::Path` / `Love::Arg` helpers and the game-argument accessors.
- `callbacks.rb` — the default `Love.run` main loop, the event-handler table,
  and `Love.error_handler`. Game callbacks are public singleton methods on
  `Love` (`def Love.load(args, raw)`, `def Love.update(dt)`, `def Love.draw`,
  `def Love.quit`), detected with `Love.respond_to?`. The loop pumps and polls
  `Love::Event` (now ported): each `[:name, *args]` event is dispatched through
  the handler table, and `:quit` is routed through `Love.quit` (which may veto).
- `boot.rb` — `Love.boot` (filesystem init + identity) and `Love.init` (config,
  `Love.conf`, first timestep, load the game), then the **root coroutine**: an
  mruby `Fiber` (`$LOVE_MAIN`) that runs boot/init/run inside an error boundary
  and `Fiber.yield`s once per frame. The harness resumes it until it finishes —
  the same resume-until-done loop `love.cpp` runs against the Lua boot coroutine.

Adaptations for mruby / the current module set: `Fiber` replaces the Lua
coroutine; modules not yet ported (graphics) — or unavailable at runtime, like
`window` on a headless machine — are detected with `const_defined?` and their
branches skipped (so `draw` is called directly); the game's main file is
`eval`'d (mruby has no file-level `Kernel#load`). The event module is wired up —
a `Love.quit!` stand-in remains only as the fallback when `love.event` is
absent. `Love.init` now creates the window from the `t.window` config (via
`Love::Window.set_mode`) when `window` is present, just like `boot.lua`.

## Remaining work (per-module template established by this slice)

1. Port the other 71 `wrap_*.cpp` files to the `wrap_*_mrb.cpp` pattern, one
   module at a time, converting positional params to keyword args. The timer
   slice covers module functions; math covers object types with instance
   methods (`mrbx_register_type` / `mrbx_pushtype`); filesystem covers an
   abstract module with a real backend (physfs), returning Hashes/arrays and
   wrapping File/FileData objects.
   Deferred within filesystem: the Lua-loader functions (load, require paths),
   CommonPath mounting, symlinks, fused/android settings, Data-based mounting.
2. DONE: the Lua boot scripts (`boot.lua`, `callbacks.lua`, `arg.lua`) are
   ported to Ruby (`boot.rb`, `callbacks.rb`, `arg.rb`) with the coroutine boot
   loop mapped onto mruby `Fiber`, and the run loop now pumps/polls the ported
   `love.event`, and `Love.init` opens the config window via `love.window`.
   Still to do as more modules land: wire the graphics branch and the
   graphics-backed error screen. Both the event backend (`HarnessEvent`) and the
   window backend (`HarnessWindow`) are intentionally lean. `HarnessEvent`
   converts only window-independent SDL events; `HarnessWindow` creates a real
   SDL window but no renderer context (the full `window/sdl/Window.cpp` builds an
   OpenGL/Metal/Vulkan context and drives the `love.graphics` backbuffer, pulling
   in the whole graphics subsystem). Swap both for the full SDL backends once
   graphics + the input modules (keyboard/mouse/joystick/touch) are ported -- the
   Ruby-facing APIs are unchanged. Deferred within window: `update_mode`,
   `set_icon`/`get_icon` (need the image module), `show_file_dialog` (needs Ruby
   callback plumbing), `get_pointer`, and the HiDPI coordinate transforms (the
   lean backend fixes DPI scale at 1.0).
3. Swap the object/proxy system: the Lua weak-table identity map needs an mruby
   equivalent so the same C++ object always maps to the same Ruby object.
4. Wire CMake (`CMakeLists.txt`) to build `libmruby.a` and link it instead of
   `lovedep::Lua`; drop `src/libraries/lua53` and the LuaJIT path.
5. Port the FFI-dependent fast paths (love uses LuaJIT FFI in a few wrappers).
