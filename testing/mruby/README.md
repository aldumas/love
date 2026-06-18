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
| Ported module wrapper (keyword-arg API) | `src/modules/timer/wrap_Timer_mrb.cpp` |
| Standalone demo harness | `testing/mruby/harness.cpp` |
| nanosleep/deprecation stubs (avoid linking SDL for the demo) | `testing/mruby/delay_stub.cpp` |

The full `love` executable can't link until all 74 module wrappers are ported,
so this harness exercises just the one ported module (`timer`) end-to-end.

## API shape

Decided convention: a top-level `Love` namespace, modules as Ruby modules,
**snake_case** method names, and **keyword arguments**.

```ruby
Love::Timer.get_time                  # was love.timer.getTime()
Love::Timer.sleep(seconds: 0.05)      # was love.timer.sleep(0.05)
Love::Graphics.rectangle(mode: "fill", x: 0, y: 0, width: 100, height: 50)
```

Missing required keywords raise `ArgumentError`; omitted optional keywords
arrive as mruby `undef` and are handled by the `mrbx_opt*` helpers.

## Building mruby

```sh
cd mruby
MRUBY_CONFIG=love ruby ./minirake
# -> mruby/build/host/lib/libmruby.a
# -> generated headers in mruby/build/host/include
```

## Building and running the harness

```sh
cd love
MRB=../mruby
g++ -std=c++14 -I src -I $MRB/include -I $MRB/build/host/include \
  testing/mruby/harness.cpp \
  testing/mruby/delay_stub.cpp \
  src/common/mrb_runtime.cpp \
  src/modules/timer/wrap_Timer_mrb.cpp \
  src/modules/timer/Timer.cpp \
  src/common/Module.cpp \
  src/common/Object.cpp \
  src/common/types.cpp \
  src/common/Exception.cpp \
  -L $MRB/build/host/lib -lmruby -lm \
  -o /tmp/love_mrb_harness
/tmp/love_mrb_harness
```

Expected output ends with `OK` and exit code 0.

## Remaining work (per-module template established by this slice)

1. Port the other 73 `wrap_*.cpp` files to the `wrap_*_mrb.cpp` pattern, one
   module at a time, converting positional params to keyword args.
2. Replace the embedded Lua boot scripts (`src/modules/love/boot.lua`,
   `callbacks.lua`, `arg.lua`) with Ruby equivalents; map the Lua coroutine
   boot loop onto mruby `Fiber`.
3. Swap the object/proxy system: the Lua weak-table identity map needs an mruby
   equivalent so the same C++ object always maps to the same Ruby object.
4. Wire CMake (`CMakeLists.txt`) to build `libmruby.a` and link it instead of
   `lovedep::Lua`; drop `src/libraries/lua53` and the LuaJIT path.
5. Port the FFI-dependent fast paths (love uses LuaJIT FFI in a few wrappers).
