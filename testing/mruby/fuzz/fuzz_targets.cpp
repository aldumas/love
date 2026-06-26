/**
 * libFuzzer harnesses for the mruby-port data-driven entry points (§E row 7 of
 * testing/mruby/PORTING.md). One source, compiled once per target via a
 * -DFZ_<TARGET> macro, so each fuzz binary shares the same VM bring-up and only
 * differs in the Ruby driver it runs per input.
 *
 * Targets (each its own binary, run under ASan+UBSan):
 *   FZ_DATA    love.data pack/unpack/get_packed_size  (the lstrlib format engine)
 *   FZ_LOADER  love.filesystem require-path resolver + load (compile-to-Proc)
 *   FZ_IMAGE   love.image  new_image_data / new_compressed_data decoders
 *   FZ_SOUND   love.sound  new_decoder / new_sound_data decoders
 *   FZ_FONT    love.font   freetype/BMFont rasterizer constructors
 *
 * Mechanism: open one mruby VM with every Love:: module (open_love, copied from
 * harness.cpp), define a Ruby method `__fuzz(bytes)` that drives the target, and
 * per input set it as a String and call `__fuzz`. The VM is reused across inputs
 * (libFuzzer is in-process); each iteration saves/restores the GC arena and a
 * periodic full GC bounds memory. Every call site rescues exceptions in Ruby and
 * clears mrb->exc in C++, so only a *crash* (ASan/UBSan abort or real SIGSEGV)
 * stops the fuzzer — which is exactly the signal we want.
 */

#include "common/mrb_runtime.h"

extern "C" {
	#include <mruby/compile.h>
	#include <mruby/string.h>
	#include <mruby/variable.h>
}

#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace love { namespace timer      { extern "C" void mrb_love_timer_init(mrb_state *mrb); } }
namespace love { namespace math       { extern "C" void mrb_love_math_init(mrb_state *mrb);  } }
namespace love { namespace filesystem { extern "C" void mrb_love_filesystem_init(mrb_state *mrb); } }
namespace love { namespace event      { extern "C" void mrb_love_event_init(mrb_state *mrb); } }
namespace love { namespace window     { extern "C" void mrb_love_window_init(mrb_state *mrb); } }
namespace love { namespace graphics   { extern "C" void mrb_love_graphics_init(mrb_state *mrb); } }
namespace love { namespace keyboard   { extern "C" void mrb_love_keyboard_init(mrb_state *mrb); } }
namespace love { namespace mouse      { extern "C" void mrb_love_mouse_init(mrb_state *mrb); } }
namespace love { namespace system     { extern "C" void mrb_love_system_init(mrb_state *mrb); } }
namespace love { namespace data       { extern "C" void mrb_love_data_init(mrb_state *mrb); } }
namespace love { namespace image      { extern "C" void mrb_love_image_init(mrb_state *mrb); } }
namespace love { namespace font       { extern "C" void mrb_love_font_init(mrb_state *mrb); } }
namespace love { namespace thread     { extern "C" void mrb_love_thread_init(mrb_state *mrb); } }
namespace love { namespace sound      { extern "C" void mrb_love_sound_init(mrb_state *mrb); } }
namespace love { namespace audio      { extern "C" void mrb_love_audio_init(mrb_state *mrb); } }
namespace love { namespace touch      { extern "C" void mrb_love_touch_init(mrb_state *mrb); } }
namespace love { namespace sensor     { extern "C" void mrb_love_sensor_init(mrb_state *mrb); } }
namespace love { namespace joystick   { extern "C" void mrb_love_joystick_init(mrb_state *mrb); } }
namespace love { namespace video      { extern "C" void mrb_love_video_init(mrb_state *mrb); } }
namespace love { namespace physics { namespace box2d { extern "C" void mrb_love_physics_init(mrb_state *mrb); } } }

namespace love { namespace thread {
typedef void (*ThreadVMOpener)(struct mrb_state *);
extern ThreadVMOpener g_threadVMOpener;
} }

static std::string g_arg0 = "love_fuzz";

static void open_love(mrb_state *mrb)
{
	struct RClass *love = mrb_define_module(mrb, "Love");
	mrb_define_const(mrb, love, "ARG0", mrb_str_new_cstr(mrb, g_arg0.c_str()));

	love::timer::mrb_love_timer_init(mrb);
	love::math::mrb_love_math_init(mrb);
	love::filesystem::mrb_love_filesystem_init(mrb);
	love::event::mrb_love_event_init(mrb);
	love::window::mrb_love_window_init(mrb);
	love::graphics::mrb_love_graphics_init(mrb);
	love::video::mrb_love_video_init(mrb);
	love::keyboard::mrb_love_keyboard_init(mrb);
	love::mouse::mrb_love_mouse_init(mrb);
	love::system::mrb_love_system_init(mrb);
	love::data::mrb_love_data_init(mrb);
	love::image::mrb_love_image_init(mrb);
	love::font::mrb_love_font_init(mrb);
	love::thread::mrb_love_thread_init(mrb);
	love::sound::mrb_love_sound_init(mrb);
	love::audio::mrb_love_audio_init(mrb);
	love::touch::mrb_love_touch_init(mrb);
	love::sensor::mrb_love_sensor_init(mrb);
	love::joystick::mrb_love_joystick_init(mrb);
	love::physics::box2d::mrb_love_physics_init(mrb);
}

// ---- Per-target Ruby drivers -------------------------------------------------
//
// SETUP_RB runs once (any one-time init: filesystem mount, etc.). FUZZ_RB
// defines `__fuzz(b)` where `b` is the fuzz input as a binary String. Every
// outward call is wrapped in `begin/rescue Exception` so an expected
// malformed-input error never reaches libFuzzer; only a memory/UB fault does.

#if defined(FZ_DATA)
static const char *SETUP_RB = "";
static const char *FUZZ_RB =
	"def __fuzz(b)\n"
	"  n = b.empty? ? 0 : (b.getbyte(0) % (b.length + 1))\n"
	"  fmt = b.byteslice(1, n) || \"\"\n"
	"  rest = b.byteslice(1 + n, b.length) || \"\"\n"
	"  begin; Love::Data.get_packed_size(format: fmt); rescue Exception; end\n"
	"  begin; Love::Data.unpack(format: fmt, string: rest); rescue Exception; end\n"
	"  begin; Love::Data.unpack(format: fmt, string: b); rescue Exception; end\n"
	"  vals = [0, 1, -1, 127, -128, 255, 65535, 1.5, -2.5, \"a\", \"bb\", \"ccc\", 0, 0, 0, 0]\n"
	"  begin; Love::Data.pack(format: fmt, values: vals); rescue Exception; end\n"
	"  begin; Love::Data.pack(format: fmt, values: vals, container: \"data\"); rescue Exception; end\n"
	"end\n";

#elif defined(FZ_LOADER)
static const char *SETUP_RB =
	"Love::Filesystem.init(arg0: \"love\")\n"
	"Love::Filesystem.set_identity(name: \"love_fuzz_loader\")\n";
static const char *FUZZ_RB =
	"def __fuzz(b)\n"
	"  i = b.index(\"\\n\") || (b.length / 2)\n"
	"  pat = b.byteslice(0, i) || \"\"\n"
	"  name = b.byteslice(i + 1, b.length) || \"\"\n"
	"  begin; Love::Filesystem.set_require_path(paths: [pat, pat + \"?.rb\", \"?.rb\"]); rescue Exception; end\n"
	"  begin; Love::Filesystem.load(name: name); rescue Exception; end\n"
	"  begin; Love::Filesystem.load(name: b); rescue Exception; end\n"
	"  begin; Love::Filesystem.get_require_path; rescue Exception; end\n"
	"end\n";

#elif defined(FZ_IMAGE)
static const char *SETUP_RB = "";
static const char *FUZZ_RB =
	"def __fuzz(b)\n"
	"  fd = Love::Filesystem.new_file_data(contents: b, name: \"fuzz.bin\")\n"
	"  begin; Love::Image.new_image_data(file: fd); rescue Exception; end\n"
	"  begin; Love::Image.new_compressed_data(file: fd); rescue Exception; end\n"
	"  begin; Love::Image.compressed?(file: fd); rescue Exception; end\n"
	"end\n";

#elif defined(FZ_SOUND)
static const char *SETUP_RB = "";
static const char *FUZZ_RB =
	"def __fuzz(b)\n"
	"  fd = Love::Filesystem.new_file_data(contents: b, name: \"fuzz.bin\")\n"
	"  begin\n"
	"    dec = Love::Sound.new_decoder(file: fd, stream_source: \"memory\")\n"
	"    8.times { break unless dec.decode } if dec\n"
	"  rescue Exception; end\n"
	"  begin; Love::Sound.new_sound_data(file: fd); rescue Exception; end\n"
	"end\n";

#elif defined(FZ_FONT)
static const char *SETUP_RB = "";
static const char *FUZZ_RB =
	"def __fuzz(b)\n"
	"  fd = Love::Filesystem.new_file_data(contents: b, name: \"fuzz.ttf\")\n"
	"  begin; Love::Font.new_true_type_rasterizer(file: fd, size: 12); rescue Exception; end\n"
	"  begin; Love::Font.new_bm_font_rasterizer(file: fd); rescue Exception; end\n"
	"  begin; Love::Font.new_rasterizer(file: fd); rescue Exception; end\n"
	"end\n";

#else
#error "define one of FZ_DATA / FZ_LOADER / FZ_IMAGE / FZ_SOUND / FZ_FONT"
#endif

// ---- libFuzzer plumbing ------------------------------------------------------

static mrb_state *g_mrb = nullptr;
static mrb_sym g_fuzz_sym = 0;
static unsigned long g_iter = 0;

static void die_if_exc(const char *phase)
{
	if (g_mrb->exc)
	{
		fprintf(stderr, "fuzz setup error in %s:\n", phase);
		mrb_print_error(g_mrb);
		abort();
	}
}

extern "C" int LLVMFuzzerInitialize(int *argc, char ***argv)
{
	(void) argc; (void) argv;

	g_mrb = mrb_open();
	if (g_mrb == nullptr)
	{
		fprintf(stderr, "failed to open mruby state\n");
		abort();
	}

	open_love(g_mrb);
	love::thread::g_threadVMOpener = open_love;
	die_if_exc("open_love");

	if (SETUP_RB[0] != '\0')
	{
		mrb_load_string(g_mrb, SETUP_RB);
		die_if_exc("SETUP_RB");
	}

	mrb_load_string(g_mrb, FUZZ_RB);
	die_if_exc("FUZZ_RB");

	g_fuzz_sym = mrb_intern_lit(g_mrb, "__fuzz");
	return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	mrb_state *mrb = g_mrb;
	int ai = mrb_gc_arena_save(mrb);

	mrb_value s = mrb_str_new(mrb, (const char *) data, size);
	mrb_funcall_argv(mrb, mrb_top_self(mrb), g_fuzz_sym, 1, &s);

	// The driver rescues every expected error; anything still pending is a
	// surprise (e.g. a NoMemoryError from a real runaway alloc) — clear it so
	// the run continues, but it has already been observed by the sanitizers.
	mrb->exc = nullptr;

	mrb_gc_arena_restore(mrb, ai);

	// Full GC every iteration. A single input can drive a large transient
	// allocation (an image's pixel buffer, a `pack` cN field, a font table), and
	// mruby's incremental GC can lag behind a tight fuzzing loop — so without a
	// forced sweep the unreferenced-but-not-yet-collected set balloons and
	// libFuzzer reports a *cumulative* OOM that is a harness artifact, not a
	// real per-input bomb. Sweeping every input keeps RSS flat so the only OOM
	// libFuzzer can report is a genuine single-input amplification. The sweep is
	// cheap next to the decode/parse work it follows.
	(void) g_iter;
	mrb_full_gc(mrb);

	return 0;
}
