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
· video (theora) · physics (box2d — first slice; see §A) · boot pipeline (arg/callbacks/boot)

---

## A. Per-module deferrals (a ported module is missing specific features)

### filesystem
Archive mounting and symlink toggling are ported. `mount` now takes either
`archive:` (a path String) or `data:` (a Data/FileData whose bytes are mounted
as an archive — a non-FileData Data also needs an archive `name:`); `unmount`
likewise accepts `archive:` or `data:`. The full-path / common-path family is
exposed: `mount_full_path` / `mount_common_path` (with `permissions:` "read"
(default) or "readwrite"), `unmount_full_path` / `unmount_common_path`, and
`get_full_common_path` (`common_path:` is a CommonPath name e.g. "appsavedir",
"userhome", "userappdata"). Symlinks: `set_symlinks_enabled` (`enable:`) /
`symlinks_enabled?`. Platform settings are exposed too: `set_fused` (`fused:`) /
`fused?` and `set_android_save_external` (`external:`, default false) /
`android_save_external?`. Covered by `filesystem_mount_test.rb`. Note: re-mounting
an already-mounted real path returns false by design (physfs), so e.g. the
auto-mounted save dir can't be mounted again via `mount_common_path`. The
following were split out of the former #fs-deferred bundle and are now all done:
- [x] (#fs-loader) loader / `require` — done, re-interpreted for mruby (which has
      no `require` of its own). Three pieces, all over the **virtual** filesystem
      (so they work inside `.love` archives and the save dir, like Lua's loader):
      • `get_require_path` / `set_require_path(paths:)` — the search patterns
        (each with a `?` placeholder). Default is `["?.rb", "?/init.rb"]` (the
        mruby module overrides the engine's Lua default at init). No
        `c_require_path`: mruby has no runtime native-module loading (gems are
        compiled in), so the Lua C-loader has no analog.
      • `load(name:)` — reads + compiles a script to a callable chunk **without**
        running it, returning a `Proc` (call it to run the body and get its last
        value). Raises `SyntaxError` on a parse error. The compiled top-proc has
        its class restored (codegen nulls it) so it is a real `.call`-able Proc.
      • a global `require(name)` (Kernel method, **positional** arg like Ruby's)
        — resolves via the require path, runs the file **once** at top level for
        its side effects (defining classes/constants), returns true the first
        time / false if already loaded, tracks `$LOADED_FEATURES`, and on a
        raising/failing file un-records it (so a retry is possible) and
        propagates. Semantics are Ruby's, not Lua's (Lua's `require` returned the
        module value; Ruby's runs for side effects + returns a bool). The run is
        wrapped in `mrb_protect_error` because `mrb_load` can both longjmp and
        set `mrb->exc` depending on nesting. Covered by `loader_test.rb`.
- [x] (#fs-platform) fused-mode + Android save-storage settings — done.
      `set_fused`/`fused?` wrap the physfs latch (`setFused` is one-shot, set by
      boot exactly once, so a later `set_fused` is ignored — the test asserts
      this). `set_android_save_external`/`android_save_external?` wrap the
      Android-only save-routing flag (a no-op off Android; the underscore-prefixed
      private `_setAndroidSaveExternal` in the Lua wrapper, here a plain setter).

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
- [x] (#data-pack) `pack` / `unpack` / `get_packed_size` — done; the Lua 5.3
      `lstrlib` (`string.pack`) format engine is reimplemented natively in
      `wrap_DataModule_mrb.cpp` (no Lua state), faithful to the original size /
      alignment / endianness / overflow logic. API under the kwarg convention:
      `pack(format:, values:, container:)` returns a String (default) or a
      ByteData (`container: "data"`), or — given `data:` (a ByteData) + optional
      `offset:` — packs in place and returns it; `unpack(format:, data:/string:,
      offset:)` returns a Hash `{values: [...], offset: <1-based next position>}`
      (the trailing position Lua's unpack returns); `get_packed_size(format:)`
      returns an Integer and raises on a variable-length (`s`/`z`) format. All
      format options carry over (`bBhHiIlLjJTfdn` ints/floats, `c`/`s`/`z`
      strings, `x`/`X` padding, `<>=!` modifiers). Covered by `data_test.rb`.
      Note: this harness's mruby caps integer literals at signed 32-bit, so the
      8-byte `j`/`J` cases and >2^31 values can't be exercised from Ruby here,
      but the engine handles them (sizes derive from the format, not mrb_int).
- [x] (#data-ffi-atomic) `Data#get_pointer` / `#get_ffi_pointer` /
      `#perform_atomic` — done. `get_pointer` returns the raw buffer pointer as a
      TT_CPTR value (as the window module's `get_pointer` does). `get_ffi_pointer`
      is a LuaJIT-FFI-only fast path with no mruby analog: the Lua base returned
      nil unless the FFI overrode it, so under mruby it faithfully always returns
      nil (see the §C FFI note). `perform_atomic { |data| ... }` runs the block
      with the Data's mutex held (atomic read-modify-write), yields the Data,
      returns the block's value, and — via `mrb_protect_error` — releases the
      mutex even when the block raises before re-raising (mruby's longjmp
      exceptions don't unwind the C++ stack, so RAII can't be relied on). Mirrors
      the channel module's `perform_atomic`. Covered by `data_test.rb`.

### video
Ported with the **real** theora decode backend (`video::theora::Video`, which
runs a decode worker thread; links system `libtheoradec` + the already-linked
`libogg`). The module instance is created at init and surfaced through
`Love::Graphics.new_video`; the `Love::VideoStream` object type exposes playback
control (`play`/`pause`/`seek`/`rewind`/`tell`/`playing?`/`get_filename`/
dimensions). The graphics-side `Love::Video` Drawable (YUV→RGB via the standard
video shader) is registered by the graphics module — see `#gfx-backend`.
Playback is timer-driven (a `TheoraVideoStream` owns a `DeltaSync` by default),
so video advances on its worker thread once played.
- [x] (#video-audio) audio track wired — done; `new_video` now best-effort builds
      a streaming audio `Source` from the same file (via the sound + audio
      modules), attaches it (`Video::setSource`), and syncs the video frames to it
      (`stream->setSync(new SourceSync(source))`), mirroring the Lua `newVideo`
      wrapper. `video.get_source` returns the `Love::Source`. Any failure (no audio
      track, missing modules) is non-fatal: the video keeps its default timer
      `DeltaSync` and plays silently. Note: once synced to a Source, the video
      advances with the audio clock, so frame progress needs `love.audio` to be
      pumped (the boot loop does this); a Source has no separate per-frame
      `tell`-advance in a bare script that never updates audio.

### physics (box2d) — FIRST SLICE
The first physics slice exposes the simulation core: `Love::Physics` (meter,
`new_world`, `new_body`, the circle/rectangle/polygon/edge/chain **shape**
creators and the body+shape combo creators), the `Love::World` type (update,
gravity, sleeping, locked?, body/joint/contact counts, get_bodies, lifecycle),
the `Love::Body` type (transforms, velocity, forces/impulses/torque, mass data,
type, local<->world transforms, flags, get_shape(s)), and the shape hierarchy
`Love::Shape` (friction/restitution/density/sensor, filter category/mask/group,
test_point) with the concrete `Love::CircleShape` / `PolygonShape` (validate) /
`EdgeShape` / `ChainShape`. Covered by `physics_test.rb` (drop a dynamic body
under gravity, step, read back the transform). Box2D builds as `libbox2d.a`.

Physics is the first engine code with VM calls embedded **in the engine class**
(World inherits the b2 listener interfaces; World/Body/Shape carry
`lua_State`-taking helper methods). Those Lua-only sections are guarded out with
`#ifndef LOVE_MRUBY` (the `LOVE_MRUBY` macro is defined by the harness Makefile)
and reconciled here by tag.

Reimplemented in `wrap_Physics_mrb.cpp` (mruby path is complete; the in-engine
Lua helper stays behind the `LOVE_MRUBY` guard as the Lua-build path, so the
marker is permanent and the item is `[~]` rather than `[x]`):
- [~] (#phys-world-helpers) `World::getGravity` / `getBodies` — reimplemented in
      the wrapper over `World::getBox2DWorld()` (a new accessor) + `getGroundBody`.
- [~] (#phys-body-helpers) `Body::getMassData` / `getWorldPoints` /
      `getLocalPoints` / `getShapes` — reimplemented over the public Body /
      `b2Body` API (`get_world_points`/`get_local_points` take a flat array).
- [~] (#phys-shape-filter) `Shape` category/mask bit helpers — reimplemented via
      `get/setFilterData(int*)`; `set_category`/`set_mask` take a `categories:`
      array of 1..16, `get_category`/`get_mask` return one.
- [~] (#phys-shape-query) `Shape` `ray_cast` / `compute_aabb` / `compute_mass` /
      `get_bounding_box` / `get_mass_data` — reimplemented in the wrapper over the
      `Shape::getBox2DShape()` / `getFixture()` accessors (new, since the wrapper
      isn't a friend of Shape). `ray_cast(x1:,y1:,x2:,y2:,max_fraction:,
      child_index:)` casts against the live fixture; adding `x:,y:,r:` casts
      against the bare shape at that transform instead — returns a Hash
      `{normal_x:, normal_y:, fraction:}` or nil on a miss. `compute_aabb`
      (`x:,y:,r:,child_index:`) and `get_bounding_box(child_index:)` return
      `{top_left_x:, top_left_y:, bottom_right_x:, bottom_right_y:}`;
      `compute_mass(density:)` and `get_mass_data` return `{x:, y:, mass:,
      inertia:}` (faithful quirk: compute_mass double-scales `I`, get_mass_data
      leaves it unscaled). Covered by `physics_test.rb`.
- [~] (#phys-shape-points) `PolygonShape`/`EdgeShape` `get_points` — transformed
      vertex readback as a flat `[x0,y0,x1,y1,...]` array (polygon: all verts;
      edge: the two endpoints), over `getBox2DShape()`. Covered by `physics_test.rb`.
- [~] (#phys-distance) `Physics.get_distance(shape_a:, shape_b:)` — reimplemented
      over `Shape::getFixture()`; both shapes must be active in a World. Returns a
      Hash `{distance:, x1:, y1:, x2:, y2:}` (distance + the nearest point on each
      shape). Covered by `physics_test.rb`.

The Lua-build paths for the three `[~]` items above stay behind `#ifndef
LOVE_MRUBY` in Shape.cpp / PolygonShape.cpp / EdgeShape.cpp / Physics.cpp, so
their markers are permanent (like #phys-shape-filter) and they are `[~]`, not `[x]`.

Deferred to later physics slices (functionality not in the mruby build yet):
- [ ] (#phys-callbacks) World collision callbacks: `set_callbacks`/`get_callbacks`,
      contact filter, and the `ContactCallback`/`ContactFilter` machinery. Needs
      an mruby callback-reference mechanism + the Contact type. `ShouldCollide`
      still applies the standard category/mask/group filtering (no user filter).
- [ ] (#phys-contact) the `Contact` object type + `World`/`Body` `getContacts`.
- [ ] (#phys-query) `World` `queryShapesInArea` / `getShapesInArea` (AABB query).
- [ ] (#phys-raycast) `World` `rayCast` / `rayCastAny` / `rayCastClosest`.
- [ ] (#phys-joints) all joint types + the `Physics` joint factories + `getJoints`
      (the heaviest remaining chunk — 11 joint wrappers).
- [ ] (#phys-userdata) `Body`/`Shape` `setUserData`/`getUserData` (Lua Reference).

---

## B. Temporary backends (lean stand-ins to be swapped wholesale)

These are deliberate: the real backends pull in the whole graphics/input
subsystem (~8000 lines). The Ruby-facing APIs are stable; only the C++ behind
them changes when we swap.

- [x] (#event-backend) **event** — done; the module instance is now the real
      `love::event::sdl::Event` (`event/sdl/Event.cpp` linked). The lean
      `HarnessEvent` is removed. The Ruby-facing API (pump/poll/push/clear/quit/
      restart) is unchanged — it calls through the abstract `love::event::Event`
      interface (queue + Variant round-trip), which the SDL backend's full
      translation (keyboard/text/mouse/touch/joystick/gamepad/sensor/window/drop/
      system) feeds. The previously-omitted live-resize *modal-draw* hook now
      works: it dynamic_casts to the real `window::sdl::Window` and re-renders
      through the real `graphics::Graphics`, both linked. All the modules the
      backend resolves at translation time (keyboard/window/graphics/input/audio/
      timer/filesystem) are ported.
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
      exposed so far: **Texture** (`new_image`; query/dimensions +
      `set_filter`/`get_filter`), **Quad** (`new_quad` + `get_viewport`/
      `set_viewport`), and **Font** (`new_font` + metrics: get_height/get_width/
      ascent/descent/baseline/line_height/has_glyphs/get_wrap), plus `draw`
      (Drawable or Texture+Quad), `set_font`/`get_font`, and `print`/`printf`
      (the full x/y/r/sx/sy/ox/oy/kx/ky transform; printf adds wrap limit +
      align). The full coordinate-system transform stack is exposed too:
      `push` (optional `type:` `"transform"`/`"all"` and optional `transform:` a
      `Love::Transform`) / `pop` / `translate` / `rotate` (`angle:`) / `scale`
      (`x:` defaults `y:` to it) / `shear` / `origin`, plus `apply_transform` /
      `replace_transform` (each takes `transform:` a `Love::Transform`) and
      `transform_point` / `inverse_transform_point` (`x:`+`y:` -> Hash {x:, y:}).
      Render state is exposed too: `set_blend_mode` (`mode:`, optional
      `alpha_mode:`) / `get_blend_mode` (-> Hash {mode:, alpha_mode:}),
      `set_scissor` (`x:`/`y:`/`width:`/`height:`, all optional -> none disables)
      / `intersect_scissor` / `get_scissor` (-> Hash or nil), `set_color_mask`
      (`r:`/`g:`/`b:`/`a:`, each optional, default true) / `get_color_mask`,
      `set_line_width` / `get_line_width`, `set_line_style` / `get_line_style`,
      `set_line_join` / `get_line_join`, `set_point_size` / `get_point_size`, and
      `set_wireframe` / `wireframe?`. Shaders are exposed: `new_shader`
      (`pixel:`/`vertex:` each a GLSL source / filename / FileData, plus optional
      `defines:` Hash and `debug_name:`), `set_shader` (no arg resets to default)
      / `get_shader`, and the `Love::Shader` type with `send` (`name:`+`value:`,
      handling float/int/uint/bool scalars+vectors+arrays, matrices via a
      `Love::Transform` or row-major number array, and samplers via a
      `Love::Texture`), `send_color`, `has_uniform?`, and `get_warnings`. Canvas /
      render targets are exposed: `new_canvas` (`width:`/`height:` default to the
      screen, plus optional `format:`/`msaa:`/`readable:`) returns a render-target
      `Love::Texture`; `set_canvas` (`canvas:` a Texture or Array of them for MRT,
      nil/omitted resets to the backbuffer; `stencil:`/`depth:` request a
      temporary depth/stencil buffer) / `get_canvas`. Stencil/depth render state
      is exposed: `set_stencil_mode` (`mode:` "off"/"draw"/"test"/"custom",
      omitted resets to off; `value:` defaults 1) / `get_stencil_mode` (-> Hash
      {mode:, value:}) and `set_depth_mode` (`compare:`, `write:`; both omitted
      reset) / `get_depth_mode` (-> Hash {compare:, write:}). The `Love::SpriteBatch`
      object type is exposed: `new_sprite_batch` (`texture:`, `size:` default
      1000, `usage:` "dynamic"/"static"/"stream") and the methods `add` /`set`
      (`quad:` optional + the standard transform; `add` returns the 1-based
      index), `clear`, `flush`, `set_texture`/`get_texture`, `set_color`/
      `get_color`, `get_count`, `get_buffer_size`, `set_draw_range`/
      `get_draw_range` (1-based, Hash or nil). The `Love::TextBatch` object type
      is exposed: `new_text_batch` (`font:`, optional `text:` String) and `set`
      (`text:`) / `setf` (`text:`/`wrap:`/`align:`) / `add` (`text:` + standard
      transform -> 1-based index) / `addf` (`text:`/`wrap:`/`align:` + transform)
      / `clear` / `set_font`/`get_font` / `get_width`/`get_height`/
      `get_dimensions` (optional 1-based `index:`). The `Love::ParticleSystem`
      object type is exposed comprehensively: `new_particle_system` (`texture:`,
      `size:` default 1000) plus the config API (emission rate/lifetime, particle
      lifetime, position/move_to, emission area, direction/spread, speed, linear/
      radial/tangential acceleration + linear damping, sizes/size_variation,
      rotation, spin/spin_variation, offset, colors, quads, insert_mode,
      relative_rotation) and the lifecycle (`start`/`stop`/`pause`/`reset`/
      `emit`(`count:`)/`update`(`dt:`)/`get_count`/`active?`/`paused?`/`stopped?`/
      `empty?`/`full?`). min/max (or x/y component) pairs are keyword args, and
      multi-value getters return Hashes; colors are Arrays of [r,g,b,a]. The
      `Love::Mesh` object type is exposed for the **standard vertex format** (a
      vertex is `[x, y, u, v, r, g, b, a]`): `new_mesh` (`vertices:` an Array of
      such, or `count:` for an empty mesh; `mode:` "fan"/"strip"/"triangles"/
      "points"; `usage:`) and `set_vertex`/`get_vertex` (1-based) / `set_vertices`
      / `get_vertex_count` / `set_texture`/`get_texture` / `set_draw_mode`/
      `get_draw_mode` / `set_draw_range`/`get_draw_range` / `set_vertex_map`/
      `get_vertex_map` (1-based) / `flush`. Custom vertex formats, per-attribute
      access, attached attributes, and explicit index buffers aren't ported. The
      `Love::Video` object type is exposed (`new_video` (`file:` an .ogv theora
      filename, `dpi_scale:`) + `play`/`pause`/`seek`/`rewind`/`tell`/`playing?`
      / `get_stream` / `get_source` / dimensions / `set_filter`/`get_filter`),
      backed by the now-linked **love.video** theora module (see its own line
      below). **With this every graphics object type is exposed.**
      ParticleSystem#clone isn't ported (needs the object identity map). Text is
      a plain String (the colored-string-segments form isn't ported);
      SpriteBatch's add_layer/set_layer (array textures) and
      attach_attribute (custom vertex buffers) aren't ported; nor are the
      slice/mipmap/explicit-depthstencil-texture set_canvas variants or the
      low-level set_stencil_state / set_depth_state.

      Name collision (font vs graphics): the love.font module and the graphics `Font`
      *type* both map to `Love::Font`. Resolved as for data/thread/joystick --
      one Ruby class doubles as both: the love.font module functions are
      registered as **class methods** on the graphics `Font` type's class (in
      `wrap_Font_mrb.cpp`), while graphics `Font` instances (`g.new_font`) are
      objects of that same class with the instance methods registered by the
      graphics wrapper. So `Love::Font.new_true_type_rasterizer(...)` and a
      font's `get_height` both work.
- [x] (#kbd-backend) **keyboard** — done; the module instance is now the real
      `keyboard::sdl::Keyboard` (`keyboard/Keyboard.cpp` + `keyboard/sdl/Keyboard.cpp`
      linked). The wrapper translates key/scancode/modifier names to and from the
      engine enum tables via `Keyboard::getConstant`, so down?/scancode_down? and
      the conversion helpers use LÖVE's canonical names (e.g. scancode `"a"`, not
      SDL's `"A"`); `modifier_active?` is now the real sticky-modifier set
      (numlock/capslock/scrolllock/mode). Key repeat is real state on the module,
      consulted directly by the event backend.
- [x] (#mouse-backend) **mouse** — done; the module instance is now the real
      `love::mouse::sdl::Mouse` (`mouse/sdl/Mouse.cpp` linked; the base
      `mouse/Mouse.h` is header-only). The Ruby bindings were unchanged — every
      binding calls through the abstract `love::mouse::Mouse` interface, which
      the SDL backend implements. The one wrapper adjustment: `down?` now builds
      a `std::vector<int>` for the real `isDown(buttons)` signature. Cursor
      objects (`new_cursor`/`get_system_cursor`/`set_cursor`/`get_cursor`) and
      grab/relative-mode/visibility all run on the real backend.

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
