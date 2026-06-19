# mruby port — deferral & temporary-work ledger

Source of truth for everything that was **skipped, stubbed, or temporarily
implemented** while porting LÖVE from Lua to mruby. The README's "Remaining
work" section is the narrative; this file is the actionable checklist.

## How this stays honest

Each item that has a code site carries a stable tag like `(#win-icon)`. The
same tag appears in a `// TODO(mruby) #win-icon: …` comment at the deferral
site. A pre-commit hook (`check_porting_ledger.sh`) enforces the link:

- every `#tag` in a code marker must exist in this ledger;
- every open (`[ ]`/`[~]`) ledger tag must have at least one code marker;
- every done (`[x]`) ledger tag must have **no** code marker left.

So when you finish an item: flip its box to `[x]` here **and** delete its
inline marker(s) — the hook fails until both are done. Reconcile manually with
`grep -rn "TODO(mruby)" src/`. Items with no code yet (§C, §D) are untagged and
ignored by the hook. A memory entry (`mruby-port-ledger`) points sessions here.

Legend: `[ ]` not started · `[~]` partial / stubbed · `[x]` done

---

## Modules ported so far

timer · math · filesystem · event · window · graphics (slice) · keyboard · mouse
· boot pipeline (arg/callbacks/boot)

---

## A. Per-module deferrals (a ported module is missing specific features)

### filesystem
- [ ] (#fs-deferred) deferred filesystem features:
  - Lua-loader functions (`load`, `require` search paths)
  - CommonPath mounting
  - symlink support
  - fused / Android settings
  - Data-based mounting (mount a FileData/ByteData)

### window
- [ ] (#win-omitted) `update_mode` and `get_pointer` — not yet exposed
- [ ] (#win-icon) `set_icon` / `get_icon` — needs the **image** module
- [ ] (#win-filedialog) `show_file_dialog` — needs Ruby callback plumbing;
      currently stubbed to call back with an error
- [~] (#win-dpi) HiDPI coordinate transforms — DPI scale pinned to 1.0,
      transforms identity

### keyboard
- [~] (#kbd-keyrepeat) `set_key_repeat` — stored state only; the lean event
      backend always forwards key repeats regardless

### mouse
- [ ] (#mouse-cursor) cursor object family: `new_cursor` (needs **image** /
      `ImageData`), `get_system_cursor`, `set_cursor`, `get_cursor` — all need
      a `Cursor` Type

---

## B. Temporary backends (lean stand-ins to be swapped wholesale)

These are deliberate: the real backends pull in the whole graphics/input
subsystem (~8000 lines). The Ruby-facing APIs are stable; only the C++ behind
them changes when we swap.

- [~] (#event-backend) **event** — `HarnessEvent`: window-independent SDL event
      conversion only. Swap for `event/sdl/Event.cpp` once the input family
      (joystick/touch/sensor) lands.
- [~] (#win-backend) **window** — `HarnessWindow`: real SDL window, no
      renderer/graphics context. Swap for `window/sdl/Window.cpp`.
- [~] (#gfx-backend) **graphics** — `HarnessGraphics`: immediate-mode
      (fixed-function GL 2.1) scaffolding. No textures, shaders, transforms
      beyond `origin`, blend/stencil state, fonts, or batched drawing. Swap for
      the real shader-based batched renderer (`graphics/opengl|vulkan|metal`).
- [~] (#kbd-backend) **keyboard** — plain `love::Module` using SDL name lookups
      instead of the 621-line `Keyboard.h` enum tables. Swap for
      `keyboard/sdl/Keyboard.cpp` once the key-constant tables are ported.
- [~] (#mouse-backend) **mouse** — plain `love::Module` driving SDL state
      directly instead of the real `Mouse` base (which needs Cursor + image).
      Swap for `mouse/sdl/Mouse.cpp`.

---

## C. Cross-cutting (whole-port infrastructure, no code site yet)

- [ ] Object/proxy identity map: an mruby equivalent of Lua's weak-table map so
      the same C++ object always maps to the same Ruby object.
- [ ] CMake: build/link `libmruby.a` instead of `lovedep::Lua`; drop
      `src/libraries/lua53` and the LuaJIT path.
- [ ] FFI fast paths: re-implement the few wrappers that use LuaJIT FFI.
- [ ] Port the remaining ~66 `wrap_*.cpp` modules.

---

## D. Not-yet-started input family (no code site yet)

- [ ] joystick (gamepad mappings, haptics — heaviest)
- [ ] touch
- [ ] sensor
