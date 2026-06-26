# Fuzzing the mruby-port parsers (§E row 7)

libFuzzer harnesses over the data-driven entry points most exposed to malformed
input. See the row-7 writeup in `../PORTING.md` for findings. This directory is
self-contained; the build rules live in `../Makefile`.

## Targets

One source, `fuzz_targets.cpp`, compiled once per target via `-DFZ_<TARGET>`:

| binary        | target code                                              |
|---------------|----------------------------------------------------------|
| `fuzz_data`   | `love.data` `pack`/`unpack`/`get_packed_size` (lstrlib engine) |
| `fuzz_loader` | `love.filesystem` require-path resolver + `load`         |
| `fuzz_image`  | `love.image` `new_image_data` / `new_compressed_data` decoders |
| `fuzz_sound`  | `love.sound` `new_decoder` / `new_sound_data` decoders    |
| `fuzz_font`   | `love.font` freetype / BMFont rasterizer constructors    |

Each opens one mruby VM with every `Love::` module, then per input drives the
target through the real Ruby binding (`__fuzz(bytes)`), rescuing expected errors
so only a memory/UB fault stops the run. The port's own TUs (including the
lodepng/stb/dr_* decoder cpps) are built with SanitizerCoverage + ASan + UBSan,
so the parsers are coverage-guided and fully checked; the prebuilt archives and
libmruby/freetype stay uninstrumented (the same partial-instrumentation model the
`SANITIZE` knob uses — heap faults inside them are still caught via the global
malloc/new interceptors).

## Build

    make fuzz            # builds all five into testing/mruby/fuzz/

Requires clang (libFuzzer is clang-only); `make fuzz` uses clang++ regardless of
`$(CXX)`. The instrumented object set is shared, so the five binaries differ only
by a quick final link.

## Run

The committed `seeds/<target>/` inputs are the starting corpus; copy them into a
working dir (gitignored) and fuzz:

    cd testing/mruby/fuzz
    for t in data loader image sound font; do mkdir -p corpus_$t && cp seeds/$t/* corpus_$t/; done

    # one target, ASan+UBSan, 10 min, capturing any crash:
    DISPLAY=:1 ASAN_OPTIONS=detect_leaks=0:abort_on_error=1 \
      UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1:abort_on_error=1 \
      ./fuzz_image -max_total_time=600 -timeout=30 -rss_limit_mb=6144 \
      -artifact_prefix=crashes/image_ corpus_image

Notes:
- `DISPLAY=:1` — the VM opens every module (incl. graphics/window), which need an
  X display, exactly as the main harness does.
- `detect_leaks=0` — leaks are rows 2 (LSan) and 5 (Valgrind); libFuzzer's
  per-run leak check is unreliable against a persistent GC VM (objects pending a
  sweep look reachable/leaked). This row targets memory-corruption + UB.
- The harness runs a full GC after every input, so RSS stays flat and the only
  out-of-memory libFuzzer can report is a *genuine single-input* allocation
  amplification (see findings), not cumulative growth.

## Findings (reproducers in `findings/`)

- `image_int_overflow_size.bin` — UBSan: signed-int overflow computing a decoded
  image's byte size (`width*height*4` in `int`) in the magpie handlers. **Fixed**
  (widened to `size_t`); this input now decodes/errors cleanly.
- `image_stb_tga_bomb.bin` / `data_pack_large_cN.bin` — single-input allocation
  amplification: a tiny input drives a multi-hundred-MB allocation (stb's TGA
  loader sizing from header dimensions; `pack`'s `cN` fixed-size field). Faithful
  to upstream LÖVE / Lua `string.pack`; documented, not a port regression.
