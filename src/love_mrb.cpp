/**
 * Copyright (c) 2006-2026 LOVE Development Team
 *
 * This software is provided 'as-is', without any express or implied
 * warranty.  In no event will the authors be held liable for any damages
 * arising from the use of this software.
 *
 * Permission is granted to anyone to use this software for any purpose,
 * including commercial applications, and to alter it and redistribute it
 * freely, subject to the following restrictions:
 *
 * 1. The origin of this software must not be misrepresented; you must not
 *    claim that you wrote the original software. If you use this software
 *    in a product, an acknowledgment in the product documentation would be
 *    appreciated but is not required.
 * 2. Altered source versions must be plainly marked as such, and must not be
 *    misrepresented as being the original software.
 * 3. This notice may not be removed or altered from any source distribution.
 **/

// The mruby liblove boot driver -- the production counterpart to
// testing/mruby/harness.cpp, replacing the Lua src/love.cpp's runlove(). Opens
// an mruby state, registers every ported Love:: module, and drives the boot
// pipeline (the embedded arg.rb/callbacks.rb/boot.rb + the $LOVE_MAIN Fiber
// resume loop) against the game given on the command line. Compiled into the
// shared liblove and reached via the exported `love_mrb_main`; the thin `love`
// executable (src/love_mrb_exe.cpp) just forwards to it. Built only in the
// LOVE_MRUBY build (the parallel cmake/LoveMruby.cmake path).

#include "common/config.h"   // LOVE_EXPORT
#include "common/version.h"
#include "common/mrb_runtime.h"

extern "C" {
	#include <mruby/compile.h>
	#include <mruby/string.h>
	#include <mruby/variable.h>
}

#include <cstdio>
#include <cstring>
#include <string>
#include <sys/stat.h>

// The boot scripts, baked in by embed_script.cmake (see CMAKE_MIGRATION.md), so
// a shipped binary doesn't read them from disk.
#include "arg_rb.h"
#include "callbacks_rb.h"
#include "boot_rb.h"
#include "nogame_rb.h"

// Each ported module's C-accessible opener (defined in its wrap_*_mrb.cpp),
// mirroring the luaopen_love_* table the Lua build uses.
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

static std::string g_arg0;

// Register the Love namespace + every module into an mrb_state (the main VM and
// each thread VM). Keep in step with harness.cpp's open_love.
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

static bool read_file(const char *path, std::string &out)
{
	FILE *f = fopen(path, "rb");
	if (f == nullptr)
		return false;
	char buf[4096];
	size_t n;
	while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
		out.append(buf, n);
	fclose(f);
	return true;
}

static bool is_dir(const char *path)
{
	struct stat st;
	return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

// Run embedded Ruby source (NUL-terminated; the embedded arrays are unsigned
// char, hence the cast) with a filename context. False on a raised exception.
static bool run_embedded(mrb_state *mrb, const unsigned char *source, const char *name)
{
	mrbc_context *ctx = mrbc_context_new(mrb);
	mrbc_filename(mrb, ctx, name);
	mrb_load_string_cxt(mrb, (const char *) source, ctx);
	mrbc_context_free(mrb, ctx);
	if (mrb->exc)
	{
		mrb_print_error(mrb);
		mrb->exc = nullptr;
		return false;
	}
	return true;
}

// Resolve the game argument to a source file: a .rb file, or a directory's
// main.rb (mirrors LÖVE running a game folder). Returns false if unreadable.
static bool load_game_source(const std::string &game, std::string &path, std::string &source)
{
	path = is_dir(game.c_str()) ? (game + "/main.rb") : game;
	return read_file(path.c_str(), source);
}

enum DoneAction { DONE_QUIT, DONE_RESTART };

// One boot: open a VM, register modules, run the pipeline + Fiber loop. Returns
// the action (quit/restart) and writes the exit code. With nogame=true there is
// no game on the command line: the embedded nogame.rb is loaded and boot.rb
// (seeing an empty $LOVE_GAME_SOURCE) installs the no-game screen's callbacks.
static DoneAction runlove(const std::string &game, int &retval, bool nogame)
{
	mrb_state *mrb = mrb_open();
	if (mrb == nullptr)
	{
		fprintf(stderr, "failed to open mruby state\n");
		retval = 1;
		return DONE_QUIT;
	}

	open_love(mrb);
	love::thread::g_threadVMOpener = open_love;

	std::string path, source;
	if (!nogame && !load_game_source(game, path, source))
	{
		fprintf(stderr, "could not read game: %s\n", game.c_str());
		love::mrbx_close_state(mrb);
		retval = 1;
		return DONE_QUIT;
	}

	mrb_gv_set(mrb, mrb_intern_lit(mrb, "$LOVE_GAME_FILE"), mrb_str_new_cstr(mrb, path.c_str()));
	mrb_gv_set(mrb, mrb_intern_lit(mrb, "$LOVE_GAME_SOURCE"), mrb_str_new(mrb, source.data(), source.size()));

	DoneAction done = DONE_QUIT;
	retval = 0;

	if (run_embedded(mrb, arg_rb, "arg.rb")
		&& run_embedded(mrb, callbacks_rb, "callbacks.rb")
		&& (!nogame || run_embedded(mrb, nogame_rb, "nogame.rb"))
		&& run_embedded(mrb, boot_rb, "boot.rb"))
	{
		mrb_value fiber = mrb_gv_get(mrb, mrb_intern_lit(mrb, "$LOVE_MAIN"));
		mrb_value last = mrb_nil_value();
		while (mrb_test(mrb_funcall(mrb, fiber, "alive?", 0)) && mrb->exc == nullptr)
			last = mrb_funcall(mrb, fiber, "resume", 0);

		if (mrb->exc)
		{
			mrb_print_error(mrb);
			retval = 1;
		}
		else if (mrb_string_p(last) && strcmp(mrb_str_to_cstr(mrb, last), "restart") == 0)
			done = DONE_RESTART;
		else if (mrb_integer_p(last))
			retval = (int) mrb_integer(last);
	}
	else
		retval = 1;

	love::mrbx_close_state(mrb);
	return done;
}

static void print_usage()
{
	printf("LOVE is a framework for making 2D games -- mruby build (Ruby games).\n"
		"https://love2d.org\n\n"
		"usage:\n"
		"    love --version            prints the LOVE version and quits\n"
		"    love --help               prints this message and quits\n"
		"    love path/to/gamedir      runs the game (a folder with a main.rb)\n"
		"    love path/to/main.rb      runs a single Ruby game file\n");
}

// The exported liblove entry point. The thin `love` executable
// (src/love_mrb_exe.cpp) just forwards to this, mirroring the Lua build's
// src/love.cpp -> liblove split.
extern "C" LOVE_EXPORT int love_mrb_main(int argc, char **argv)
{
	g_arg0 = argc > 0 ? argv[0] : "love";

	if (argc > 1 && strcmp(argv[1], "--version") == 0)
	{
		printf("LOVE %s [mruby]\n", LOVE_VERSION_STRING);
		return 0;
	}
	if (argc > 1 && strcmp(argv[1], "--help") == 0)
	{
		print_usage();
		return 0;
	}

	// No game given: print usage (like the Lua build) and show the no-game
	// screen instead of bailing out.
	bool nogame = argc <= 1;
	if (nogame)
		print_usage();

	std::string game = nogame ? std::string() : argv[1];
	int retval = 0;
	DoneAction done;
	do {
		done = runlove(game, retval, nogame);
	} while (done == DONE_RESTART);

	return retval;
}
