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

// mruby replacement for LuaThread.cpp. The class/interface (LuaThread, the
// "Thread" love::Type) is unchanged so ThreadModule.cpp links against it as-is;
// only the VM run inside threadFunction differs: each thread spins up its own
// mrb_state (mruby states are not shareable across threads), opens the LÖVE
// modules into it via a host-installed opener, and runs the thread's code.

#include "LuaThread.h"
#include "event/Event.h"
#include "common/config.h"
#include "common/mrb_runtime.h"

#include <mruby.h>
#include <mruby/compile.h>
#include <mruby/string.h>
#include <mruby/array.h>
#include <mruby/variable.h>

namespace love
{
namespace thread
{

// Installed by the host (e.g. the harness): registers the Love:: modules into a
// freshly-created thread mrb_state. Without it a thread VM still runs, but only
// plain Ruby (no LÖVE modules / channels).
ThreadVMOpener g_threadVMOpener = nullptr;

love::Type LuaThread::type("Thread", &Threadable::type);

LuaThread::LuaThread(const std::string &name, love::Data *code)
	: code(code)
	, name(name)
	, haserror(false)
{
	threadName = name;
}

LuaThread::~LuaThread()
{
}

void LuaThread::threadFunction()
{
	error.clear();
	haserror = false;

	mrb_state *mrb = mrb_open();
	if (mrb == nullptr)
	{
		error = "Could not create mruby state for thread.";
		haserror = true;
		onError();
		return;
	}

	// Bring up the LÖVE modules in this thread's VM (love.thread / love.filesystem
	// and the rest), so channels and filesystem paths work just like the main VM.
	if (g_threadVMOpener != nullptr)
		g_threadVMOpener(mrb);

	// The start() arguments, exposed as a global array -- the mruby analog of the
	// Lua thread chunk's `...` varargs. A thread script reads `$LOVE_THREAD_ARGS`.
	mrb_value argv = mrb_ary_new_capa(mrb, (mrb_int) args.size());
	for (const Variant &v : args)
		mrb_ary_push(mrb, argv, mrbx_pushvariant(mrb, v));
	args.clear();
	mrb_gv_set(mrb, mrb_intern_lit(mrb, "$LOVE_THREAD_ARGS"), argv);

	mrb_load_nstring(mrb, (const char *) code->getData(), code->getSize());

	if (mrb->exc)
	{
		mrb_value exc = mrb_obj_value(mrb->exc);
		mrb->exc = nullptr;
		mrb_value msg = mrb_funcall(mrb, exc, "message", 0);
		if (mrb_string_p(msg))
			error.assign(RSTRING_PTR(msg), RSTRING_LEN(msg));
		else
			error = "unknown error in thread";
		haserror = true;
	}

	mrbx_forgetstate(mrb);
	mrb_close(mrb);

	if (haserror)
		onError();
}

bool LuaThread::start(const std::vector<Variant> &args)
{
	if (isRunning())
		return false;

	this->args = args;
	error.clear();
	haserror = false;

	return Threadable::start();
}

const std::string &LuaThread::getError() const
{
	return error;
}

void LuaThread::onError()
{
	auto eventmodule = Module::getInstance<event::Event>(Module::M_EVENT);
	if (!eventmodule)
		return;

	std::vector<Variant> vargs = {
		Variant(&LuaThread::type, this),
		Variant(error.c_str(), error.length())
	};

	StrongRef<event::Message> msg(new event::Message("threaderror", vargs), Acquire::NORETAIN);
	eventmodule->push(msg);
}

} // thread
} // love
