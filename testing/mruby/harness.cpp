/**
 * Standalone harness / playground for the mruby-port proof-of-concept.
 *
 * The full `love` binary cannot link until all 74 modules are ported, so this
 * small program exercises the end-to-end mruby pipeline for the one module that
 * has been ported (timer):
 *
 *   - opens an mruby state
 *   - defines the top-level `Love` namespace
 *   - registers Love::Timer (via the ported wrapper)
 *   - runs a Ruby script that calls the keyword-argument API
 *
 * Usage:
 *   love_mrb_harness [script.rb]
 *
 * With no argument it runs testing/mruby/sample.rb relative to the executable's
 * directory. Pass any .rb path to run your own script. Edit the script and
 * re-run -- no recompile needed.
 *
 * Build instructions live in testing/mruby/README.md.
 */

#include "common/mrb_runtime.h"

extern "C" {
	#include <mruby/compile.h>
	#include <mruby/string.h>
}

#include <cstdio>
#include <string>
#include <vector>

namespace love { namespace timer      { extern "C" void mrb_love_timer_init(mrb_state *mrb); } }
namespace love { namespace math       { extern "C" void mrb_love_math_init(mrb_state *mrb);  } }
namespace love { namespace filesystem { extern "C" void mrb_love_filesystem_init(mrb_state *mrb); } }
namespace love { namespace event      { extern "C" void mrb_love_event_init(mrb_state *mrb); } }
namespace love { namespace window     { extern "C" void mrb_love_window_init(mrb_state *mrb); } }
namespace love { namespace graphics   { extern "C" void mrb_love_graphics_init(mrb_state *mrb); } }
namespace love { namespace keyboard   { extern "C" void mrb_love_keyboard_init(mrb_state *mrb); } }

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

// Loads and runs a Ruby file with a filename context (for backtraces). Returns
// false if the file can't be read or raised an exception (which is printed).
static bool run_file(mrb_state *mrb, const std::string &path)
{
	std::string source;
	if (!read_file(path.c_str(), source))
	{
		fprintf(stderr, "could not read script: %s\n", path.c_str());
		return false;
	}

	mrbc_context *ctx = mrbc_context_new(mrb);
	mrbc_filename(mrb, ctx, path.c_str());
	mrb_load_string_cxt(mrb, source.c_str(), ctx);
	mrbc_context_free(mrb, ctx);

	if (mrb->exc)
	{
		mrb_print_error(mrb);
		mrb->exc = nullptr;
		return false;
	}
	return true;
}

#ifndef LOVE_SRC_DIR
#define LOVE_SRC_DIR "."
#endif

// Drives the boot pipeline: load the ported framework scripts, point them at
// the game file, then resume the $LOVE_MAIN Fiber until it finishes -- the same
// resume-until-done loop love.cpp runs against the Lua boot coroutine. Returns
// the game's exit code.
static int run_boot(mrb_state *mrb, const std::string &game)
{
	const std::string scriptdir = std::string(LOVE_SRC_DIR) + "/modules/love/";

	// The game file the boot sequence will load (mirrors require("main")).
	// mruby has no file-loading Kernel#load, so we hand boot.rb the source to
	// eval; the path is kept for messages.
	std::string gamesrc;
	if (!read_file(game.c_str(), gamesrc))
	{
		fprintf(stderr, "could not read game: %s\n", game.c_str());
		return 1;
	}
	mrb_gv_set(mrb, mrb_intern_lit(mrb, "$LOVE_GAME_FILE"), mrb_str_new_cstr(mrb, game.c_str()));
	mrb_gv_set(mrb, mrb_intern_lit(mrb, "$LOVE_GAME_SOURCE"), mrb_str_new(mrb, gamesrc.data(), gamesrc.size()));

	if (!run_file(mrb, scriptdir + "arg.rb"))       return 1;
	if (!run_file(mrb, scriptdir + "callbacks.rb")) return 1;
	if (!run_file(mrb, scriptdir + "boot.rb"))      return 1;

	mrb_value fiber = mrb_gv_get(mrb, mrb_intern_lit(mrb, "$LOVE_MAIN"));
	mrb_value last = mrb_nil_value();

	while (mrb_test(mrb_funcall(mrb, fiber, "alive?", 0)) && mrb->exc == nullptr)
		last = mrb_funcall(mrb, fiber, "resume", 0);

	if (mrb->exc)
	{
		mrb_print_error(mrb);
		return 1;
	}

	return mrb_integer_p(last) ? (int) mrb_integer(last) : 0;
}

int main(int argc, char **argv)
{
	// Two modes:
	//   love_mrb_harness [script.rb]          run a flat script directly
	//   love_mrb_harness --boot [game.rb]     run the full boot pipeline
	bool boot = argc > 1 && std::string(argv[1]) == "--boot";

	mrb_state *mrb = mrb_open();
	if (mrb == nullptr)
	{
		fprintf(stderr, "failed to open mruby state\n");
		return 1;
	}

	// Top-level namespace that all LÖVE modules live under.
	struct RClass *love = mrb_define_module(mrb, "Love");

	// Expose the executable path so scripts can bootstrap the filesystem module
	// (Love::Filesystem.init(arg0: Love::ARG0)), mirroring what love.cpp passes
	// from main(argv[0]) in the real engine.
	mrb_define_const(mrb, love, "ARG0", mrb_str_new_cstr(mrb, argv[0]));

	love::timer::mrb_love_timer_init(mrb);
	love::math::mrb_love_math_init(mrb);
	love::filesystem::mrb_love_filesystem_init(mrb);
	love::event::mrb_love_event_init(mrb);
	love::window::mrb_love_window_init(mrb);
	love::graphics::mrb_love_graphics_init(mrb);
	love::keyboard::mrb_love_keyboard_init(mrb);

	int rc;
	if (boot)
	{
		std::string exe = argv[0];
		size_t slash = exe.find_last_of('/');
		std::string dir = (slash == std::string::npos) ? "." : exe.substr(0, slash);
		std::string game = argc > 2 ? argv[2] : dir + "/game.rb";
		rc = run_boot(mrb, game);
	}
	else
	{
		std::string path;
		if (argc > 1)
		{
			path = argv[1];
		}
		else
		{
			std::string exe = argv[0];
			size_t slash = exe.find_last_of('/');
			std::string dir = (slash == std::string::npos) ? "." : exe.substr(0, slash);
			path = dir + "/sample.rb";
		}
		rc = run_file(mrb, path) ? 0 : 1;
	}

	mrb_close(mrb);
	return rc;
}
