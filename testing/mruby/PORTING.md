# mruby port — deferral & temporary-work ledger

Source of truth for everything that was **skipped, stubbed, or temporarily
implemented** while porting LÖVE from Lua to mruby. The README's "Remaining
work" section is the narrative; this file is the actionable checklist.

How this stays honest:
- Every entry below should have a matching `// TODO(mruby): …` comment at the
  deferral site in the code. Reconcile with:
  `grep -rn "TODO(mruby)" src/`
- When you finish an item, check it off here **and** delete its inline marker.
- A memory entry (`mruby-port-ledger`) points sessions at this file.

Legend: `[ ]` not started · `[~]` partial / stubbed · `[x]` done

---

## Modules ported so far

timer · math · filesystem · event · window · graphics (slice) · keyboard · mouse
· boot pipeline (arg/callbacks/boot)

---

## A. Per-module deferrals (a ported module is missing specific features)

### filesystem
- [ ] Lua-loader functions (`load`, `require` search paths)
- [ ] CommonPath mounting
- [ ] symlink support
- [ ] fused / Android settings
- [ ] Data-based mounting (mount a FileData/ByteData)

### window
- [ ] `update_mode` (partial mode changes without full `set_mode`)
- [ ] `set_icon` / `get_icon` (needs the **image** module)
- [ ] `show_file_dialog` (needs Ruby callback plumbing; currently stubbed to
      call back with an error)
- [ ] `get_pointer` (native window handle accessor)
- [~] HiDPI coordinate transforms — DPI scale pinned to 1.0, transforms identity

### keyboard
- [~] `set_key_repeat` — stored state only; the lean event backend always
      forwards key repeats regardless

### mouse
- [ ] cursor object family: `new_cursor` (needs **image** / `ImageData`),
      `get_system_cursor`, `set_cursor`, `get_cursor` — all need a `Cursor` Type

---

## B. Temporary backends (lean stand-ins to be swapped wholesale)

These are deliberate: the real backends pull in the whole graphics/input
subsystem (~8000 lines). The Ruby-facing APIs are stable; only the C++ behind
them changes when we swap.

- [~] **event** — `HarnessEvent`: window-independent SDL event conversion only.
      Swap for `event/sdl/Event.cpp` once the input family (joystick/touch/
      sensor) lands.
- [~] **window** — `HarnessWindow`: real SDL window, no renderer/graphics
      context. Swap for `window/sdl/Window.cpp`.
- [~] **graphics** — `HarnessGraphics`: immediate-mode (fixed-function GL 2.1)
      scaffolding. No textures, shaders, transforms beyond `origin`, blend/
      stencil state, fonts, or batched drawing. Swap for the real shader-based
      batched renderer (`graphics/opengl|vulkan|metal`).
- [~] **keyboard** — plain `love::Module` using SDL name lookups instead of the
      621-line `Keyboard.h` enum tables. Swap for `keyboard/sdl/Keyboard.cpp`
      once the key-constant tables are ported.
- [~] **mouse** — plain `love::Module` driving SDL state directly instead of the
      real `Mouse` base (which needs Cursor + image). Swap for
      `mouse/sdl/Mouse.cpp`.

---

## C. Cross-cutting (whole-port infrastructure)

- [ ] Object/proxy identity map: an mruby equivalent of Lua's weak-table map so
      the same C++ object always maps to the same Ruby object.
- [ ] CMake: build/link `libmruby.a` instead of `lovedep::Lua`; drop
      `src/libraries/lua53` and the LuaJIT path.
- [ ] FFI fast paths: re-implement the few wrappers that use LuaJIT FFI.
- [ ] Port the remaining ~66 `wrap_*.cpp` modules.

---

## D. Not-yet-started input family
- [ ] joystick (gamepad mappings, haptics — heaviest)
- [ ] touch
- [ ] sensor
