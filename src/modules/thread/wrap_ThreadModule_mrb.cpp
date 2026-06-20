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

// mruby port of wrap_ThreadModule.cpp + wrap_LuaThread.cpp + wrap_Channel.cpp.
//
// Exposes Love::Thread (the module functions), the Love::Thread object type
// (a running thread), and the Love::Channel object type (thread-safe message
// passing). snake_case names, keyword arguments. Values crossing channels and
// thread-start arguments use the runtime's Variant <-> Ruby conversion.
//
// A thread runs an mruby script in its own mrb_state; the start() arguments are
// exposed to that script as the global `$LOVE_THREAD_ARGS` array (the mruby
// analog of the Lua chunk's `...` varargs).

#include "common/config.h"
#include "common/mrb_runtime.h"
#include "common/Variant.h"

#include "ThreadModule.h"
#include "LuaThread.h"
#include "Channel.h"

#include "filesystem/Filesystem.h"
#include "filesystem/FileData.h"

#include <cstring>
#include <string>
#include <vector>

namespace love
{
namespace thread
{

#define instance() (Module::getInstance<ThreadModule>(Module::M_THREAD))

// --- module functions ----------------------------------------------------

// new_thread(code:) -- code is the thread's source. A multi-line or long String
// (or a Data/FileData) is treated as code; a short single-line String is a
// filename read via the filesystem module.
static mrb_value w_newThread(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[1];
	mrbx_get_kwargs(mrb, {"code"}, 1, v);

	std::string name = "Thread code";
	love::Data *data = nullptr;

	if (mrb_string_p(v[0]))
	{
		const char *str = RSTRING_PTR(v[0]);
		mrb_int slen = RSTRING_LEN(v[0]);
		auto fs = Module::getInstance<love::filesystem::Filesystem>(Module::M_FILESYSTEM);
		if (fs == nullptr)
			mrb_raise(mrb, E_RUNTIME_ERROR, "The filesystem module must be loaded to create a thread.");

		if (slen >= 1024 || memchr(str, '\n', (size_t) slen) != nullptr)
		{
			// Treat as code.
			auto *fd = fs->newFileData(str, (size_t) slen, "Thread code");
			name = std::string("@") + fd->getFilename();
			data = fd;
		}
		else
		{
			// Treat as a filename.
			love::filesystem::FileData *fd = nullptr;
			if (mrbx_catchexcept(mrb, [&]() { fd = fs->read(str); }))
				return mrb_nil_value();
			name = std::string("@") + fd->getFilename();
			data = fd;
		}
	}
	else if (mrbx_istype<love::filesystem::FileData>(mrb, v[0]))
	{
		auto *fd = mrbx_checktype<love::filesystem::FileData>(mrb, v[0]);
		fd->retain();
		name = std::string("@") + fd->getFilename();
		data = fd;
	}
	else
	{
		love::Data *d = mrbx_checktype<love::Data>(mrb, v[0]);
		d->retain();
		data = d;
	}

	LuaThread *t = instance()->newThread(name, data);
	data->release();

	mrb_value r = mrbx_pushtype(mrb, t);
	t->release();
	return r;
}

static mrb_value w_newChannel(mrb_state *mrb, mrb_value self)
{
	(void) self;
	Channel *c = instance()->newChannel();
	mrb_value r = mrbx_pushtype(mrb, c);
	c->release();
	return r;
}

static mrb_value w_getChannel(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[1];
	mrbx_get_kwargs(mrb, {"name"}, 1, v);
	std::string n = mrbx_checkstring(mrb, v[0]);
	Channel *c = instance()->getChannel(n);
	// getChannel returns a borrowed reference owned by the named-channel registry.
	return mrbx_pushtype(mrb, c);
}

static const MrbReg functions[] =
{
	{ "new_thread",  w_newThread,  MRB_ARGS_KEY(1, 0) },
	{ "new_channel", w_newChannel, MRB_ARGS_NONE() },
	{ "get_channel", w_getChannel, MRB_ARGS_KEY(1, 0) },
	{ nullptr, nullptr, 0 }
};

// --- Thread instance methods ---------------------------------------------

// start(args:) -- args is an optional Array of values handed to the thread
// script as $LOVE_THREAD_ARGS. Returns false if already running.
static mrb_value t_start(mrb_state *mrb, mrb_value self)
{
	LuaThread *t = mrbx_checktype<LuaThread>(mrb, self);
	mrb_value v[1];
	mrbx_get_kwargs(mrb, {"args"}, 0, v);

	std::vector<Variant> args;
	if (!mrb_undef_p(v[0]))
	{
		if (!mrb_array_p(v[0]))
			mrb_raise(mrb, E_ARGUMENT_ERROR, "args: must be an Array");
		for (mrb_int i = 0; i < RARRAY_LEN(v[0]); i++)
		{
			Variant var = mrbx_checkvariant(mrb, mrb_ary_ref(mrb, v[0], i));
			if (var.getType() == Variant::UNKNOWN)
				mrb_raise(mrb, E_ARGUMENT_ERROR, "thread arguments must be boolean, number, string, a love type, or a flat array/hash");
			args.push_back(var);
		}
	}

	return mrbx_boolean(mrb, t->start(args));
}

static mrb_value t_wait(mrb_state *mrb, mrb_value self)
{
	mrbx_checktype<LuaThread>(mrb, self)->wait();
	return mrb_nil_value();
}

static mrb_value t_getError(mrb_state *mrb, mrb_value self)
{
	LuaThread *t = mrbx_checktype<LuaThread>(mrb, self);
	if (t->hasError())
		return mrbx_string(mrb, t->getError());
	return mrb_nil_value();
}

static mrb_value t_isRunning(mrb_state *mrb, mrb_value self)
{
	return mrbx_boolean(mrb, mrbx_checktype<LuaThread>(mrb, self)->isRunning());
}

static const MrbReg threadFunctions[] =
{
	{ "start",     t_start,     MRB_ARGS_KEY(1, 0) },
	{ "wait",      t_wait,      MRB_ARGS_NONE() },
	{ "get_error", t_getError,  MRB_ARGS_NONE() },
	{ "running?",  t_isRunning, MRB_ARGS_NONE() },
	{ nullptr, nullptr, 0 }
};

// --- Channel instance methods --------------------------------------------

static mrb_value c_push(mrb_state *mrb, mrb_value self)
{
	Channel *c = mrbx_checktype<Channel>(mrb, self);
	mrb_value v[1];
	mrbx_get_kwargs(mrb, {"value"}, 1, v);
	Variant var = mrbx_checkvariant(mrb, v[0]);
	if (var.getType() == Variant::UNKNOWN)
		mrb_raise(mrb, E_ARGUMENT_ERROR, "expected boolean, number, string, a love type, or a flat array/hash");
	uint64 id = 0;
	mrbx_catchexcept(mrb, [&]() { id = c->push(var); });
	return mrbx_integer(mrb, (int) id);
}

static mrb_value c_supply(mrb_state *mrb, mrb_value self)
{
	Channel *c = mrbx_checktype<Channel>(mrb, self);
	mrb_value v[2];
	mrbx_get_kwargs(mrb, {"value", "timeout"}, 1, v);
	Variant var = mrbx_checkvariant(mrb, v[0]);
	if (var.getType() == Variant::UNKNOWN)
		mrb_raise(mrb, E_ARGUMENT_ERROR, "expected boolean, number, string, a love type, or a flat array/hash");
	bool result = false;
	if (mrb_undef_p(v[1]))
		mrbx_catchexcept(mrb, [&]() { result = c->supply(var); });
	else
	{
		double timeout = mrbx_checknumber(mrb, v[1]);
		mrbx_catchexcept(mrb, [&]() { result = c->supply(var, timeout); });
	}
	return mrbx_boolean(mrb, result);
}

static mrb_value c_pop(mrb_state *mrb, mrb_value self)
{
	Channel *c = mrbx_checktype<Channel>(mrb, self);
	Variant var;
	bool found = c->pop(&var);
	return found ? mrbx_pushvariant(mrb, var) : mrb_nil_value();
}

static mrb_value c_demand(mrb_state *mrb, mrb_value self)
{
	Channel *c = mrbx_checktype<Channel>(mrb, self);
	mrb_value v[1];
	mrbx_get_kwargs(mrb, {"timeout"}, 0, v);
	Variant var;
	bool found = false;
	if (mrb_undef_p(v[0]))
		found = c->demand(&var);
	else
	{
		double timeout = mrbx_checknumber(mrb, v[0]);
		found = c->demand(&var, timeout);
	}
	return found ? mrbx_pushvariant(mrb, var) : mrb_nil_value();
}

static mrb_value c_peek(mrb_state *mrb, mrb_value self)
{
	Channel *c = mrbx_checktype<Channel>(mrb, self);
	Variant var;
	bool found = c->peek(&var);
	return found ? mrbx_pushvariant(mrb, var) : mrb_nil_value();
}

static mrb_value c_getCount(mrb_state *mrb, mrb_value self)
{
	return mrbx_integer(mrb, mrbx_checktype<Channel>(mrb, self)->getCount());
}

static mrb_value c_hasRead(mrb_state *mrb, mrb_value self)
{
	Channel *c = mrbx_checktype<Channel>(mrb, self);
	mrb_value v[1];
	mrbx_get_kwargs(mrb, {"id"}, 1, v);
	uint64 id = (uint64) mrbx_checkint(mrb, v[0]);
	return mrbx_boolean(mrb, c->hasRead(id));
}

static mrb_value c_clear(mrb_state *mrb, mrb_value self)
{
	mrbx_checktype<Channel>(mrb, self)->clear();
	return mrb_nil_value();
}

// perform_atomic { |channel| ... } -- runs the block with the channel's mutex
// held, so a read-modify-write sequence on the channel is atomic.
static mrb_value c_performAtomic(mrb_state *mrb, mrb_value self)
{
	Channel *c = mrbx_checktype<Channel>(mrb, self);
	mrb_value blk = mrb_nil_value();
	mrb_get_args(mrb, "&", &blk);
	if (mrb_nil_p(blk))
		mrb_raise(mrb, E_ARGUMENT_ERROR, "perform_atomic requires a block");

	c->lockMutex();
	mrb_value result = mrb_yield_argv(mrb, blk, 1, &self);
	c->unlockMutex();
	return result;
}

static const MrbReg channelFunctions[] =
{
	{ "push",           c_push,          MRB_ARGS_KEY(1, 0) },
	{ "supply",         c_supply,        MRB_ARGS_KEY(2, 0) },
	{ "pop",            c_pop,           MRB_ARGS_NONE() },
	{ "demand",         c_demand,        MRB_ARGS_KEY(1, 0) },
	{ "peek",           c_peek,          MRB_ARGS_NONE() },
	{ "get_count",      c_getCount,      MRB_ARGS_NONE() },
	{ "has_read?",      c_hasRead,       MRB_ARGS_KEY(1, 0) },
	{ "clear",          c_clear,         MRB_ARGS_NONE() },
	{ "perform_atomic", c_performAtomic, MRB_ARGS_BLOCK() },
	{ nullptr, nullptr, 0 }
};

// Equivalent of luaopen_love_thread. The module name "Thread" collides with the
// Thread object type's class (both want to be Love::Thread), so -- exactly like
// the data module -- the module functions are registered as class methods on the
// Thread type's class rather than via a separate module. (The native
// ThreadModule self-registers in its constructor, so instance() still resolves.)
extern "C" void mrb_love_thread_init(mrb_state *mrb)
{
	if (instance() == nullptr)
		(new ThreadModule())->retain();

	struct RClass *threadClass = mrbx_gettypeclass(mrb, LuaThread::type);

	for (const MrbReg *r = threadFunctions; r->name != nullptr; r++)
		mrb_define_method(mrb, threadClass, r->name, r->func, r->aspec);

	for (const MrbReg *r = functions; r->name != nullptr; r++)
		mrb_define_class_method(mrb, threadClass, r->name, r->func, r->aspec);

	mrbx_register_type(mrb, Channel::type, channelFunctions);
}

} // thread
} // love
