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

timer · math · filesystem · event · window · graphics (real backend) · keyboard · mouse
· system · data · image · font · thread · sound · audio · touch · sensor · joystick
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

### sound
Fully ported with the **real** lullaby decode backend. `Love::Sound` exposes
`new_decoder` and `new_sound_data`, plus the `Love::Decoder` and
`Love::SoundData` object types (`SoundData` is-a `Data`, inheriting the Data
instance methods via the class hierarchy — the data module must init first). All
five upstream decoders are linked: Wave (bundled Wuff), FLAC + MP3 (bundled
dr_flac / dr_mp3 compiled into their decoder cpps), Vorbis (system libvorbis),
and ModPlug (system libmodplug). `new_decoder(file:)` resolves a filename String
(opened via the filesystem module), a `Data` object, or a `Stream` into a
`love::Stream`; the default `stream_source: "file"` streams from disk, `"memory"`
reads the whole file into a `data::DataStream`. `new_sound_data` has three forms:
`samples:`/`sample_rate:`/`bit_depth:`/`channels:` (empty buffer), `decoder:`
(fully decode a Decoder), or `file:` (decode a file). `get_sample`/`set_sample`
take an optional 1-based `channel:`; `slice`/`copy_from`/`clone` are exposed.
Note the FFI fast path in `wrap_SoundData.lua` (a per-object pointer cache) is a
LuaJIT-only optimization and is intentionally dropped — the mruby binding calls
the C++ `getSample`/`setSample` directly. No deferrals.

### audio
Fully ported with the **real** OpenAL backend (links system libopenal; the null
backend is kept as the fallback exactly as the Lua loader did — OpenAL is tried
first, null only if a device can't be opened). `Love::Audio` exposes the module
functions plus the `Love::Source` and `Love::RecordingDevice` object types (both
is-a Object). Listener/source vectors that returned several Lua numbers return a
Hash here (`{x:, y:, z:}`, `{fx:, fy:, fz:, ux:, uy:, uz:}`, cone/limits/
distances likewise). Effect and Filter descriptions, which were Lua tables,
become Ruby Hashes keyed by the same parameter-name strings (symbol **or**
string keys accepted) with a mandatory `type:` entry; the EFX path is exercised
end-to-end (set/get a scene effect, source filters, source effects). `new_source`
mirrors the Lua convention: `file:` (a filename String or Data, decoded via the
sound + filesystem modules), `decoder:`, or `sound_data:`, with `type:`
`"static"`/`"stream"` (`"queue"` is rejected — use `new_queueable_source`). The
sound module must be initialised first (new_source-from-file builds a Decoder via
the Sound instance). `set_playback_device` returns false rather than raising when
a device can't be set, matching the Lua wrapper.

The OpenAL backend needs an audio device: in this dev environment `alcOpenDevice`
succeeds (EFX supported, recording devices enumerated, sources actually play); on
a host with no device it transparently falls back to null audio.

- Not ported (intentionally): `Source#queue`'s raw-pointer (lightuserdata) form —
  an FFI-style path with no mruby analog. The `SoundData` form is supported.
  RecordingDevice is fully wrapped, but mic capture itself isn't exercised by the
  test (depends on host hardware/permission). No code-site deferrals.

### touch / sensor / joystick (the input family)
All three ported with their **real** SDL backends.

- **touch** (`Love::Touch`): `get_touches` (optional `device_type:` filter),
  `get_position`/`get_pressure`/`get_device_type`/`mouse?` (each takes `id:`).
  Touch ids were Lua lightuserdata (to dodge the 2^53 double-precision limit);
  here they are plain Integers, since mruby's integers are 64-bit (word boxing)
  and hold an SDL touch id exactly.
- **sensor** (`Love::Sensor`): `has_sensor?`/`enabled?`/`set_enabled`/`get_data`/
  `get_name`, all keyed by `type:`. `get_data` returns an Array of floats.
- **joystick**: the module name collides with the `Joystick` type, so (like
  data/thread) the module functions are **class methods** on `Love::Joystick`
  while the per-controller API is **instance methods** on the same class.
  Module side: `get_joysticks`, `get_joystick_count`, `set_background_events`/
  `background_events?`, `set_gamepad_mapping` (kwargs `guid:`/`gamepad_input:`/
  `input_type:`/`input_index:`/`hat_direction:`), `load_gamepad_mappings(data:)`
  (a filename if it names an existing file, else a literal mappings string),
  `save_gamepad_mappings(file:)` (returns the String; writes it too if `file:`
  given), `get_gamepad_mapping_string(guid:)`. Instance side: the full controller
  API — ids/axes/buttons/hats (1-based, as in Lua), gamepad axis/button/mapping
  queries, vibration, and (under `LOVE_ENABLE_SENSOR`, which is defined) the
  per-controller sensor + power/connection-state methods. Multi-value Lua returns
  become Hashes (`get_id` -> `{id:, instance_id:}`, `get_device_info`,
  `get_vibration`, `get_device_power_info`, `get_gamepad_mapping`). No deferrals.

The harness has no physical input devices, so device lists come back empty; the
gamepad-mapping database (global to SDL) is exercised end-to-end without one.

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

- [~] (#event-backend) **event** — `HarnessEvent` now reproduces the **full**
      `event/sdl/Event.cpp` translation (keyboard/text/mouse/touch/joystick/
      gamepad/sensor/window/drop/system), using the real input-family modules and
      the keyboard enum tables (now linked) for canonical key names. Two things
      keep this from being the literal upstream file, and both are pre-existing
      lean backends, not new work: the sdl::Window live-resize *modal-draw* hook
      is omitted (it dynamic_casts to `window::sdl::Window`). Key names and the
      key-repeat check now go through the real keyboard module (`#kbd-backend` is
      done). With `#win-backend` now done too, a true wholesale swap to
      `event/sdl/Event.cpp` is unblocked (the live-resize hook can now cast to the
      real `window::sdl::Window`).
- [x] (#win-backend) **window** — done; the module instance is now the real
      `window::sdl::Window` (`window/sdl/Window.cpp` linked). The Ruby bindings
      were unchanged — every binding calls through the abstract
      `love::window::Window` interface, which the SDL backend implements. The
      coupling to `#gfx-backend` is satisfied: `setWindow()` resolves the real
      `graphics::Graphics` from `M_GRAPHICS`, creates the GL context, and calls
      `setMode()`/`backbufferChanged()` on it. The free functions
      `isDebugEnabled`/`isGammaCorrect`/`setGammaCorrect` resolve from the linked
      `graphics/Graphics.cpp`. Native file-dialog hosting (`#win-filedialog`) now
      runs on the real backend. The duplicate lean `setHighDPIAllowedImplementation`
      was dropped (the SDL backend provides it).
- [x] (#gfx-backend) **graphics** — done; the module instance is now a real
      `graphics::Graphics` from `Graphics::createInstance()` (the OpenGL backend,
      `graphics/opengl` — Vulkan/Metal are out of the build via
      `LOVE_MRUBY_NO_VULKAN`). The real shader-based batched renderer is linked:
      `graphics/*.cpp` + `graphics/opengl/*.cpp` + glad + glslang (shader
      validation/reflection) + xxHash, built as `libgfx.a`/`libglslang.a`/
      `libxxhash.a`. The Ruby API (`active?`/`clear`/`set_color`/`rectangle`/
      `origin`/`present`/dimensions) now drives the real path — a rectangle goes
      through the default shader and the streaming vertex buffer. Object types
      exposed so far: **Texture** (`new_image` — modern LÖVE merged Image into
      Texture; query/dimensions + `set_filter`/`get_filter`) and **Quad**
      (`new_quad` + `get_viewport`/`set_viewport`), plus `draw` (Drawable or
      Texture+Quad, with the full x/y/r/sx/sy/ox/oy/kx/ky transform). Still to
      expose on this same instance: shaders, transforms beyond `origin` (push/
      pop/translate/rotate/scale), blend/stencil/scissor state, fonts + print,
      and the remaining object types (SpriteBatch, Mesh, ParticleSystem, Canvas/
      render targets, TextBatch, Video).
- [x] (#kbd-backend) **keyboard** — done; the module instance is now the real
      `keyboard::sdl::Keyboard` (`keyboard/Keyboard.cpp` + `keyboard/sdl/Keyboard.cpp`
      linked). The wrapper translates key/scancode/modifier names to and from the
      engine enum tables via `Keyboard::getConstant`, so down?/scancode_down? and
      the conversion helpers use LÖVE's canonical names (e.g. scancode `"a"`, not
      SDL's `"A"`); `modifier_active?` is now the real sticky-modifier set
      (numlock/capslock/scrolllock/mode). Key repeat is real state on the module,
      consulted directly by the event backend.
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

## D. Input family

- [x] joystick (gamepad mappings, haptics — heaviest) — see §A.
- [x] touch — see §A.
- [x] sensor — see §A.

This unblocks the `#event-backend` swap in §B (the real SDL event backend needs
the joystick/touch/sensor modules to translate their events).
