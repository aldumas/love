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
| Ported module: event (queue + full SDL event translation: kbd/mouse/touch/joystick/gamepad/sensor/window/drop) | `src/modules/event/wrap_Event_mrb.cpp` |
| Ported module: window (real `window::sdl::Window` backend; creates the GL context, drives the graphics backbuffer) | `src/modules/window/wrap_Window_mrb.cpp` |
| Ported module: graphics (real shader-based batched renderer, OpenGL backend; clear/color/rectangle/present + transform stack + render state (blend/scissor/color_mask/line/point/wireframe) + shaders (new_shader/set_shader + Shader type) + canvas/render targets (new_canvas/set_canvas) + stencil/depth state + SpriteBatch + TextBatch + ParticleSystem + Mesh + Video + new_image/new_quad/draw + new_font/print/printf; Texture, Quad, Font, Shader, SpriteBatch, TextBatch, ParticleSystem, Mesh & Video object types) | `src/modules/graphics/wrap_Graphics_mrb.cpp` |
| Ported module: keyboard (real keyboard::sdl::Keyboard backend; canonical key/scancode enum names) | `src/modules/keyboard/wrap_Keyboard_mrb.cpp` |
| Ported module: mouse (real `mouse::sdl::Mouse` backend; position/buttons/visibility/grab/relative + cursor objects) | `src/modules/mouse/wrap_Mouse_mrb.cpp` |
| Ported module: system (OS/CPU/memory/clipboard/power/locale; real SDL backend) | `src/modules/system/wrap_System_mrb.cpp` |
| Ported module: data (Data/ByteData/DataView/CompressedData; compress/encode/hash) | `src/modules/data/wrap_DataModule_mrb.cpp` |
| Ported module: image (ImageData + CompressedImageData decode/encode/pixels; real lodepng/stb/exr/dds backend) | `src/modules/image/wrap_Image_mrb.cpp` |
| Ported module: font (Rasterizer + GlyphData; real freetype/harfbuzz backend, embedded default font) | `src/modules/font/wrap_Font_mrb.cpp` |
| Ported module: thread (Thread + Channel; real SDL threads, per-thread mruby VM) | `src/modules/thread/wrap_ThreadModule_mrb.cpp` |
| Ported module: sound (Decoder + SoundData; real lullaby wav/flac/ogg/mp3/mod backend) | `src/modules/sound/wrap_Sound_mrb.cpp` |
| Ported module: audio (Source + RecordingDevice; real OpenAL backend, null fallback) | `src/modules/audio/wrap_Audio_mrb.cpp` |
| Ported module: touch (real SDL backend; ids are 64-bit Integers) | `src/modules/touch/wrap_Touch_mrb.cpp` |
| Ported module: sensor (real SDL backend) | `src/modules/sensor/wrap_Sensor_mrb.cpp` |
| Ported module: joystick (Joystick type + module class methods; real SDL gamepad backend) | `src/modules/joystick/wrap_JoystickModule_mrb.cpp` |
| Ported module: video (VideoStream type; real theora decode backend, surfaced via love.graphics.new_video) | `src/modules/video/wrap_Video_mrb.cpp` |
| Ported module: physics (first slice — World/Body/Circle/Polygon/Edge/Chain shapes; real Box2D backend) | `src/modules/physics/box2d/wrap_Physics_mrb.cpp` |
| Ported boot scripts (arg/callbacks/boot) | `src/modules/love/{arg,callbacks,boot}.rb` |
| Standalone demo harness | `testing/mruby/harness.cpp` |
| nanosleep/deprecation stubs (avoid linking SDL for the demo) | `testing/mruby/delay_stub.cpp` |
| Build (`make`, `make run`, `make mruby`) | `testing/mruby/Makefile` |

The full `love` executable can't link until all 74 module wrappers are ported,
so this harness exercises the ported modules (`timer`, `math`, `filesystem`,
`event`, `window`, `graphics`, `keyboard`, `mouse`, `system`, `data`, `image`,
`font`, `thread`, `sound`, `audio`, `touch`, `sensor`, `joystick`, `video`,
`physics`) end-to-end.

The physics module ports the love.physics (box2d) simulation core: create a
world, add bodies with circle/rectangle/polygon/edge/chain shapes, step the
simulation and read back transforms (run `make run SCRIPT=physics_test.rb`). The
per-shape geometry queries are in too — `ray_cast` / `compute_aabb` /
`compute_mass` / `get_bounding_box` / `get_mass_data`, polygon/edge `get_points`
vertex readback, and `Physics.get_distance`. It is also the first engine code
with VM calls embedded in the engine class (World's collision callbacks, the
`lua_State`-taking helpers on World/Body/Shape); those Lua-only sections are
guarded with `#ifndef LOVE_MRUBY` and tracked in PORTING.md. The World-level
result-returning spatial queries are in too — `get_shapes_in_area` (AABB query)
and `ray_cast_any` / `ray_cast_closest`. All 11 joint types are ported as well:
the `Physics.new_*_joint` factories (keyword arguments), the per-joint
setters/getters and multi-return getters as Hashes (`get_anchors`, `get_limits`,
`get_axis`, `get_target`, `get_ground_anchors`, `get_linear_offset`), and
`World`/`Body` `get_joints`. The `Contact` type is in too — `World`/`Body`
`get_contacts`, `get_positions` / `get_normal` / `get_shapes` / `get_children`,
and the friction/restitution/enabled/tangent-speed accessors; contact wrappers
keep their identity across calls via the World object memoizer. Arbitrary
**user data** is ported too — `Body`/`Shape`/`Joint` `set_user_data` /
`get_user_data` store one arbitrary Ruby value per engine object (GC-protected,
keyed by the C++ object so it round-trips through a re-fetched wrapper or a
callback). **Wrapper identity** is ported engine-wide too: `mrbx_pushtype` keeps
a weak per-VM registry so every Ruby handle to one engine object is the same
object (`==`), the mruby equivalent of LÖVE's Lua weak-valued userdata table
(no leak — the wrapper's free callback evicts the entry on collection).
Collision callbacks and the **user-callback** query/ray-cast variants (which
need an mruby callback-reference mechanism) are deferred to later slices — see
PORTING.md §A for the full breakdown.
The filesystem module links the
bundled physfs library (compiled as C) and SDL3 (`/usr/local/lib`), so the
harness depends on `libSDL3` (also used by the event and window backends); the
graphics module is the **real** shader-based batched renderer (OpenGL backend),
built as static archives — `libgfx.a` (`graphics/*.cpp` + `graphics/opengl/*` +
glad GL loader + the `video::VideoStream` base), `libglslang.a` (the bundled
glslang for shader validation/reflection), and `libxxhash.a` — and links `libGL`;
the real `window::sdl::Window` backend creates the GL context and drives the
graphics backbuffer. Vulkan/Metal are intentionally out of the build
(`LOVE_MRUBY_NO_VULKAN`). The
font module links the system `freetype` and `harfbuzz` (via `pkg-config`), the
sound module links the bundled Wuff (WAV, compiled as C) plus the system
`libvorbis`/`libmodplug` for its lullaby decode backend (FLAC and MP3 use the
bundled header-only dr_flac/dr_mp3), and the audio module links the system
`libopenal` for its real OpenAL backend (with the null backend kept as a
fallback when no audio device is available). The input family (`touch`,
`sensor`, `joystick`) uses the real SDL backends (no extra libraries beyond
`libSDL3`); with no devices attached the lists are empty, but the global
gamepad-mapping database is exercised. The video module links the system
`libtheoradec` (with `libogg`, already linked for sound) for its real theora
decode backend; `love.graphics.new_video` decodes an `.ogv` on a worker thread.

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

The Ruby class hierarchy mirrors the `love::Type` hierarchy: a type's Ruby
superclass is its parent type's Ruby class (bottoming out at `love::Object`,
which maps to Ruby's `Object`). So `Love::ByteData < Love::Data`, a
`ByteData.is_a?(Love::Data)` is true, and base-class methods (e.g. the `Data`
instance methods) are registered once on the base and inherited by every
subtype. `mrbx_checktype<Base>` therefore accepts any registered subtype. The
`data` module's functions are a special case: because the module name "Data"
would collide with the `Data` type's class, they are registered as **class
methods** on `Love::Data` (call syntax is unchanged, e.g. `Love::Data.compress`).

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
coroutine; modules unavailable at runtime — e.g. `window`/`graphics` on a
headless machine — are detected with `const_defined?` (and, for graphics,
`Love::Graphics.active?`) and their branches skipped (so `draw` is called
directly and prints instead of rendering); the game's main file is `eval`'d
(mruby has no file-level `Kernel#load`). The event module is wired up — a
`Love.quit!` stand-in remains only as the fallback when `love.event` is absent.
`Love.init` creates the window from the `t.window` config (via
`Love::Window.set_mode`) when `window` is present, just like `boot.lua`, and the
run loop's draw branch clears/draws/presents through `Love::Graphics` when it's
active — so `--boot game.rb` opens a real window and renders a moving rectangle.

## Remaining work (per-module template established by this slice)

The actionable checklist of everything deferred, stubbed, or temporarily
implemented lives in **`PORTING.md`** (the ledger). Each item with a code site
carries a stable tag (e.g. `#win-icon`) that also appears in a
`// TODO(mruby) #win-icon: …` marker at the deferral site. A pre-commit hook
keeps the two in sync — install it once per clone with `make install-hooks`
(or run the check directly with `make check-ledger`). The section below is the
narrative overview.

1. Port the other 71 `wrap_*.cpp` files to the `wrap_*_mrb.cpp` pattern, one
   module at a time, converting positional params to keyword args. The timer
   slice covers module functions; math covers object types with instance
   methods (`mrbx_register_type` / `mrbx_pushtype`); filesystem covers an
   abstract module with a real backend (physfs), returning Hashes/arrays and
   wrapping File/FileData objects.
   The filesystem module is now fully ported: CommonPath/full-path/Data-based
   mounting, symlinks, fused/android settings, and the loader (require path,
   `load`, and a global Ruby-style `require` over the virtual filesystem). No
   filesystem deferrals remain — see PORTING.md §A.
2. DONE: the Lua boot scripts (`boot.lua`, `callbacks.lua`, `arg.lua`) are
   ported to Ruby (`boot.rb`, `callbacks.rb`, `arg.rb`) with the coroutine boot
   loop mapped onto mruby `Fiber`, and the run loop now pumps/polls the ported
   `love.event`, `Love.init` opens the config window via `love.window`, and the
   run loop clears/draws/presents through `love.graphics` when active. The window
   and graphics backends are now the **real** ones: window is
   `window::sdl::Window` (`window/sdl/Window.cpp`) and graphics is a real
   `graphics::Graphics` from `Graphics::createInstance()` (the OpenGL backend,
   `graphics/opengl`). The window backend creates the GL context and drives the
   graphics backbuffer: `setWindow()` resolves the `M_GRAPHICS` instance via
   `Module::getInstance<graphics::Graphics>` and calls
   `getRenderer()`/`setMode()`/`backbufferChanged()` on it — so window and
   graphics are coupled and come up together, which is why they were swapped in
   the same step. Vulkan/Metal are kept out of the build (`LOVE_MRUBY_NO_VULKAN`)
   so only the OpenGL renderer (and its glad loader + the bundled glslang shader
   compiler) is linked. The event backend is now the **real**
   `event/sdl/Event.cpp` (the lean `HarnessEvent` was swapped out); its
   live-resize modal-draw hook dynamic_casts to the real `window::sdl::Window`
   and re-renders through the real `graphics::Graphics`. See PORTING.md §B
   (`#event-backend` / `#win-backend` / `#gfx-backend`) for the details.
   The window module is fully exposed: `set_icon`/`get_icon`, `update_mode`,
   `get_pointer`, `show_file_dialog`, and the HiDPI transforms
   (`to_pixels`/`from_pixels`/`get_dpi_scale`) are all wired up. `update_mode`
   re-applies the mode with every keyword optional; `get_pointer` returns the
   native handle as a TT_CPTR value; `show_file_dialog` takes a
   `{ |files, filter_name, err| ... }` result block plus the dialog kwargs, now
   hosted by the real SDL window backend; the DPI transforms come from the real
   `window/sdl/Window.cpp` (the pixel/window size ratio plus SDL's display scale,
   honored only when the window set `use_dpi_scale`).
   The graphics API slice currently exposes `clear` / `set_color` /
   `set_background_color` / `rectangle` / `present` / dimensions, the full
   coordinate-system transform stack (`origin` / `push` / `pop` / `translate` /
   `rotate` / `scale` / `shear` / `apply_transform` / `replace_transform` /
   `transform_point` / `inverse_transform_point`), render state (`set_blend_mode`
   / `set_scissor` / `intersect_scissor` / `set_color_mask` / `set_line_width` /
   `set_line_style` / `set_line_join` / `set_point_size` / `set_wireframe` and
   their getters), shaders (`new_shader` / `set_shader` / `get_shader` and the
   `Love::Shader` type: `send` / `send_color` / `has_uniform?` / `get_warnings`),
   canvas / render targets (`new_canvas` / `set_canvas` / `get_canvas`, a canvas
   being a render-target `Love::Texture`; incl. layered/mipmapped targets and the
   slice/mipmap/depthstencil set_canvas variants), stencil/depth render state
   (`set_stencil_mode` / `set_stencil_state` / `set_depth_mode` and their getters), the `Love::SpriteBatch`
   object type (`new_sprite_batch` + add/set/clear/color/draw-range, plus
   add_layer/set_layer over an array texture from `new_array_image`, and
   attach_attribute), the
   `Love::TextBatch` object type (`new_text_batch` + set/setf/add/addf/metrics),
   the `Love::ParticleSystem` object type (`new_particle_system` + the full
   config + lifecycle API), the `Love::Mesh` object type (`new_mesh` +
   vertices/texture/draw-mode/vertex-map, standard **and custom** vertex
   formats, per-attribute access, attached attributes, and index buffers), the
   `Love::Video` object type (`new_video` + play/pause/seek/tell and the
   `Love::VideoStream` type, real theora decode backend), the low-level GPU
   `Love::GraphicsBuffer` type (`new_buffer` + set_array_data/clear/get_format/
   buffer_type?/get_size) and the `Love::GraphicsReadback` type with the
   `readback_buffer` / `readback_texture` module functions and their `_async`
   variants (sync returns ByteData/ImageData; async returns a pollable
   GraphicsReadback),
   plus
   `new_image` / `new_array_image` / `new_volume_image` / `new_cube_image` /
   `new_quad` / `draw`, `new_font` / `set_font` / `get_font` /
   `print` / `printf`, and the `Love::Texture`, `Love::Quad`, and `Love::Font`
   object types (Texture is-a Drawable, with dimensions + `get_texture_type` /
   `get_layer_count` / `get_depth` / `get_mipmap_count` + `set_filter`; `draw`
   takes a Drawable or a Texture+Quad with the full transform; text renders
   through the real glyph atlas). All of it runs through the real batched
   renderer (e.g. a textured `draw` goes through the default shader and the
   streaming vertex buffer). Note `Love::Font` is shared between the love.font
   module (class methods) and the graphics Font type (instances), resolving the
   module-name vs type-name collision the same way data/thread/joystick do.
   Every graphics object type is now exposed on the real instance. `new_video`
   also wires the audio track (best-effort): it builds a streaming `Love::Source`
   from the file and syncs the video to it, like the Lua `newVideo` wrapper
   (PORTING.md `#video-audio`).
   The keyboard module is the **real** `keyboard::sdl::Keyboard`
   (`#kbd-backend`), using LÖVE's canonical key/scancode enum names, and the now-
   real event backend handles `set_key_repeat` natively (no lean key-repeat
   shim).
   The mouse module is now the **real** `love::mouse::sdl::Mouse` backend
   (`mouse/sdl/Mouse.cpp`), driving position, buttons, visibility, grab, and
   relative mode through the abstract `love::mouse::Mouse` interface. Button
   indices keep LÖVE's convention (1 left, 2 right, 3 middle). The cursor object
   family (`new_cursor` / `get_system_cursor` / `set_cursor` / `get_cursor` and
   the `Love::Cursor` type) runs on the real backend's `love::mouse::sdl::Cursor`.
3. Swap the object/proxy system — **done** for object identity: `mrbx_pushtype`
   keeps a weak per-VM registry so the same C++ object always maps to the same
   Ruby object (the mruby equivalent of the Lua weak-table identity map).
4. Wire CMake (`CMakeLists.txt`) to build `libmruby.a` and link it instead of
   `lovedep::Lua`; drop `src/libraries/lua53` and the LuaJIT path.
5. Port the FFI-dependent fast paths (love uses LuaJIT FFI in a few wrappers).
