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
· system · data · image · font · thread · boot pipeline (arg/callbacks/boot)

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
- [x] (#win-omitted) `update_mode` and `get_pointer` — done; `update_mode`
      re-applies the mode with every keyword (incl. width/height) optional, and
      `get_pointer` returns the native window handle as a TT_CPTR value
- [x] (#win-icon) `set_icon` / `get_icon` — done; the lean window backend builds
      an SDL window icon from the ImageData's RGBA8 pixels and retains it
- [x] (#win-filedialog) `show_file_dialog` — done; takes a result block
      `{ |files, filter_name, err| ... }` and the dialog kwargs (`type:` required,
      `title:`/`accept_label:`/`cancel_label:`/`default_name:`/`filters:`/
      `multi_select:`/`attach_to_window:`). The Ruby callback plumbing is real;
      hosting a native dialog moves to the #win-backend swap (the lean backend
      resolves the block with an "unsupported" error, like a user cancel).
- [x] (#win-dpi) HiDPI coordinate transforms — done; faithful to
      window/sdl/Window.cpp: window<->pixel uses the tracked pixel/window size
      ratio and the DPI scale is SDL's display scale, honored only when the
      window opted into `use_dpi_scale`. `to_pixels`/`from_pixels` are now exposed
      too (single value -> Number, `x:`+`y:` -> Hash {x:, y:})

### keyboard
- [x] (#kbd-keyrepeat) `set_key_repeat` — done; the lean event backend now
      consults the keyboard module (via `keyboard::harnessKeyRepeatEnabled`) and
      drops auto-repeat keypressed events when key repeat is off, matching the
      real event backend

### mouse
- [x] (#mouse-cursor) cursor object family: `new_cursor`, `get_system_cursor`,
      `set_cursor`, `get_cursor` and the `Love::Cursor` type — done; the lean
      mouse backend manages cursors using the real `love::mouse::sdl::Cursor`

### image
ImageData ported with a real backend (decode via lodepng/stb/tinyexr/ddsparse,
all magpie handlers compiled). ImageData is-a Data and inherits the Data instance
methods through the runtime's class hierarchy.
- [x] (#img-compressed) `new_compressed_data` / `compressed?` and the
      `CompressedImageData` object type — done; `Love::CompressedImageData` is-a
      `Love::Data` (inherits the Data methods via the class hierarchy) and exposes
      clone / get_width / get_height / get_dimensions (optional 1-based `mipmap:`)
      / get_mipmap_count / get_format / set_linear / linear?. Both module funcs
      take `file:` (a Data object or a filename String read via the filesystem).

### font
Fully ported with the **real** `freetype::Font` backend (links freetype +
harfbuzz; default font is the embedded gzip NotoSans, decompressed via the data
module). Exposes the module functions (new_rasterizer / new_true_type_rasterizer
/ new_bm_font_rasterizer / new_image_rasterizer / new_glyph_data) and the
`Love::Rasterizer` and `Love::GlyphData` types (GlyphData is-a Data). No
deferrals. Note a faithful upstream quirk: `GlyphData#get_glyph` is 0 (and
`get_glyph_string` is "\\x00") for TrueType glyphs — `Rasterizer::getGlyphData`
routes through `getGlyphDataForIndex`, which constructs `GlyphData(0, …)` since
it only has the freetype glyph index, not the codepoint. (The graphics-side
`TextShaper` / harfbuzz shaping is compiled in but exposed via the unported
graphics `Font`, not love.font.)

### thread
Fully ported with the **real** SDL thread backend. A thread runs an mruby
script in its own `mrb_state` (mruby states aren't shareable across threads):
`LuaThread.cpp` is swapped for `LuaThread_mrb.cpp`, which spins up the state,
opens the Love:: modules via a host-installed opener (`g_threadVMOpener`, set by
the harness to the same routine it uses for the main VM), and runs the code.
start() arguments reach the script as the global `$LOVE_THREAD_ARGS` array (the
mruby analog of the Lua chunk's `...`). Like the data module, the "Thread"
module-name/type-name collision is resolved by hanging the module functions as
class methods on the `Love::Thread` type class. Channels round-trip values via
the runtime's Variant<->Ruby conversion; `perform_atomic` takes a block.
Required a runtime fix: `mrbx_gettypeclass`'s type->class cache is now keyed per
`mrb_state` (with `mrbx_forgetstate` cleanup on thread exit) -- the old
single-state cache handed a thread VM the main VM's stale classes. Also,
Variant->Ruby now reconstructs a contiguous 1-based integer table as an Array
(so arrays round-trip as arrays through channels/threads/events). No deferrals.

### data
The module's functions are class methods on `Love::Data` (the module name "Data"
collides with the `Data` type; a Ruby class doubling as the namespace resolves
it). Base `Data` instance methods are registered once and inherited by every
`Data` subtype via the runtime's love::Type-mirrored class hierarchy.
- [ ] (#data-pack) `pack` / `unpack` / `get_packed_size` — depend on Lua 5.3's
      `lstrlib` (`string.pack`); need a native binary-pack implementation.
- [ ] (#data-ffi-atomic) `Data#get_pointer` / `#get_ffi_pointer` (raw/FFI
      pointers) and `Data#perform_atomic` (mutex + block) — not exposed.

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
      directly instead of the real `Mouse` base (it manages cursors itself via
      the real `sdl::Cursor`). Swap for `mouse/sdl/Mouse.cpp`.

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
