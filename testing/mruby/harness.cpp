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

namespace love { namespace timer { extern "C" void mrb_love_timer_init(mrb_state *mrb); } }

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

int main(int argc, char **argv)
{
	// Resolve the script path: explicit arg, else sample.rb next to the binary.
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

	std::string source;
	if (!read_file(path.c_str(), source))
	{
		fprintf(stderr, "could not read script: %s\n", path.c_str());
		return 1;
	}

	mrb_state *mrb = mrb_open();
	if (mrb == nullptr)
	{
		fprintf(stderr, "failed to open mruby state\n");
		return 1;
	}

	// Top-level namespace that all LÖVE modules live under.
	mrb_define_module(mrb, "Love");

	love::timer::mrb_love_timer_init(mrb);

	mrbc_context *ctx = mrbc_context_new(mrb);
	mrbc_filename(mrb, ctx, path.c_str());
	mrb_load_string_cxt(mrb, source.c_str(), ctx);
	mrbc_context_free(mrb, ctx);

	int rc = 0;
	if (mrb->exc)
	{
		mrb_print_error(mrb);
		rc = 1;
	}

	mrb_close(mrb);
	return rc;
}
