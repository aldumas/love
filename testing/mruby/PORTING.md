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
- [~] (#phys-query) `World.get_shapes_in_area(x1:, y1:, x2:, y2:, categories:)` —
      the **result-returning** AABB query, reimplemented over
      `getBox2DWorld()->QueryAABB` + a small in-wrapper `ShapeCollector`
      (b2QueryCallback). `categories:` is an optional array of 1..16 (default all).
      Returns an Array of the overlapping shapes. The **user-callback** variant
      `query_shapes_in_area` (a Ruby block per fixture) is now ported too, under
      #phys-callbacks. Covered by `physics_test.rb`.
- [~] (#phys-raycast) `World.ray_cast_any` / `ray_cast_closest`
      (`x1:, y1:, x2:, y2:, categories:`) — the two **result-returning** ray casts,
      reimplemented over `getBox2DWorld()->RayCast` + the engine's shared
      `RayCastOneCallback` (which is not LOVE_MRUBY-guarded). Each returns a Hash
      `{shape:, x:, y:, normal_x:, normal_y:, fraction:}` or nil on a miss. The
      full `ray_cast` (a Ruby block invoked per fixture hit, returning the next
      fraction) is now ported too, under #phys-callbacks. Covered by `physics_test.rb`.

The Lua-build paths for the `[~]` items above stay behind `#ifndef LOVE_MRUBY`
in Shape.cpp / PolygonShape.cpp / EdgeShape.cpp / Physics.cpp / World.cpp, so
their markers are permanent (like #phys-shape-filter) and they are `[~]`, not `[x]`.

Ported in later physics slices (Lua-only paths stay behind `#ifndef LOVE_MRUBY`):
- [~] (#phys-callbacks) World collision callbacks + contact filter + the
      user-callback query/raycast variants are ported. The mruby
      callback-reference mechanism is `mrbx_set_callback`/`mrbx_get_callback`/
      `mrbx_clear_callback` (common/mrb_runtime.cpp): a `const void* -> (mrb_state,
      GC-protected Proc)` store keyed by a stable address. `World::ContactCallback`/
      `ContactFilter` each key the store by their own `this` (the binding stores
      under the matching `World::getCallbackKey`/`getContactFilterKey`, new
      accessors); their `process()` is defined in `wrap_Physics_mrb.cpp` (the
      Lua-build `process()` stays guarded in World.cpp) and yields to the stored
      Proc with the two Shapes + Contact (+ impulses for postsolve). `set_callbacks`
      takes `begin:`/`end:`/`presolve:`/`postsolve:` Procs (omitted = cleared);
      `get_callbacks` returns a Hash; `set_contact_filter(filter:)` /
      `get_contact_filter` wrap the filter (nil clears, then `ShouldCollide`
      applies only the standard category/mask/group test). `query_shapes_in_area`
      (block per overlapping shape) and `ray_cast` (block per hit, returns the next
      fraction) run a local `b2QueryCallback`/`b2RayCastCallback` over the block.
      `World::destroy` clears the store entries; `mrbx_forgetstate` drops a closing
      VM's. The engine's Lua `QueryCallback`/`RayCastCallback` (#phys-query/
      #phys-raycast) stay guarded for the Lua build. Covered by `physics_test.rb`.
- [~] (#phys-contact) the `Contact` object type + `World`/`Body` `get_contacts` are
      ported: `valid?`/`destroyed?`, `get_positions` (flat `[x0,y0,…]` array),
      `get_normal`/`get_children`/`get_shapes` as Hashes/Arrays, the friction/
      restitution/enabled/tangent-speed get/set + resets. `Contact.h` is mruby-safe
      now (its `common/runtime.h` include + the Lua `getPositions`/`getNormal` are
      guarded; the wrapper reimplements them over `getBox2DContact()`), so its
      include is un-guarded in `Physics.h`/`World.cpp` and the `EndContact`
      invalidation of a wrapping `Contact` is live. The remaining markers only guard
      the Lua `getPositions`/`getNormal` and the Lua `World`/`Body` `getContacts`
      readbacks, reimplemented in the wrapper. Contact wrapper **identity** across
      calls is preserved via the World object memoizer (`findObject`) for the C++
      Contact, and the Ruby wrapper is now also stable via the #phys-identity
      registry. Covered by `physics_test.rb`.
- [~] (#phys-joints) all 11 joint types + the `Physics` joint factories +
      `World`/`Body` `getJoints` are ported (keyword-argument factories, snake_case
      methods, multi-return getters as Hashes). Joint lifecycle (implicit/deferred
      destruction in `World::SayGoodbye` and the time-step destruct queue) is also
      un-guarded. The remaining `#ifndef LOVE_MRUBY` markers only guard the Lua
      multi-return methods (`getAnchors`/`getReactionForce`/`getTarget`/`getLimits`/
      `getAxis`/`getGroundAnchors`/`getLinearOffset`) and the Lua `getJoints`
      readbacks — all reimplemented in the wrapper over `getBox2DJoint()`; the Lua
      bodies survive behind the guard for the Lua build. Joint object **identity**
      across wrappers is now preserved by the #phys-identity registry.
- [x] (#phys-userdata) `Body`/`Shape`/`Joint` `set_user_data`/`get_user_data`
      are ported. The mruby build stores one arbitrary Ruby value per engine
      object via `mrbx_set_userdata`/`mrbx_get_userdata`/`mrbx_clear_userdata`
      (common/mrb_runtime.cpp): a `(mrb_state*, love::Object*)`-keyed map that
      GC-protects the value (`mrb_gc_register`) while set, mirroring the Lua
      Reference. Keyed by the C++ object, so the value round-trips no matter
      which Ruby wrapper fetches it (e.g. read back via `shape.get_body` or in a
      future collision callback); `set_user_data(value: nil)` clears it. The
      object's `destroy` clears the entry (so a reused address can't return
      stale data) and `mrbx_forgetstate` drops a closing VM's entries. The Lua
      `setUserData`/`getUserData` survive behind `#ifndef LOVE_MRUBY` for the Lua
      build (they need a `lua_State`); that is a permanent dual-build split, not
      a deferral, so it carries no marker. Covered by `physics_test.rb`.
- [x] (#phys-identity) wrapper-identity registry: the same engine object now
      round-trips to one Ruby object (`==`). `mrbx_pushtype` keeps a **weak**
      `(mrb_state*, love::Object*) -> wrapper` map (`objectWrappers` in
      common/mrb_runtime.cpp) and returns the live wrapper if one exists. The map
      is *not* GC-protected, so it never keeps a wrapper (or the C++ object it
      retains) alive; the wrapper's existing free callback (`mrbx_object_free`)
      evicts the entry when the wrapper is collected, and `mrbx_forgetstate`
      drops a closing VM's entries. That weak behaviour is the mruby equivalent
      of the Lua weak-valued userdata table, so it works for **every** love::Object
      type, not just physics, with no leak. Relies on mruby's non-moving GC (the
      stored mrb_value stays a valid heap pointer) and on pushes always using the
      object's concrete type (pushShape/pushJoint dispatch on the runtime type;
      single inheritance keeps the love::Object address identical across static
      types). Covered by the `=== wrapper identity ===` block in physics_test.rb.

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
      exposed so far: **Texture** — `new_image` (a 2D texture: `file:` a filename
      String, an ImageData/CompressedImageData, or an Array of those = explicit
      mip levels), `new_array_image` (`layers:` one entry per layer; the kind a
      SpriteBatch needs for add_layer/set_layer), `new_volume_image` (`layers:`
      one entry per depth slice, or `image:` a single ImageData sliced into depth
      layers) -> a 3D volume texture, and `new_cube_image` (`faces:` 6 entries, or
      `image:` a single ImageData split into 6 faces) -> a cube texture. Each
      "entry" is a single image **or** an Array of mip-level images; all four take
      the common settings `linear:`/`mipmaps:` (`true` builds a full mip chain,
      generated from level 0 when only the base is given)/`dpi_scale:`, and
      `new_image` also `format:`/`msaa:`. A shared image-data resolver handles the
      filename/ImageData/CompressedImageData inputs (a compressed image
      contributes its base slice). The dpiscale-from-`@2x`-filename autodetection
      and the texture-view / viewformats / computeWrite settings aren't ported.
      Query getters: dimensions / `get_texture_type` ("2d"/"array"/"cube"/
      "volume") / `get_layer_count` / `get_depth` (`mipmap:` 1-based) /
      `get_mipmap_count` / `is_compressed` + `set_filter`/`get_filter`. **Quad**
      (`new_quad` + `get_viewport`/
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
      screen, plus optional `format:`/`msaa:`/`readable:`, and `type:` "2d"/
      "array"/"volume"/"cube" + `layers:` for a layered render target + `mipmaps:`
      for a mipmapped one) returns a render-target `Love::Texture`; `set_canvas`
      (`canvas:` a Texture or Array of them for MRT, nil/omitted resets to the
      backbuffer; for a single non-2D/mipmapped target `slice:`+`mipmap:` (1-based)
      select the layer/face/depth-slice and mip level, or in the MRT Array each
      element may be a Hash `{texture:, slice:/layer:/face:, mipmap:}`;
      `depthstencil:` is an explicit depth/stencil Texture, else `stencil:`/
      `depth:` request a temporary buffer) / `get_canvas` (a Texture, or a
      `{texture:, slice:, mipmap:}` Hash for a non-2D/non-zero-slice target, or an
      Array for MRT, or nil). Stencil/depth render state
      is exposed: `set_stencil_mode` (`mode:` "off"/"draw"/"test"/"custom",
      omitted resets to off; `value:` defaults 1) / `get_stencil_mode` (-> Hash
      {mode:, value:}), the low-level `set_stencil_state` (`action:`/`compare:`/
      `value:`/`read_mask:`/`write_mask:`; no args resets to keep/always) /
      `get_stencil_state` (-> Hash; masks are Numbers since `0xFFFFFFFF` exceeds
      this build's 31-bit boxed-int range), and `set_depth_mode` (`compare:`,
      `write:`; both omitted reset) / `get_depth_mode` (-> Hash {compare:,
      write:}; depth has no separate low-level state in LÖVE — set_depth_mode is
      the whole API). The `Love::SpriteBatch`
      object type is exposed: `new_sprite_batch` (`texture:`, `size:` default
      1000, `usage:` "dynamic"/"static"/"stream") and the methods `add` /`set`
      (`quad:` optional + the standard transform; `add` returns the 1-based
      index), `add_layer`/`set_layer` (array-texture layer sprites: `layer:`
      1-based + `quad:` + the standard transform; the batch's texture must be an
      array texture — see `new_array_image`), `attach_attribute` (`name:` +
      `buffer:`/`mesh:`, binds a per-sprite vertex attribute), `clear`, `flush`,
      `set_texture`/`get_texture`, `set_color`/`get_color`, `get_count`,
      `get_buffer_size`, `set_draw_range`/`get_draw_range` (1-based, Hash or
      nil). The `Love::TextBatch` object type
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
      `Love::Mesh` object type is fully exposed, for both the **standard vertex
      format** (a vertex is `[x, y, u, v, r, g, b, a]`) and **custom vertex
      formats**. Vertex read/write is driven by the mesh's actual format via the
      shared Buffer data helpers (the standard format is just one such format,
      so it round-trips identically). `new_mesh` takes three constructors:
      `format:` (an Array of declaration Hashes `{name:, format:, array_length:,
      location:}`) for a custom format, else the default standard format, with
      `vertices:` (an Array of per-vertex component arrays), `data:` (a Data of
      packed vertex bytes), or `count:` (empty); or `buffers:` (an Array of
      attribute Hashes `{buffer:, location:, name:, step:, location_in_buffer:,
      name_in_buffer:, start_index:}`) for a mesh sourced from existing GPU
      Buffers; plus `mode:` "fan"/"strip"/"triangles"/"points" and `usage:`. The
      methods: `set_vertex`/`get_vertex` (1-based; `get_vertex` returns a flat
      component Array in format order) / `set_vertices` / `set_vertex_attribute`/
      `get_vertex_attribute` (`attribute:` a 1-based attribute index, `value:`/
      return an Array of that attribute's components) / `get_vertex_count` /
      `get_vertex_format` (-> Array of member Hashes `{name:, location:, format:,
      array_length:, offset:}`) / `set_attribute_enabled`/`attribute_enabled?`
      (by `name:` or `location:`) / `attach_attribute` (bind a Buffer/Mesh as a
      named or located attribute; `step:` "pervertex"/"perinstance",
      `attach_name:`/`attach_location:`, `start_index:`) / `detach_attribute`
      (`name:` -> bool) / `get_attached_attributes` (-> Array of Hashes) /
      `get_vertex_buffer` / `set_texture`/`get_texture` / `set_draw_mode`/
      `get_draw_mode` / `set_draw_range`/`get_draw_range` / `set_vertex_map`
      (an Array of 1-based indices, or a Data + `index_type:` "uint16"/"uint32"
      + optional `count:`) / `get_vertex_map` (1-based) / `set_index_buffer`/
      `get_index_buffer` (an explicit GPU index Buffer) / `flush`. The legacy
      `{name, datatype, components}` format-declaration form is intentionally
      dropped (deprecated upstream; use the declaration Hash). Covered by
      `mesh_test.rb`. The
      `Love::Video` object type is exposed (`new_video` (`file:` an .ogv theora
      filename, `dpi_scale:`) + `play`/`pause`/`seek`/`rewind`/`tell`/`playing?`
      / `get_stream` / `get_source` / dimensions / `set_filter`/`get_filter`),
      backed by the now-linked **love.video** theora module (see its own line
      below). The low-level GPU **Buffer** and **GraphicsReadback** types are
      exposed too: `new_buffer` (`format:` a single format String or an Array of
      declaration Hashes `{name:, format:, array_length:, location:}`; `data:` a
      Data / an Array of component arrays / a flat Array, or `count:` for an
      empty zero-initialized buffer; `usage_flags:` an Array of "vertex"/"index"/
      "texel"/"shaderstorage"/"indirectarguments"; `usage:` the data-usage hint;
      `debug_name:`) returns a `Love::GraphicsBuffer` (the type's engine name) with
      `set_array_data` (`data:`/`source_index:`/`dest_index:`/`count:`, 1-based) /
      `clear` (`offset:`/`size:`) / `get_element_count` / `get_element_stride` /
      `get_size` / `get_format` (-> Array of member Hashes) / `buffer_type?`
      (`type:`) / `get_debug_name`. Readbacks: `readback_buffer`
      (`buffer:`/`offset:`/`size:`/`dest:`/`dest_offset:`) -> a `Love::ByteData`
      synchronously, and `readback_texture` (`texture:`/`slice:`/`mipmap:`/`x:`/
      `y:`/`width:`/`height:`/`dest:`/`dest_x:`/`dest_y:`) -> a `Love::ImageData`;
      the `_async` variants return a `Love::GraphicsReadback` (`complete?` /
      `error?` / `wait` / `update` / `get_buffer_data` / `get_image_data`).
      **With this every graphics object type is exposed.**
      `ParticleSystem#clone` is ported — `clone()` then `mrbx_pushtype`, mirroring
      wrap_ParticleSystem.cpp; the copy carries over the emitter config + `active`
      state but starts with no live particles. Covered by `particle_test.rb`. Text
      (`print`/`printf`, `new_text_batch`, and TextBatch `set`/`setf`/`add`/`addf`)
      accepts either a plain String or the **colored-string-segments** form — a Ruby
      Array alternating color arrays (`[r,g,b]` or `[r,g,b,a]`, 0..1) with Strings,
      each color applying to the strings after it. Built by `check_colored_string`
      in wrap_Graphics_mrb.cpp, faithful to `luax_checkcoloredstring`. Covered by
      `textbatch_test.rb`.
      The graphics module is now feature-complete for the practical surface: the
      texture creators take explicit mip-level arrays, the `mipmaps:`/`dpi_scale:`/
      `format:`/`msaa:` settings, and the single-image cube/volume splits; layered
      and mipmapped render-target canvases plus the slice/mipmap/depthstencil
      set_canvas variants, the low-level stencil state, the SpriteBatch
      array-texture layers + attach_attribute, and the Mesh custom-vertex-format /
      attach_attribute / explicit-index-buffer paths are all ported (see those
      paragraphs). The only deliberately-unported bits are obscure texture knobs:
      the `@2x`-filename dpiscale autodetection, texture views
      (`newTextureView`), and the `viewformats`/`computewrite` creation settings.

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

- [x] Module-singleton teardown on quit. `mrbx_register_module` used to `retain()`
      each module on top of the init's own `new`/`retain`, and the module was bound
      to a plain Ruby module with no GC free-func — so `mrb_close` released nothing
      and the Window/Graphics/Audio/Font singletons leaked on quit in BOTH the
      harness and the real exe. (The Lua build releases them via the Proxy userdata
      `__gc` on `lua_close`, `common/runtime.cpp`.) Fixed by tracking each state's
      module instances (`moduleInstances` in `common/mrb_runtime.cpp`) and releasing
      them in a single `mrbx_close_state(mrb)` — used everywhere a love `mrb_state`
      is closed (`love_mrb.cpp`, `harness.cpp`, `LuaThread_mrb.cpp`) in place of a
      bare `mrb_close`. It runs `mrbx_forgetstate`, then `mrb_close` (so Ruby objects
      die first), then releases the modules in reverse registration order — which
      tears down graphics before the window so `~Graphics` frees GPU state while the
      GL context is alive. The one non-linear dependency: the graphics module owns an
      internal default `Font` (not a Ruby object, so it outlives `mrb_close`) whose
      FreeType face uses the `FT_Library` owned by the font module by raw pointer
      (`font/freetype/Font.cpp`); since graphics is registered after font, reverse
      order would free font first, so `mrbx_close_state` defers the font module past
      every other module. `mrbx_register_module` no longer retains (it tracks); the
      five name-collision modules that bypass it (data/thread/joystick/font/video)
      normalized to one `if-null-new-else-retain` binding ref + `mrbx_track_module`.
      Verified: real exe quits cleanly; ASan/LSan on a quitting game = 0 errors, 0
      `love::` leak frames (only external dbus/nvidia driver allocations remain); the
      `*_test.rb` suite still passes (23/23, once `filesystem_mount_test` was made
      hermetic). Corrects the two notes in the Memory-audit / broader-bug-hunt items
      that assumed the exe already tore modules down on quit.
- [x] Object/proxy identity map: an mruby equivalent of Lua's weak-table map so
      the same C++ object always maps to the same Ruby object — done by the
      physics-slice work (see §A `#phys-identity`), but the mechanism is
      **general**, not physics-specific. `mrbx_pushtype` (the single push path
      for every `love::Object` type) keeps a weak `(mrb_state*, love::Object*) ->
      wrapper` map (`objectWrappers` in `common/mrb_runtime.cpp`) and returns the
      live wrapper if one exists, so the same C++ object always maps to one Ruby
      object (`==`). The map is not GC-protected (so it never keeps a wrapper or
      its C++ object alive); `mrbx_object_free` evicts on collection and
      `mrbx_forgetstate` drops a closing VM's entries. This is the mruby analog
      of Lua's weak-valued userdata table.
- [x] CMake: build/link `libmruby.a` instead of `lovedep::Lua`. Done via
      `cmake -DLOVE_MRUBY=ON`: a ~10-line hook in `CMakeLists.txt` includes
      `cmake/LoveMruby.cmake`, which builds a working `love` (the `love_mrb`
      OBJECT library of every ported binding + the bundled archives + the
      embedded boot scripts + `src/love_mrb.cpp`). `testing/mruby/CMakeLists.txt`
      reuses the same module so there is one source list. `src/libraries/lua53`
      and the LuaJIT path are **not** removed — deliberately: the parallel-path
      approach (chosen for upstream-sync friendliness, see `SYNC.md`) leaves the
      Lua build intact and simply doesn't reference lua53/LuaJIT/socket/enet in
      the mruby path. Remaining polish (CI-on-real-runner) is tracked in
      `CMAKE_MIGRATION.md`; nogame.rb, install rules, and the shared `liblove`
      one-symbol ABI are done.
- [ ] FFI fast paths: re-implement the few wrappers that use LuaJIT FFI.
- [ ] Fused-mode (self-contained executable). The Lua build can run "fused":
      a `.love` archive is appended to the `love` executable to make one
      self-contained binary that boots its own game (no separate game argument).
      **Confirm this works for the port — currently unverified, and likely not
      wired up.** State of play:
      - The **C++/physfs machinery is ported and exercised:** `Filesystem::setFused`
        / `isFused` (filesystem/physfs/Filesystem.cpp) and the fused special-cases
        in `mount`/`unmount`/`getRealDirectory` (the `isFused() && sourceBase ==
        archive` branches that mount the executable itself as the source archive,
        skipping any non-archive prefix bytes) compile in the mruby build, and
        `set_fused`/`fused?` are bound + covered by `filesystem_mount_test.rb`
        (see §A filesystem `#fs-platform`).
      - **CONFIRMED: it does NOT work for the port** (verified 2026-06-25 by
        reading the exe's game-acquisition path, not just inferred). Two layers
        are missing:
        - *The exe never mounts an archive as the game.* `love_mrb.cpp`'s
          `load_game_source` reads the game **directly off the host filesystem
          via stdio** (`read_file`) — a single `.rb` file, or a directory's
          `main.rb` — and hands the raw text to boot.rb as `$LOVE_GAME_SOURCE`
          (a String it `eval`s). There is no physfs source mount, no `.love`
          (zip) game container, and no reading of an archive appended to the
          executable. The port doesn't even consume a plain `.love` as a game
          yet, and fused mode is a strict superset of that.
        - *The boot pipeline never drives fused.* `src/modules/love/boot.rb` (see
          its own comment, "the source-detection dance of boot.lua collapses to
          'use the working tree'") never calls `isFused`, never mounts the
          executable as the source, never latches `set_fused`. Lua's boot.lua
          instead detects a fused exe (its appended archive mounts), calls
          `setFused(true)`, sets the source to the executable path, and derives
          the identity from it. None of that exists here.
        So appending a `.love` to `build-mrb/love` and running it with no game
        argument would just hit the no-game screen (or error), not boot the
        appended game. Only the isolated `fused?` latch is exercised by the unit
        test; no end-to-end fused path exists.
      To close this: (1) decide whether fused deployment (and `.love`-archive
      games generally) are in scope for the port — the exe currently runs raw
      `.rb`/`main.rb` source, so this is a sizeable feature, not a tweak;
      (2) if so, teach the exe to mount a `.love` (and the self-appended archive)
      as the physfs source and load `main.rb` through the virtual FS, and port
      boot.lua's fused-detection + executable-self-mount + `set_fused` latch into
      boot.rb; (3) verify end to end — `cat build-mrb/love game.love > fused &&
      chmod +x fused && ./fused` boots the game with no argument. Record the
      outcome here. No code site yet, so this item is untagged.
- [x] Port the remaining `wrap_*.cpp` modules. All 21 LÖVE modules and every
      object type they expose are ported, including the low-level GPU `Buffer`
      (`GraphicsBuffer`) and `GraphicsReadback` types. Every per-type *method*
      feature noted in §A/§B is ported too (ParticleSystem#clone, colored-string
      text, Mesh custom formats / attributes / index buffers, SpriteBatch layers +
      attach_attribute, low-level stencil state, the 2d/array/volume/cube texture
      creators with mip-array/`mipmaps:`/`dpi_scale:`/`format:` inputs and the
      single-image cube/volume splits, layered/mipmapped render-target canvases,
      and the slice/mipmap/depthstencil set_canvas variants). No whole module,
      object type, or feature is unported; the only deliberate omissions are a few
      obscure texture knobs (the `@2x`-filename dpiscale autodetection, texture
      views, the `viewformats`/`computewrite` settings) — see the graphics §B note.
- [x] Memory audit (static pass + ASan error-detector run + suppression-filtered
      LeakSanitizer run all done). Findings:
      • **ASan run: clean across the whole binding surface.** Built an
        AddressSanitizer harness — `make SANITIZE=1 BIN=…love_mrb_harness_asan`
        (a new Makefile knob) instruments the directly-compiled binding layer
        (`wrap_*_mrb.cpp` + `mrb_runtime` + the module engine `.cpp`) while the
        prebuilt archives (gfx/glslang/box2d/physfs/…) and libmruby stay
        uninstrumented; ASan's global `operator new/delete` + malloc interceptors
        still cover those, so heap-overflow / use-after-free / double-free /
        alloc-dealloc-mismatch are caught process-wide. Ran all 23 `*_test.rb`
        under `DISPLAY=:1 ASAN_OPTIONS=detect_leaks=0` (leaks muted per the note
        below). **No UAF, double-free, or heap-overflow in the binding layer.**
      • **One real bug found + fixed (upstream, not the port):**
        `graphics::Mesh::~Mesh()` freed `vertexData` (allocated with
        `new uint8[]`) using scalar `delete` — a `new[]`/`delete` mismatch (UB),
        flagged by ASan via `mesh_test.rb`. Pre-existing upstream (commit
        1f9d98263, 2020); fixed here to `delete[]` (correct for both builds).
        `indexData` uses `realloc`/`free` (matched). After the fix the mesh
        report is gone and the graphics suite re-runs clean.
      • **Out of scope (not a memory issue):** `filesystem_mount_test.rb` exited 1
        on both the ASan and the normal harness — it mounted `/tmp/lovefs_test`, a
        fixture nothing created in this environment; a missing-fixture test gap,
        unrelated to the audit. (Since fixed: the test is now hermetic — it
        self-provisions a save-area subdir to mount and decodes an embedded base64
        zip — so the suite runs 23/23 from a clean checkout.)
      • **LeakSanitizer run: clean, one binding leak found + fixed.** Ran the
        whole suite with `detect_leaks=1` and `LSAN_OPTIONS=suppressions=`
        `leak_suppressions.txt` — a `leak:` file for the uninstrumented external
        allocators (GL driver / SDL / X11 / ALSA-OpenAL), which leak context- and
        device-lifetime state inside the driver. (At the time this note assumed the
        love module singletons themselves were never torn down — "the real `love`
        exe does that on quit" was wrong for the mruby exe too; both now tear them
        down via `mrbx_close_state`. See the "Module-singleton teardown on quit"
        item in §C.) With those suppressed, exactly **one** genuine binding-layer
        leak remained: `k_require` (`wrap_Filesystem_mrb.cpp`) constructed a
        `std::string modulename`/`resolved` that was still alive when the
        not-found / read-fail / raising-file paths called `mrb_raisef` — mruby's
        longjmp skips the C++ destructor, leaking the string's heap buffer (24 B,
        surfaced by `loader_test.rb`'s require-nonexistent case). Fixed by
        resolving inside a no-longjmp scope and handing the path out as a
        GC-managed mrb_value, so every raise happens after the std::string locals
        are destroyed. After the fix the entire suite is leak-clean (0 residual
        blocks with suppressions). General lesson recorded for future ports: never
        hold a non-trivially-destructible C++ local across a longjmping mruby call
        (raise/load) — the same RAII-vs-longjmp hazard `perform_atomic` guards
        against with `mrb_protect_error`.
      • **Class (2) retained-reference balance: clean.** There are no
        `mrb_gc_register`/`unregister` calls outside `common/mrb_runtime.cpp` —
        all retention flows through the balanced helpers (`mrbx_set_userdata` /
        `mrbx_set_callback`, each unregistering on replace/clear), and
        `mrbx_forgetstate` drops a closing VM's entries across **all four** stores
        (typeClasses, userData, objectWrappers, callbacks).
      • **Class (1) arena hygiene: fixed the unbounded yield-in-loop sites.** The
        physics `query_shapes_in_area` / `ray_cast` block callbacks
        (`QueryBlock` / `RayCastBlock::ReportFixture`) pushed a transient Shape
        wrapper per fixture with no arena restore — a wide query/long ray pinned
        O(n) garbage; now save/restore the arena per call (the `map_pixel`
        pattern). The other `mrb_yield*` sites (thread/data `perform_atomic`,
        window file-dialog) are one-shot, not loops. Output-proportional builders
        (`get_bodies`, the per-step collision-callback argv) stay as-is (bounded
        by the result / by one physics step).
      • **UAF/error + leak runs: both done** (see the ASan and LeakSanitizer
        summaries at the top of this item — the harness is `make SANITIZE=1`, run
        with `ASAN_OPTIONS=detect_leaks=0` for errors and `detect_leaks=1` +
        `leak_suppressions.txt` for leaks). Broader tool coverage beyond ASan is
        tracked in **§E. Bug-hunt tool roster** (ASan/LSan are its first two,
        done rows).
      The original guidance follows.

      Sweep the mruby bindings for allocation/deallocation correctness. Two
      classes to look for:
      (1) **GC-arena hygiene** — high-iteration loops that create and discard heap
      `mrb_value`s (Arrays/Hashes/Strings/wrappers) without `mrb_gc_arena_save`/
      `restore` pin O(n) garbage for the whole call. `Image#map_pixel` was fixed
      this way; re-scan every per-pixel/per-sample/per-vertex loop and any
      `mrb_yield*` inside a loop. (Note: under this build's word boxing, ints and
      floats are immediates, so number-only loops are exempt — only heap objects
      count.) (2) **Retained-reference balance** — every `mrb_gc_register` /
      retain / `mrbx_set_userdata` / `mrbx_set_callback` has a matching
      unregister/release on teardown *and* in `mrbx_forgetstate` (a closing VM),
      with no leak on the replace path. Output-proportional builders (e.g.
      `World#get_bodies`, collision-callback argv) are fine — their arena use is
      bounded by the result they hand back. Consider a leak run under
      valgrind/ASan once CMake replaces the harness Makefile.

- [ ] Audit for **dormant functions** — code the port no longer reaches and
  must deliberately decide about. The parallel-path port (Lua kept intact for
  upstream-sync, see the CMake item) leaves shared `common/` primitives that the
  Lua runtime calls but the mruby bindings never do. Dormant ≠ harmless: such a
  function can carry a latent bug that nothing currently triggers, so it passes
  every dynamic tool (ASan/UBSan/TSan) yet detonates the moment a future binding
  starts calling it — with no test or reviewer expecting it. **Sweep for them and,
  per function, record one disposition in this ledger:** (a) **Lua-only, leave
  intact** — kept for upstream sync, just note it's dormant in the port so nobody
  trusts a clean tool run over it; (b) **mruby scaffolding, now unused** — remove
  it; (c) **shared primitive that could be reactivated** — either fix it to be
  correct under the port's assumptions (e.g. its concurrency model) now, or add an
  `assert`/comment that it must not be used from the mruby path until fixed.
  - **How to find them:** functions declared in `common/` (and module headers)
    whose only call sites are `common/runtime.cpp` / `wrap_*.cpp` (Lua) and never
    the `_mrb` bindings — `grep` each suspect's name across `src` and check the
    hits are all Lua-path. The `_mrb.cpp` / non-`_mrb.cpp` split is the main tell.
  - **Seed (found in the §E row-4 TSan pass):** `love::Type::init()` / `getId()` /
    `isa()` and the non-atomic `static uint32 nextId` in `common/types.cpp`.
    Dormant in the port (bindings type-check via Ruby's `mrb_obj_is_kind_of` and
    key maps by `Type*` identity; `init()` is only called from the Lua
    `runtime.cpp`). It has a **real data race** (`nextId++` RMW + unsynchronized
    `id`/`inited`/`bits` writes) and a worse correctness bug (two threads
    first-touching a virgin type could assign it two different ids → broken
    `isa`). Disposition (c): currently Lua-only so leave intact, but **if any
    mruby binding ever starts using `Type::init/getId/isa`, make it thread-safe
    first** (once-init under a mutex with a lock-free `inited` fast path) — the
    port runs one `mrb_state` per OS thread, so first-touch from two thread VMs
    would race immediately.

- [ ] Revisit how a game registers callbacks (`Love.load`/`update`/`draw`/…) —
  is reopening the `Love` module idiomatic Ruby, or is there a better shape?
  Today a game defines its callbacks as **public singleton methods on the `Love`
  module** — `def Love.load(args, raw); end`, `def Love.update(dt); end`,
  `def Love.draw; end`, etc. — and the run loop discovers them with
  `Love.respond_to?(:name)` / `Love.send(name, …)` (see `callbacks.rb` and its
  header comment). This is a faithful, near-literal port of Lua's "assign
  functions onto the global `love` table" idiom, but Lua-on-a-table ≠ idiomatic
  Ruby, and it has real friction:
  - **Name collisions with `Kernel`.** `Love.load` shadows the private
    `Kernel#load`; the code already documents that `respond_to?` only sees the
    callback once the game defines a *public* `Love.load`. Other callback names
    could collide similarly (`p`, `print`, `format`, …) and the framework can't
    control the game's chosen names.
  - **No separation between framework and game code** — the game monkeypatches
    the framework's own module, so framework internals and game callbacks share
    one namespace, with no encapsulation or per-instance state.
  - Evaluate the Ruby-idiomatic alternatives and pick one (or deliberately keep
    the current shape and record why):
    (a) **a game base class** the user subclasses and overrides instance methods
        on — `class MyGame < Love::Game; def load(args, raw); end; def draw; end;
        end` — then boot instantiates it; clean encapsulation + per-game state,
        the most conventional Ruby/OOP approach, but the biggest departure from
        the LÖVE mental model;
    (b) **a mixin/`Love::Callbacks` module** the game includes into its own
        object;
    (c) **block/DSL registration** — `Love.on(:load) { |args, raw| … }` or
        `Love.load { … }` — no method-name collisions, explicit registry instead
        of `respond_to?` probing;
    (d) **keep singleton-methods-on-`Love`** but harden it (a dedicated callback
        registry / `Love.callbacks` namespace rather than the bare module, to
        dodge the `Kernel` collisions).
  - Weigh against the **port's stated goals**: it already breaks from Lua on API
    convention (keyword args, snake_case, `?`-predicates under `Love`), so a more
    Ruby-idiomatic callback shape is in keeping — but it changes every game's
    entry point, so document the migration and update `nogame.rb`, the
    `testing/mruby/*.rb` games, and the README examples to match whatever is
    chosen. No code-site marker yet (a design decision), so this item is untagged.

- [ ] Audit DragonRuby's mruby fork for patches worth adopting — **clean-room**.
  DragonRuby ships its own patched mruby (`https://github.com/DragonRuby/
  mruby-patched`); they have shipped a commercial game engine on mruby for years
  and have almost certainly hit — and patched — the same VM-level rough edges
  this port keeps working around (GC arena/longjmp-vs-RAII hazards, the 31-bit
  boxed-int literal cap noted in §A `#data-pack`, thread/`mrb_state` isolation,
  performance/footprint tweaks, missing core methods). Determine **what** they
  changed to upstream mruby and **whether** we should make an equivalent change
  here. Deliverables:
  - Identify each customization: what mruby version/commit they forked from, then
    what diverges — diff against that upstream base and read their commit
    history / changelog / build config (`build_config.rb`, gembox, any `mrbgems`
    they bundle or patch). Group findings (correctness/VM fixes · core-method
    additions · integer/float/boxing changes · GC/memory · build/footprint ·
    platform shims).
  - Per customization, record a disposition in this ledger: (a) **adopt** — it
    fixes a real problem the port has or will have (cross-reference the relevant
    §A/§C item, e.g. the int-literal cap, the longjmp/RAII hazards); (b) **N/A** —
    DragonRuby-engine-specific, or already handled differently here (e.g. our
    `mrbx_*` runtime, per-thread VM model); (c) **defer** — plausibly useful, not
    now.
  - **Hard constraint — do NOT copy any code from that repo.** Read it only to
    learn *what* was changed and *why*; any patch we adopt must be **independently
    reimplemented** against our own mruby from the public problem description /
    upstream-mruby context, with no DragonRuby source pasted or transliterated.
    Note their license terms in the writeup so the clean-room boundary is on
    record. No code site yet, so this item is untagged.

- [ ] Implement the **Vulkan and Metal** graphics backends (the port is
  OpenGL-only today). The `#gfx-backend` swap (§B) brought up the real
  `graphics::Graphics` via `Graphics::createInstance()`, but **only the OpenGL
  backend** — Vulkan and Metal are compiled out: the build defines
  `LOVE_MRUBY_NO_VULKAN` (`testing/mruby/Makefile:54`, and the CMake
  `LOVE_MRB_DEFS` in `cmake/LoveMruby.cmake:81`) and links only `graphics/opengl`,
  so neither `LOVE_GRAPHICS_VULKAN` nor `LOVE_GRAPHICS_METAL` is ever defined and
  the renderer loop in `Graphics::createInstance` (`graphics/Graphics.cpp:165`)
  can only resolve `opengl::createInstance()`. The abstract `love::graphics`
  interface the Ruby bindings call through is renderer-agnostic, so — exactly like
  the OpenGL swap — **no `wrap_*_mrb.cpp` change should be needed**; this is a
  build/link + platform-integration job, not a binding job. Pieces:
  - **Vulkan (cross-platform — Linux/Windows/Android; testable in this env).**
    Build/link `src/modules/graphics/vulkan/*.cpp` and define
    `LOVE_GRAPHICS_VULKAN` (drop it from the `NO_VULKAN` exclusion in both the
    Makefile harness and `LoveMruby.cmake`). Wire its deps: the Vulkan loader/SDK
    headers and the VMA (Vulkan Memory Allocator) the backend uses. **SPIR-V:**
    glslang is already linked (it backs the OpenGL shader validation/reflection),
    so confirm it is built with the SPIR-V target enabled and that
    `graphics/vulkan` shader compilation path is reachable. **Window/surface:** the
    SDL window backend currently creates a GL context — the Vulkan path needs a
    `VK_KHR_surface` from the SDL window instead (`window/sdl/Window.cpp`
    `setWindow`/context creation), under the renderer kind.
  - **Metal (Apple-only — macOS/iOS; NOT testable in this Linux dev env).**
    Build/link `src/modules/graphics/metal/*` (Objective-C++ `.mm` TUs), define
    `LOVE_GRAPHICS_METAL`, link the Metal/QuartzCore frameworks, and create a
    `CAMetalLayer`-backed surface from the SDL window. Shader path is
    glslang→SPIR-V→SPIRV-Cross→MSL — confirm SPIRV-Cross is available/linked.
    This can only be brought up and verified on Apple hardware, so it likely lands
    as a separate, untested-here slice gated behind the Apple platform.
  - **Renderer selection.** Once more than one backend exists, the
    `getRenderers`/`setRenderers`/`rendererOrder` machinery (`graphics/Graphics.cpp`)
    becomes live; decide whether to expose renderer choice / `get_renderer_info`
    to Ruby and honor a `conf` renderer preference, and update the
    `#gfx-backend` §B note (which currently states Vulkan/Metal are out of the
    build) when this lands.
  - **Verification.** Vulkan: run the `testing/mruby/*.rb` graphics games + the
    graphics test suite against the Vulkan backend under `DISPLAY=:1` (and fold it
    into the §E sanitizer rows). Metal: deferred to an Apple host. No code-site
    marker yet (it un-excludes + links backends rather than guarding a tagged
    deferral site), so this item is untagged.

- [ ] Support compiling the port for **Windows** targets (it builds Linux-only
  today). Upstream LÖVE's Lua build already ships on Windows, so the engine `src/`
  is largely Windows-clean (the `LOVE_WINDOWS`/`_WIN32` guards and the win32-only
  source paths already exist and the Lua build exercises them); the gap is the
  **mruby parallel-path build plumbing**, which is currently Unix-only:
  - **CMake (`cmake/LoveMruby.cmake`).** It assumes a Linux host throughout —
    `find_package(PkgConfig REQUIRED)` + `pkg_check_modules` for freetype /
    harfbuzz / openal (and the vorbis/ogg/modplug/theora system libs the modules
    link), a hardcoded `SDL_LIBDIR=/usr/local/lib`, `.a`/`.so` artifacts, and an
    implicit Linux platform define. For MSVC there is no pkg-config: source the
    deps via vcpkg / `find_package` / prebuilt bundles (upstream uses its
    "megasource" dep pack for Windows) instead, set `LOVE_WINDOWS`/`_WIN32`
    appropriately, and emit `.exe` + `liblove.dll`/`.lib` (the one-symbol
    `love_mrb_main` ABI carries over) with the Windows icon/manifest resource.
  - **libmruby for Windows.** `libmruby.a` is built by the Unix
    `make -C testing/mruby mruby` recipe against a host Ruby; Windows needs mruby
    built with an MSVC/mingw `build_config.rb` (native on Windows, or a mingw-w64
    cross-build from this Linux env).
  - **Dev harness Makefile.** `testing/mruby/Makefile` is GNU-make + Unix tools
    and the test run loop needs `DISPLAY=:1` (X11). Windows verification would go
    through the CMake harness target (not the Makefile), with the X11 assumption
    dropped; decide whether the Makefile harness stays Linux-only and CMake is the
    cross-platform path (consistent with `CMAKE_MIGRATION.md`).
  - **Decide the toolchain + what's testable here.** Native **MSVC** and/or
    **mingw-w64**. Only a **mingw-w64 cross-compile from this Linux box** can be
    built/smoke-tested without a Windows host; native MSVC + real-window runtime
    verification needs a Windows machine, so that part likely lands as an
    unverified-here slice. Renderer note: OpenGL works on Windows via WGL (no
    extra work); the Vulkan/Metal item above is orthogonal (Metal is Apple-only).
  - **Cross-references:** update `SYNC.md`/`CMAKE_MIGRATION.md` and the §B
    `#gfx-backend`/build notes as platforms are added; this is the first step of a
    broader "ship the port on all of upstream's platforms" effort (macOS, Android,
    iOS would each be their own ledger items). No code-site marker (build/toolchain
    plumbing, not a guarded deferral site), so this item is untagged.

- [ ] **macOS** target. Like the Windows item, the engine `src/` already builds
  on macOS for the Lua build (`LOVE_MACOS`/`LOVE_APPLE` guards, the Cocoa/`.mm`
  source paths exist); the gap is the mruby parallel-path plumbing. CMake needs
  the Apple toolchain path (frameworks instead of pkg-config for some deps —
  OpenAL/CoreAudio, Cocoa, OpenGL/Metal), a macOS libmruby `build_config.rb`, an
  `.app` bundle layout, and code-signing/notarization hooks for distribution.
  Ties to the **Metal** backend item (the preferred renderer on Apple). Buildable
  and verifiable only on Apple hardware, so unverified in this Linux env. Untagged.
- [ ] **Android** target. The heaviest platform: NDK toolchain, the Gradle/APK
  build, JNI activity glue, asset packing into the APK (physfs reads from the
  APK), the Android save-storage routing (already bound — §A `#fs-platform`
  `set_android_save_external`), GLES/Vulkan renderer selection, and a mruby
  `build_config.rb` cross-compiled per ABI (arm64/armv7/x86_64). Mobile lifecycle
  + touch/sensor (ported, §A) must be exercised on-device. Verifiable only on a
  device/emulator. Untagged.
- [ ] **iOS** target. Apple mobile: the Xcode project / `.ipa`, the iOS libmruby
  cross-build, Metal renderer (ties to that item), the touch/sensor input family
  (ported), App Store signing/provisioning. Verifiable only on Apple hardware.
  Untagged.
- [ ] **Port & run LÖVE's official testsuite under mruby** — the biggest
  correctness gap. Upstream ships a comprehensive suite in `testing/`
  (`testing/main.lua`, the `TestSuite`/`TestModule`/`TestMethod` classes in
  `testing/classes/`, and `testing/tests/*.lua` — 21 files, **~364 test
  functions**, e.g. ~444 assert/method sites in `graphics.lua` alone) with
  **golden reference-image comparison** (`testing/output/`, `testing/resources/`).
  The port currently has only **24 bespoke `testing/mruby/*_test.rb`**, and no
  reference-image regression coverage at all. Port the harness + the per-module
  tests to the Ruby keyword-arg API (or drive the assertions against the Ruby
  bindings) and wire up the pixel-diff image comparison. This is functional
  coverage, complementary to the §E bug-hunt roster (which is memory/UB/race
  tooling, not feature correctness). Untagged.
- [ ] **Render the real error screen** (stale deferral — precondition now met).
  `src/modules/love/callbacks.rb#error_handler` only `puts` the message +
  backtrace to stdout; its own comment says the blue graphics error screen is
  "omitted until graphics is ported" — but graphics **is** ported now (§B
  `#gfx-backend`). Port the full LÖVE error screen: render via `Love::Graphics`
  (the blue screen + formatted message), format the Ruby exception/backtrace,
  support copy-to-clipboard and restart/quit keys, and run its own mini event
  loop, mirroring `callbacks.lua`'s `love.errorhandler`. Untagged.
- [ ] **Complete and honor `love.conf`** (stale stub). `boot.rb#default_config`
  is a 6-field stub (`title`/`identity`/`append_identity`/`window{width,height}`
  + 6 of ~17 modules) and the code admits module enable/disable from conf isn't
  actually applied ("we simply note which ones are present"). Port the full conf
  surface from `boot.lua`: the complete window flag set (fullscreen + fullscreentype,
  vsync, msaa, depth, stencil, resizable, borderless, centered, min width/height,
  display, x/y, highdpi/usedpiscale, displayindex), `t.version`, `t.console`
  (Windows), `t.identity`/`t.appendidentity`, `t.gammacorrect`, the `t.audio`
  block (mic, mixwithsystem), `t.window.icon`, the full module table, and
  `t.renderers`/`t.excluderenderers` (ties to the Vulkan/Metal renderer-selection
  item) — and actually apply module enable/disable and renderer choice. Untagged.
- [ ] **Formal API coverage matrix.** §B asserts "every module/type/feature
  ported," but there is no systematic Lua-`wrap_*.cpp`-vs-`wrap_*_mrb.cpp` diff to
  prove it. Build the matrix (every Lua-exposed module function + object method →
  its mruby binding, with a semantics note) and record each **deliberate
  omission** with a disposition: the texture knobs (`newTextureView`,
  `viewformats`, `computewrite`, the `@2x`-filename dpiscale autodetection — see
  the §B graphics note), the LuaJIT-FFI fast paths (also the standalone FFI item),
  the legacy `{name,datatype,components}` mesh-format form, the `Source#queue`
  raw-pointer form, etc. Goal: turn "we think it's complete" into a checkable
  artifact. Untagged.
- [ ] **Load a plain `.love` archive as the game.** Split out from the fused-mode
  item, which notes it as a prerequisite: today `love_mrb.cpp#load_game_source`
  reads a raw `.rb`/`main.rb` directly off disk via stdio — there is no physfs
  source mount and the exe can't consume a `.love` (zip) game container. Teach the
  exe to mount a `.love` (and a game directory) as the physfs source and load
  `main.rb` through the virtual FS, deriving identity from it — the standard LÖVE
  game-acquisition path. Fused mode (already tracked) is then a superset. Untagged.
- [ ] **Packaging / distribution tooling.** A way to ship a finished game per
  platform — the love-release analog: bundle the game `.love` with the runtime
  into a distributable (Linux AppImage/tarball, Windows `.exe`+`liblove.dll` zip,
  macOS `.app`, Android APK, iOS IPA), including fused-binary creation once that
  works. Depends on the per-platform build items above. Untagged.
- [ ] **CI matrix.** Automated build + test across platforms and renderers on
  real runners (`CMAKE_MIGRATION.md` flags CI-on-real-runner as still pending):
  build each platform target, run the ported official testsuite + the §E
  bug-hunt roster (or a sanitizer subset), and gate merges on them. Untagged.
- [ ] **Performance characterization vs Lua / LuaJIT.** mruby has no JIT and the
  LuaJIT-FFI fast paths were intentionally dropped (Data/window pointer paths,
  the SoundData per-object cache — see §A), so the port is expected to be slower;
  quantify it. Benchmark representative games + hot paths (per-frame draw batching,
  pixel/sample/vertex loops, channel/thread throughput) against the Lua build,
  identify regressions, and decide on optimizations (incl. whether to revisit the
  dropped FFI paths via a native fast path — ties to the FFI item). Untagged.
- [ ] **API documentation / reference.** Every signature differs from the LÖVE
  wiki (keyword args, snake_case, `?`-predicates under `Love`). Produce a Ruby-API
  reference (generated from the bindings if practical, or hand-written) so the port
  is usable without reverse-engineering the wrappers; at minimum document the
  positional→keyword and naming conventions and the per-module method list. Untagged.
- [ ] **UTF-8 / string-encoding parity.** The Lua build bundles and uses the
  `utf8` library (e.g. for text input, `love.keyboard`/`love.graphics` string
  handling). Audit where LÖVE relies on it and confirm Ruby's encoding-aware
  Strings cover the same ground, or provide the equivalent helpers; verify the
  port handles non-ASCII text input/rendering correctly end to end. Untagged.
- [ ] **App-lifecycle & remaining callbacks audit.** Confirm the handler table in
  `callbacks.rb#create_handlers` covers the full LÖVE callback set, especially the
  mobile/lifecycle ones: `lowmemory`, `focus`/`mousefocus`/`visible`,
  `displayrotated`, `filedropped`/`directorydropped`, `resize`, and the gamepad/
  joystick add/remove events. Cross-check against `callbacks.lua`'s handler table
  and add any missing forwards. Untagged.

- Broader bug-hunt beyond ASan — promoted to its own section so each tool is a
  self-contained, context-clear-safe run and the whole set is one roster to work
  from. **See §E. Bug-hunt tool roster.** ASan, LeakSanitizer, and UBSan are done
  (ASan/LSan findings in the memory-audit item above; UBSan findings in §E row 3);
  TSan is done too (row 4: 2 races found + fixed), and Valgrind is done (row 5:
  5 bugs found + fixed — box2d uninitialised `m_u`, `~Event()` queue leak, two
  SDL return-array leaks, and the systemic `mrbx_catchexcept` longjmp-in-catch
  exception-object leak). Static analysis is done too (row 6: binding layer clean
  across clang-analyzer/cppcheck/`-fanalyzer`; root-caused + fixed a harness
  `mrb_noreturn` build quirk that was masking the signal). Fuzzing is done too
  (row 7: five libFuzzer targets over the parsers under ASan+UBSan — found + fixed
  a signed-int overflow miscomputing decoded-image size across four magpie
  handlers, and documented two upstream allocation-amplification DoS surfaces).
  The real-exe sanitizer pass (row 8) is still pending there.

---

## D. Input family

- [x] joystick (gamepad mappings, haptics — heaviest) — see §A.
- [x] touch — see §A.
- [x] sensor — see §A.

This unblocks the `#event-backend` swap in §B (the real SDL event backend needs
the joystick/touch/sensor modules to translate their events).

---

## E. Bug-hunt tool roster — "run the code through all the tools"

A standing QA roster: run the port through each tool below, each specializing in
a bug class the others miss. **This is the single list to work from when asked
to "run the code through all the tools."** Every entry is self-contained (what
it catches · how to run · target · status · findings) so a run can start cold
after a context clear — you do not need any prior conversation, just this
section. As each tool runs, record its findings + fixes inline under that tool
and flip its box. None of these items has a code site (they are process/audit
tasks), so they are untagged and the pre-commit hook ignores them.

### Status at a glance

| # | Tool | Catches | Status |
|---|------|---------|--------|
| 1 | AddressSanitizer (ASan) | heap overflow · UAF · double-free · alloc/dealloc mismatch | ✅ done |
| 2 | LeakSanitizer (LSan) | memory leaks | ✅ done |
| 3 | UBSan | overflow · bad shift/enum/bool · null/misaligned deref · vptr confusion | ✅ done (suite clean; vptr excluded) |
| 4 | ThreadSanitizer (TSan) | data races · lock-order issues across threads | ✅ done (2 races found + fixed; suite clean) |
| 5 | Valgrind / memcheck | uninitialized reads + the *uninstrumented* archives & mruby | ✅ done (5 bugs found + fixed: box2d uninit `m_u`, `~Event()` queue leak, 2 SDL array leaks, `mrbx_catchexcept` longjmp-in-catch exception leak; suite clean) |
| 6 | Static analysis (clang analyzer / `-fanalyzer` / cppcheck) | unreached error/rare-kwarg paths | ✅ done (binding layer clean; root-caused + fixed a `mrb_noreturn` build quirk that was hiding the signal) |
| 7 | Fuzzing (libFuzzer) | data-driven entry points (`pack`/`unpack`, loader, decoders) | ✅ done (1 UBSan int-overflow fixed across 4 image handlers; 2 alloc-amplification cases noted) |
| 8 | Real `love` exe under sanitizers | full boot pipeline / shutdown ordering vs a real game | ✅ done (ASan+UBSan+TSan on the CMake exe; boot→restart→quit clean; 2 OpenAL/PipeWire-teardown races suppressed as external) |

### Shared harness mechanism (read once, applies to the sanitizer rows)

- **Build knob.** `testing/mruby/Makefile` has a `SANITIZE` knob that instruments
  only the directly-compiled binding layer (`$(SRCS)`: `wrap_*_mrb.cpp` +
  `mrb_runtime` + the module engine `.cpp`); the prebuilt archives (gfx/glslang/
  box2d/physfs/lz4/wuff/xxhash) and libmruby stay uninstrumented. The sanitizer's
  global `operator new/delete` + malloc interceptors still cover those, so heap
  errors are caught process-wide while the noise from external libs is muted.
- **The knob now dispatches on kind (done 2026-06-25):** `SANITIZE=<kind>` maps
  to a `-fsanitize=` flag — `address` (`SANITIZE=1` is a legacy alias),
  `undefined`, or `thread`; anything else errors. It adds `-fno-omit-frame-pointer
  -g -O1` and threads the same flag into LDFLAGS. The `undefined` kind also passes
  `-fno-sanitize=vptr`: the vptr (type-confusion) check needs typeinfo for every
  instrumented class, but only the binding layer is instrumented (archives/mruby
  are not), so vptr both fails to link (`undefined reference to typeinfo for
  love::Reference`) and can't see across the uninstrumented boundary anyway.
  Build each kind to its own binary via `BIN=`. TSan (row 4) reuses this as-is.
- **Run the whole suite (24 tests), e.g.:**
  `for t in testing/mruby/*_test.rb; do echo "== $t"; DISPLAY=:1 ./<bin> "$t" || echo FAIL; done`
  (`DISPLAY=:1` is required — the graphics/window tests need an X display.)
  (TSan needs `setarch -R` too: `… setarch -R ./<bin> "$t" …`.)
- **Findings home:** record inline under each tool here, mirroring the
  memory-audit item's style (tool · how invoked · what was found · fix · result).

### Driver script (run the whole roster, emit a Claude-followable report)

- [ ] Write one driver script (e.g. `testing/mruby/run_bug_hunt.sh`) that runs
  the **entire §E roster** end to end and writes a single result file a fresh
  Claude session can act on cold. Today each tool is run by hand from its row's
  re-run command; this collapses them into one reproducible entry point.
  - **Scope = every row, at full coverage.** ASan (row 1), LSan (row 2), UBSan
    (row 3), TSan (row 4), Valgrind/memcheck (row 5), static analysis (row 6),
    fuzzing (row 7), and the real-exe sanitizer pass (row 8). Each runs at **full
    coverage across ALL tests / TUs**, never a subset or a cheaper mode — that is
    the standing rule for this roster (it is why row 5 insists on
    `--leak-check=full` on every test and row 6 on all 161 TUs). Drive each tool
    from its row's documented build + re-run command so the script and the ledger
    can't drift: the `SANITIZE=address|undefined|thread` Makefile harness, the
    normal harness under Valgrind, the `clang++ --analyze`/cppcheck/`-fanalyzer`
    passes, `make fuzz` + the `fuzz/README.md` run block, and the CMake
    `-DLOVE_MRB_SANITIZE=` real-exe builds. Honor the shared prerequisites
    (`DISPLAY=:1`; `setarch -R` for TSan; the per-tool suppression files).
  - **Output: one structured result file** (e.g. `bug_hunt_report.md` under a
    gitignored scratch/output dir, with a machine-parseable summary table plus a
    per-finding section). For each tool record: invocation, pass/fail, the
    suppression file used, wall-clock, and **per finding** the bug class, the
    file:line + symbol, the sanitizer/origin stack (or a path to the saved raw
    log), and the **exact repro/regression command** — i.e. everything the §E
    rows capture by hand, so Claude can open the file after a context clear and
    follow up on any bug without re-deriving how to reproduce it. Save each tool's
    full raw log alongside the summary (referenced by path) rather than inlining
    megabytes.
  - **Robustness:** don't abort the whole run on the first tool's failure —
    run them all (or take a `--only <tool>` / `--from <tool>` selector), capture
    each result, and reflect partial completion in the report so a long run that
    dies midway still yields actionable output. A tool with findings is a
    successful *run* (exit 0 for the driver) but a non-clean *result* — make that
    distinction explicit in the summary so "the script failed" and "the code has
    a bug" never get conflated. Note that builds are the slow part: reuse the
    per-kind binaries/build dirs across invocations where safe.
  - **Keep it the single source of truth for "run all the tools":** when a future
    row is added or a re-run command changes, update the script too. No port code
    site, so this item is untagged.

### The tools

1. **[x] AddressSanitizer — done.** Findings live in the memory-audit item in §C
   (the "ASan run" bullets). Summary: built with `make SANITIZE=1
   BIN=$PWD/love_mrb_harness_asan`; ran all 23 `*_test.rb` under
   `DISPLAY=:1 ASAN_OPTIONS=detect_leaks=0`. **No UAF / double-free /
   heap-overflow in the binding layer.** One real (upstream) bug found + fixed:
   `graphics::Mesh::~Mesh()` `new[]`/`delete` mismatch → `delete[]`. Re-run
   command for regression: same as build above.

2. **[x] LeakSanitizer — done.** Findings in the memory-audit item in §C (the
   "LeakSanitizer run" bullet). Summary: same ASan binary, run with
   `ASAN_OPTIONS=detect_leaks=1 LSAN_OPTIONS=suppressions=testing/mruby/leak_suppressions.txt`
   (the suppression file mutes the uninstrumented GL/SDL/X11/ALSA driver leaks).
   One binding leak found + fixed (`k_require`'s `std::string` held across a
   longjmping `mrb_raisef`). Suite is leak-clean (0 residual blocks). Note: the
   module singletons are also torn down on quit now (`mrbx_close_state`, §C).

3. **[x] UBSan — done 2026-06-25.** Generalized the `SANITIZE=` knob (step 0,
   see the shared-mechanism note above), built
   `make SANITIZE=undefined BIN=$PWD/love_mrb_harness_ubsan`, and ran all 23
   `*_test.rb` under `DISPLAY=:1 UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=0`.
   **Result: clean — 0 runtime errors across the whole suite, every test still
   passing (rc=0).** Regression command: same build + run loop.
   - **vptr excluded.** First link failed with `undefined reference to typeinfo
     for love::Reference` — `-fsanitize=undefined` pulls in the vptr check, which
     needs RTTI/typeinfo for every instrumented polymorphic class, but only the
     binding layer is instrumented (the archives + mruby are not, by the same
     design ASan uses). vptr can't see across that boundary regardless, so the
     `undefined` knob compiles with `-fno-sanitize=vptr`. All other UBSan checks
     (signed/unsigned overflow, shifts, null/misaligned deref, enum/bool,
     bounds, alignment) are active and clean. vptr-class type confusion is
     better covered by static analysis (row 6) under this partial-instrumentation
     model.
   - **Prime suspects all exercised and clean:** the keyword-arg coercions
     (`mrbx_opt*`/`mrbx_check*`) run on every binding call across the suite; the
     lstrlib `pack`/`unpack` size/alignment/shift math is hit directly by
     `data_test.rb` (negative ints, mixed widths/endianness, `!4` alignment,
     length-prefixed `s2`/`z` strings, and the 1-based offset conversion); the
     1-based↔0-based index conversions are exercised by `mesh_test.rb`,
     `loader_test.rb`, and the `unpack` offset path. None tripped UBSan.
   - **Coverage caveat:** this is the standard 23-test suite, not adversarial
     edge inputs — fuzzing (row 7) over `pack`/`unpack` + decoders is where
     malformed-input-driven overflow/shift UB would surface, and remains pending.
   **Findings:** no UB in the binding layer.

4. **[x] TSan — done 2026-06-25.** Wrote the thread-heavy test
   (`testing/mruby/thread_test.rb`): 8 worker `Love::Thread`s booting their own
   `mrb_state` concurrently while hammering shared named `Channel`s
   (push/demand/pop/peek/get_count/perform_atomic) and churning wrappers. Built
   `make SANITIZE=thread BIN=$PWD/love_mrb_harness_tsan` and ran it under
   `setarch -R` (TSan vs. this kernel's ASLR needs `setarch -R` to disable
   randomization, else it aborts with `unexpected memory mapping`) with
   `TSAN_OPTIONS=halt_on_error=0`. **Two real data races found and fixed; ~590
   subsequent runs (incl. a 24-worker heavy variant) are TSan-clean.**
   - **Race 1 — the global `mrbx_*` registries (the predicted big one).** 261
     race reports, all on the unguarded `std::map`s in `common/mrb_runtime.cpp`
     (`typeClasses`, `objectWrappers`, `userData`, `moduleInstances`,
     `callbacks`), keyed by `mrb_state*` but **shared across every VM**. As N
     thread VMs boot (register types → `typeClasses[k]=…`, mint wrappers →
     `objectWrappers`/`mrbx_pushtype`, track modules), run, and tear down
     (`mrbx_forgetstate`/`mrbx_close_state`), they mutate the same red-black trees
     concurrently. Not just a TSan nag — it **segfaults even uninstrumented**
     (tree corruption). Fix: one `std::recursive_mutex` (`g_runtimeMutex`)
     serializing every access (recursive because the locked entry points nest —
     `mrbx_gettypeclass` self-recurses on parent types, `mrbx_pushtype` →
     `mrbx_gettypeclass`, `mrbx_close_state` → `mrbx_forgetstate`, and
     `mrb_data_object_alloc` can trigger GC → `mrbx_object_free`, all same-thread;
     mruby's GC is per-VM and grabs no other global lock, so no lock-order
     inversion).
   - **Race 2 — `mrb_love_filesystem_init` require-path write.** Every VM boot ran
     `inst->getRequirePath() = {"?.rb","?/init.rb"}` on the **shared** Filesystem
     singleton, so concurrent worker boots wrote the same `std::vector<string>`
     (`wrap_Filesystem_mrb.cpp:1023`). Fix: do it once, inside the
     `inst == nullptr` first-creation branch — also stops a later thread boot from
     clobbering a game's customized `set_require_path`.
   - **Checked-and-clean / not-applicable:** `love::Object` refcounting is already
     `std::atomic<int>` with proper ordering (concurrent module `retain`/`release`
     across VMs is safe); `ThreadModule::getChannel`'s named-channel registry is
     already `namedChannelMutex`-guarded; `Channel`'s own ops are internally
     locked. `love::Type`'s lazy id/`isa`/`init` (non-atomic `nextId++` in
     `common/types.cpp`) **is** racy in principle but **dormant in the port** — the
     mruby bindings type-check via Ruby's `mrb_obj_is_kind_of` and key maps by
     `Type*` identity, and never call `Type::init/getId/isa` (those are only in the
     Lua `runtime.cpp`); flagged here in case a future binding starts using them.
   - **Regression command:** `make SANITIZE=thread BIN=$PWD/love_mrb_harness_tsan`
     then `DISPLAY=:1 TSAN_OPTIONS=halt_on_error=0 setarch -R
     ./love_mrb_harness_tsan thread_test.rb` (expect `ALL PASS`, 0 warnings).
   **Findings:** 2 races (global binding registries; filesystem require-path) —
   both fixed.

5. **[x] Valgrind / memcheck — done 2026-06-25.** Ran all 24 `*_test.rb` under
   Valgrind 3.22 memcheck on the normal `love_mrb_harness` (no rebuild) with
   `DISPLAY=:1 valgrind --leak-check=full --show-leak-kinds=all
   --track-origins=yes
   --suppressions=testing/mruby/valgrind_suppressions.txt`. **Five real bugs found
   and fixed; suite is now memcheck-clean (0 errors, 0 definite/possible leaks).**
   - **Run `--leak-check=full` on _every_ test, not a subset.** The first pass ran
     most tests with `--leak-check=no` (reasoning: LSan/row 2 already covered
     leaks). That was wrong — valgrind catches leaks LSan can't (timing-dependent
     ones, see bugs 2 + 5), and `--leak-check=full` only adds an exit-time heap
     scan (the instrumentation cost is identical), so it buys coverage for almost
     no time. The full-leak sweep found three more bugs (3–5) the subset missed.
     Also pass `--show-leak-kinds=all`: the C++/longjmp leak (bug 5) shows as
     *possibly* lost, not definite.
   - **Suppression file added** (`testing/mruby/valgrind_suppressions.txt`, the
     GL/SDL-driver analogue of `leak_suppressions.txt`): the NVIDIA GL driver
     does `realloc(.,0)`/`posix_memalign(.,.,0)` at `dlopen`/`_dl_init` time
     (ReallocZero/BadSize), and `libdbus` leaks a buffer + child allocations in
     `dbus_bus_register` (session-bus setup pulled in via SDL). All suppressions
     are scoped to the offending **driver/library object files**
     (`*libnvidia-glcore.so*`, `*libdbus-1.so*`), so they can never mask a defect
     in LÖVE or the bindings — LÖVE code never runs inside those libs. (The dbus
     entry covers definite/indirect/possible since timing reclassifies it.)
   - **Bug 1 — uninitialised read in box2d `b2DistanceJoint` (the kind of find
     this row exists for: an uninstrumented archive, invisible to ASan).** Its
     constructor zeroed the impulses but **not `m_u`** (the joint-axis unit
     vector, otherwise only computed in `InitVelocityConstraints` during a world
     step). `GetReactionForce()` reads `inv_dt * (m_impulse + m_lowerImpulse -
     m_upperImpulse) * m_u`, so `joint:get_reaction_force` on a joint that has
     never been in a world step reads uninitialised memory — reachable from the
     public API and hit by `physics_test.rb:212` (4 "Conditional jump depends on
     uninitialised value" reports, origin `b2BlockAllocator::Allocate` →
     `DistanceJoint`). Fix: `m_u.SetZero()` in the ctor
     (`src/libraries/box2d/dynamics/b2_distance_joint.cpp`), matching how the
     impulses are already zeroed → deterministic `(0,0)` pre-step. 4 errors → 0.
   - **Bug 2 — `Message` leak in `event::Event::~Event()`.** The destructor never
     drained `queue`, so any Messages still queued at module teardown leaked
     (refcount never hit 0 when the deque was destroyed) — a `clear()` method
     already existed but wasn't called. Surfaced by `textbatch_test.rb` under
     `--leak-check=full`: window focus/exposed/resize events were `Event.pump`'d
     but never `poll`'d, leaving 6 `convertWindowEvent` Messages in the queue
     (`src/modules/event/sdl/Event.cpp`). Why ASan/LSan (row 2) missed it: the
     leak only bites once (a) the port tears down module singletons on quit
     (`mrbx_close_state`, §C) so `~Event()` actually runs, and (b) the run is slow
     enough (Valgrind ≈50×) for the WM to deliver those async window events during
     `pump`. Fix: `~Event()` now calls `clear()`
     (`src/modules/event/Event.cpp`). 6 definite-loss records → 0; verified
     general (re-ran `event_test.rb` full-leak-clean too).
   - **Bug 3 — `SDL_GetJoysticks` array leak in `JoystickModule::checkGamepads`.**
     SDL3's `SDL_GetJoysticks()` returns a malloc'd array the caller must
     `SDL_free`; `checkGamepads` (`src/modules/joystick/sdl/JoystickModule.cpp`)
     never did (other call sites in the same file do). Hit by `input_test.rb`
     via `set_gamepad_mapping`. Fix: `SDL_free(sdlsticks)` at function end.
   - **Bug 4 — `SDL_GetDisplays` array leak in `Mouse::getGlobalPosition`.** Same
     SDL3 own-the-returned-array contract: `getGlobalPosition`
     (`src/modules/mouse/sdl/Mouse.cpp`) leaked the `SDL_GetDisplays()` array.
     Hit by `mouse_test.rb` via `get_global_position`. Fix: `SDL_free(displays)`
     after the display-walk loop.
   - **Bug 5 — every translated C++ exception leaked (systemic, the binding-wide
     one).** `mrbx_catchexcept` (`src/common/mrb_runtime.h`) caught a C++
     `std::exception` and called `mrb_raise` **inside the catch block**.
     `mrb_raise` longjmps, so it jumps out of the handler without running
     `__cxa_end_catch` → the in-flight C++ exception object (~168 B) and its
     `love::Exception` message (~73 B) are never freed, and each one accumulates
     on the thread's `caughtExceptions` list (so valgrind reports *possibly*
     lost, via the interior eh-globals pointer). The original code comment
     asserted the opposite ("mruby's exception model lets us raise directly
     rather than relying on longjmp semantics") — that misconception was the bug.
     Surfaced by `spritebatch_test.rb` (`add_layer` on a non-layered batch is the
     one suite that drives a real `love::Exception` through the translator; most
     error tests raise mruby-native errors directly and never throw C++). Fix:
     copy the message into `thread_local` storage, let the catch unwind cleanly,
     then `mrb_raise` *after* the catch block. Affects the whole binding layer;
     full suite still 24/24 functional, exception-raising tests still pass.
   - **Everything else clean.** Remaining `*_test.rb`: 0 errors, 0 definite/possible
     leaks. The pack/unpack engine (`data_test.rb`), require resolver
     (`loader_test.rb`), and the GL/glslang/freetype/decoder/openal paths all came
     back uninitialised-read- and leak-clean. (The only residual LEAK-SUMMARY noise
     is `indirectly lost` dbus children under the suppressed dbus parent — library
     noise, not counted as errors.)
   - **Regression command:** `DISPLAY=:1 valgrind --leak-check=full
     --show-leak-kinds=all --track-origins=yes
     --errors-for-leak-kinds=definite,possible
     --suppressions=testing/mruby/valgrind_suppressions.txt
     testing/mruby/love_mrb_harness testing/mruby/<test>.rb` (expect ERROR SUMMARY
     0; definitely + possibly lost 0).
   **Findings:** 5 bugs — box2d uninitialised `m_u`; `~Event()` queue leak;
   `SDL_GetJoysticks`/`SDL_GetDisplays` array leaks; and `mrbx_catchexcept`
   raising via longjmp inside a C++ catch (systemic exception-object leak) — all
   fixed.

6. **[x] Static analysis — done 2026-06-25.** Ran three analyzers at full
   coverage over the **port's own 161 directly-compiled TUs** (`$(SRCS)`: all
   `wrap_*_mrb.cpp` + `mrb_runtime.cpp` + the native module/`common` impls — the
   same scope the `SANITIZE` knob instruments; the prebuilt third-party archives
   gfx/glslang/box2d/physfs/lz4/wuff/xxhash + libmruby are out of scope, as for
   the sanitizer rows). `scan-build` isn't installed, so the clang analyzer was
   driven directly (`clang++ --analyze`, which *is* scan-build's engine).
   - **Tool 1 — clang static analyzer**, full production checker set
     (`-analyzer-checker=core,cplusplus,deadcode,nullability,security,unix,optin`,
     minus the noisy `optin.performance.Padding`), all 161 TUs.
   - **Tool 2 — cppcheck 2.13** (`--enable=warning,performance,portability`,
     `--platform=unix64`, `-DLOVE_LINUX -DLOVE_LITTLE_ENDIAN`), the 155
     module/common TUs.
   - **Tool 3 — GCC 13 `-fanalyzer`** (`+taint`) over the 22 binding-layer TUs
     (`wrap_*_mrb` + `mrb_runtime` + `LuaThread_mrb`). C++ support is limited in
     GCC 13, so a weaker signal, but clean.

   **Result: no real defect in the port's binding layer.** Consistent with the
   ASan/LSan/UBSan/TSan/Valgrind rows. cppcheck and `-fanalyzer` reported **zero**
   findings in any `wrap_*_mrb.cpp` / `mrb_runtime.cpp`.

   - **Root-caused a build quirk that was masking the analysis (fixed).** The
     first clang pass produced ~17 reports in the wrap files — 15 "Called C++
     object pointer is null" + 2 "Division by zero" — *all* on the path right
     after an `if (!x) mrb_raise(...)` guard. `mrb_raise`/`mrb_raisef`/etc. are
     declared `mrb_noreturn` (they longjmp), so those paths are unreachable. But
     `mruby/common.h` gates `mrb_noreturn` on `__STDC_VERSION__` (a C-only macro)
     then `__GNUC__ && !__STRICT_ANSI__` — and the harness Makefile compiled with
     strict **`-std=c++17`**, which *defines* `__STRICT_ANSI__` and leaves
     `__STDC_VERSION__` undefined, so `mrb_noreturn` expanded to **nothing**.
     Re-running the analyzer with `-std=gnu++17` (noreturn active) collapsed all
     17 to zero, confirming none hid a real bug. **Fix:** the real CMake build
     already uses gnu++17 (`CMAKE_CXX_STANDARD 17` with extensions left ON, the
     CMake default → `-std=gnu++17`), so the harness was the outlier; changed
     `testing/mruby/Makefile` `CXXSTD` from `c++17` to `gnu++17` to match shipped
     behaviour. This also keeps every future §E re-run's signal clean.
   - **`wrap_DataModule_mrb.cpp:918` (`else num = u.n;`) — unreachable dead code,
     not a bug.** Inherited from the Lua `lstrlib` `unpack` port. `union PkFtypes`
     makes `d` and `n` both `double`, and `getdetails` only ever sets a float
     `size` to `sizeof(float)`(4) or `sizeof(double)`(8), so the first two arms
     always catch it; the `else` (for a distinct `lua_Number` width, which here
     equals `double`) is never reached. Left as-is; optional cleanup.
   - **Everything else is upstream LÖVE / bundled third-party, not port code, and
     present in mainline** — out of scope, not fixed: `optin.cplusplus.VirtualCall`
     in `File`/`NativeFile`/`Source`/`Event`/`Joystick`/`Mouse`/`ImageData` ctors
     & dtors (the deliberate base-class pattern); dead stores (`offsetSeconds`,
     `valend`); realloc-without-temp on OOM paths (`PNGHandler`, `SoundData`);
     `Keyboard` enum-cast range; and all `libraries/*` noise (lodepng, tinyexr,
     stb, dr_mp3, box2d, ddsparse).
   - **Re-run command** (full coverage, from `testing/mruby/`): mirror the build
     flags, then for each TU in `$(SRCS)` under `src/modules` + `src/common`:
     `clang++ --analyze -Xclang -analyzer-output=text -std=gnu++17 <CXXFLAGS/INCLUDES>
     -Xclang -analyzer-checker=core,cplusplus,deadcode,nullability,security,unix,optin
     -Xclang -analyzer-disable-checker=optin.performance.Padding <tu> -o /dev/null`.
     (With the Makefile now on gnu++17, the analyzer's flags already match.)
   **Findings:** no binding-layer bugs; fixed the harness `mrb_noreturn` build
   quirk (strict `c++17` → `gnu++17`) that had been suppressing the analyzers'
   ability to see past `mrb_raise` guards.

7. **[x] Fuzzing — done 2026-06-25.** Five libFuzzer harnesses over the
   data-driven parsers, one per target, all built + run under **ASan+UBSan**.
   Harness + build + seeds live in `testing/mruby/fuzz/` (see its README); one
   source `fuzz_targets.cpp` is compiled per target via `-DFZ_<T>`, sharing one
   mruby VM (every `Love::` module opened) and driving the target through the real
   Ruby binding (`__fuzz(bytes)`, rescuing expected errors so only a memory/UB
   fault stops the run). Built with `make fuzz` (clang — libFuzzer is clang-only);
   the port's own TUs (incl. the lodepng/stb/dr_* decoder cpps) get
   SanitizerCoverage+ASan+UBSan, so the parsers are coverage-guided and fully
   checked, while the prebuilt archives + libmruby/freetype stay uninstrumented
   (the same partial-instrumentation model the `SANITIZE` knob uses; heap faults
   inside them are still caught via the global malloc/new interceptors).
   - **Targets + coverage reached (full-length runs, ASan+UBSan):** `fuzz_data`
     (`pack`/`unpack`/`get_packed_size`, the native lstrlib engine) 428k execs;
     `fuzz_loader` (require-path resolver + `load`) 524k execs; `fuzz_image`
     (`new_image_data`/`new_compressed_data`) cov ≈1.7k edges into lodepng/stb/
     dds; `fuzz_sound` (`new_decoder`/`new_sound_data`) cov ≈1.7k into the
     wave/flac/mp3/vorbis/modplug decoders; `fuzz_font` (freetype/BMFont
     rasterizers) 136k execs. Seeded from real PNG/OGG/TTF fixtures +
     valid `pack` format strings (`fuzz/seeds/`).
   - **Bug — signed-int overflow computing decoded-image byte size (UBSan, fixed).**
     `fuzz_image` found `STBHandler.cpp:96` `img.size = img.width * img.height * 4`
     overflowing `int` (e.g. 52736×59676) — the multiply is done in `int` and only
     *then* widened to the `size_t img.size`, so a crafted image makes `size` wrap
     to a wrong (small/negative→huge) value while the real pixel buffer is a
     different size: a heap-overflow primitive downstream, reachable straight from
     `Love::Image.new_image_data`. It's the **same pattern in four magpie sites**,
     so all were fixed by widening the first operand to `size_t` before the
     multiply: `STBHandler.cpp` (the 8-bit line found + the HDR line above it),
     `image/magpie/EXRHandler.cpp` (`new T[width*height*4]` — the *allocation*
     size, so an under-allocation→OOB-write), `image/ImageData.cpp::getSize`
     (`size_t(getWidth()*getHeight())` — the cast applied *after* the overflowing
     `int` multiply), and `image/magpie/PNGHandler.cpp` (here `width`/`height` are
     `unsigned`, so a defined wrap rather than UB, but still a wrong size — widened
     for correctness). All four are shared upstream LÖVE code (no `LOVE_MRUBY`
     guard), so the fix helps the Lua build too. The former crash input now
     decodes/errors cleanly; suite still 24/24. Reproducer:
     `fuzz/findings/image_int_overflow_size.bin`.
   - **Noted (not a port defect) — single-input allocation amplification.** A tiny
     input can drive a multi-hundred-MB/GB allocation: stb's TGA loader sizes the
     pixel buffer straight from header dimensions
     (`STBHandler::decode` → `stbi_load_from_memory`, no dimension cap), and
     `pack`'s `cN` fixed-size field allocates N bytes from the format string. Both
     are faithful to upstream LÖVE / Lua 5.3 `string.pack` (present in mainline,
     unguarded by policy — stb exposes `STBI_MAX_DIMENSIONS`, LÖVE doesn't set it),
     so they are documented as a malformed-asset DoS surface, not fixed here (an
     upstream policy change affecting both builds). Reproducers:
     `fuzz/findings/image_stb_tga_bomb.bin`, `data_pack_large_cN.bin`.
   - **Harness note.** An early run reported *cumulative* OOMs on `fuzz_data`/
     `fuzz_font`; root-caused to the harness GC cadence (a full GC only every 1024
     inputs let large transient allocations pile up between sweeps), not a leak or
     a port bug — fixed by sweeping after every input, which also makes any *real*
     single-input amplification (above) the only OOM libFuzzer can now report.
     Leak detection was kept **off** during fuzzing on purpose (rows 2/5 own
     leaks; libFuzzer's per-run check is unreliable against a persistent GC VM).
   - **Re-run:** `make fuzz`, then per the `fuzz/README.md` run block (seed a
     `corpus_<t>/` from `seeds/<t>/` and run each `fuzz_<t>` under
     `ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1`). Regression-check
     the fix by replaying `fuzz/findings/image_int_overflow_size.bin` (expect no
     UBSan report).
   **Findings:** 1 real bug — signed-int overflow miscomputing decoded-image size,
   fixed across all four magpie handlers that share the pattern; plus two
   documented upstream allocation-amplification (DoS) surfaces. The `pack`/`unpack`
   engine, require resolver, and sound/font decoders fuzzed memory-safety- and
   UB-clean.

8. **[x] Run the real `love` (CMake) under the sanitizers — done 2026-06-25.**
   Not just the Makefile harness: the actual `love` exe + `liblove.so`, driven
   through the full boot→shutdown pipeline against real games, under all three
   sanitizers the harness rows used. **Result: the port's own code is clean under
   ASan, UBSan, and TSan; the only findings are two data races wholly inside the
   OpenAL/PipeWire audio backend (external, suppressed).**
   - **Wired a sanitizer knob into the CMake build (the Makefile knob doesn't
     reach the real exe).** `cmake/LoveMruby.cmake` now takes
     `-DLOVE_MRB_SANITIZE=address|undefined|thread`, which instruments only the
     port's OWN targets — `love_mrb_objs` (every common/module/wrap TU) +
     `mrbh_gfx` + `liblove` + the thin `love` exe — and threads `-fsanitize=` into
     both compile and link. The prebuilt archives (box2d/glslang/physfs/lz4/wuff/
     xxhash) and libmruby stay uninstrumented, exactly the harness's
     partial-instrumentation model (heap/race errors in them are still caught via
     the global malloc/new/pthread interceptors). `undefined` also passes
     `-fno-sanitize=vptr` for the same reason row 3 does (no typeinfo across the
     uninstrumented boundary). Build each kind to its own dir
     (`build-mrb-{address,undefined,thread}`, gitignored via `/build-mrb*/`).
   - **Scenarios (both real games, run under each sanitizer):** `game.rb` — full
     boot (conf → window → OpenGL → audio/openal → keyboard/mouse/event/timer),
     600 update/draw frames, then `Event.quit`; and a new restart probe
     (`testing/mruby/restart_probe.rb`, scratch-only) that calls `Love::Event.restart`
     (`testing/mruby/restart_probe.rb`) so the C host runs a complete teardown
     (`mrbx_close_state`) **and re-init of a fresh `mrb_state` in the same
     process** before quitting — the shutdown/restart ordering surface this row
     exists for. (No on-disk game folder needed; the exe takes a `.rb` path
     directly.)
   - **ASan — clean.** `LD_LIBRARY_PATH=build-mrb-address:… DISPLAY=:1
     ASAN_OPTIONS=detect_leaks=1:halt_on_error=0
     LSAN_OPTIONS=suppressions=testing/mruby/leak_suppressions.txt`. Both games
     exit 0 with **0 ASan errors and 0 non-driver leaks** — the only leak-summary
     entries are the suppressed `libnvidia-glcore`/`libSDL3`/`libopenal` driver
     allocations (same external set as row 2). Note the real exe tears down module
     singletons on quit, so even the GL/SDL leaks the harness saw mostly don't
     appear here. The restart cycle (teardown + fresh re-init) is UAF/leak-clean.
   - **UBSan — clean.** Built `-DLOVE_MRB_SANITIZE=undefined`,
     `UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=0`. **0 runtime errors** across
     both games incl. the restart cycle (signed/unsigned overflow, shifts,
     null/misaligned deref, enum/bool, bounds — vptr excluded as above).
   - **TSan — the port is race-clean; 2 external audio-backend races found +
     suppressed.** Built `-DLOVE_MRB_SANITIZE=thread`, run under `setarch -R`
     (TSan vs ASLR, same as row 4) with `TSAN_OPTIONS=halt_on_error=0`. The first
     pass reported 2 data races in `game.rb`, **both entirely inside
     libopenal/libpipewire/libstdc++**: opening the audio device
     (`mrb_love_audio_init` → `alcOpenDevice`, our only frame) makes OpenAL spawn a
     PipeWire `PWEventThread`, and at device close/exit libopenal frees a device
     object on the main thread that the PipeWire thread also wrote, without holding
     the shared lock. **No LÖVE or binding frame participates in either racing
     access** — it is the audio driver's own teardown, reachable only because we
     open a device. Treated like the GL/dbus leak noise: added
     `testing/mruby/tsan_suppressions.txt` (`race:libopenal.so`,
     `race:libpipewire`, scoped to those uninstrumented libs). With it, 6 re-runs
     (3× `game.rb` + 3× restart probe) report **0 unsuppressed warnings**, exit 0
     (`print_suppressions=1` confirms `2 race:libopenal.so` matched). The binding
     registries / filesystem require-path races from row 4 don't recur here (this
     is single-VM, but their fixes are in).
   - **Re-run commands** (from repo root; build each dir once with the matching
     `-DLOVE_MRB_SANITIZE=`):
     `cmake -S . -B build-mrb-<kind> -DLOVE_MRUBY=ON -DLOVE_MRB_SANITIZE=<kind> && cmake --build build-mrb-<kind> -j$(nproc)`,
     then `DISPLAY=:1 LD_LIBRARY_PATH="$PWD/build-mrb-<kind>:/usr/local/lib"
     [setarch -R, for thread] ./build-mrb-<kind>/love testing/mruby/game.rb` with
     the per-kind options above (ASan adds `leak_suppressions.txt`; TSan adds
     `tsan_suppressions.txt`).
   **Findings:** no bug in the port — ASan/UBSan/TSan all clean on the real exe
   across boot, a full restart (teardown + re-init), and quit. The only races are
   two in the external OpenAL/PipeWire audio backend's own teardown (no port frame
   involved), documented + suppressed in `tsan_suppressions.txt`.
