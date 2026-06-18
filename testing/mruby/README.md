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
| Ported boot scripts (arg/callbacks/boot) | `src/modules/love/{arg,callbacks,boot}.rb` |
| Standalone demo harness | `testing/mruby/harness.cpp` |
| nanosleep/deprecation stubs (avoid linking SDL for the demo) | `testing/mruby/delay_stub.cpp` |
| Build (`make`, `make run`, `make mruby`) | `testing/mruby/Makefile` |

The full `love` executable can't link until all 74 module wrappers are ported,
so this harness exercises the ported modules (`timer`, `math`, `filesystem`)
end-to-end. The filesystem module links the bundled physfs library (compiled as
C) and SDL3 (`/usr/local/lib`), so the harness now depends on `libSDL3`.

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
  `def Love.quit`), detected with `Love.respond_to?`.
- `boot.rb` — `Love.boot` (filesystem init + identity) and `Love.init` (config,
  `Love.conf`, first timestep, load the game), then the **root coroutine**: an
  mruby `Fiber` (`$LOVE_MAIN`) that runs boot/init/run inside an error boundary
  and `Fiber.yield`s once per frame. The harness resumes it until it finishes —
  the same resume-until-done loop `love.cpp` runs against the Lua boot coroutine.

Adaptations for mruby / the current module set: `Fiber` replaces the Lua
coroutine; modules not yet ported (event, graphics, window) are detected with
`const_defined?` and their branches skipped (so `draw` is called directly and a
`Love.quit!` flag stands in for an event-module "quit"); the game's main file is
`eval`'d (mruby has no file-level `Kernel#load`).

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
   loop mapped onto mruby `Fiber`. Still to do as more modules land: wire the
   real event/graphics/window branches and the graphics-backed error screen.
3. Swap the object/proxy system: the Lua weak-table identity map needs an mruby
   equivalent so the same C++ object always maps to the same Ruby object.
4. Wire CMake (`CMakeLists.txt`) to build `libmruby.a` and link it instead of
   `lovedep::Lua`; drop `src/libraries/lua53` and the LuaJIT path.
5. Port the FFI-dependent fast paths (love uses LuaJIT FFI in a few wrappers).
