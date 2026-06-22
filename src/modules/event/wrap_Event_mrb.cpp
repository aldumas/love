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

// mruby port of wrap_Event.cpp.
//
// API shape: a Love::Event module, snake_case names, keyword arguments. The
// Lua iterator pattern (love.event.poll() returning poll_i) is replaced by the
// Ruby idiom of Love::Event.poll returning an Array of events, each event being
// [name_symbol, *args]:
//
//   love.event.poll()         -> Love::Event.poll      (Array of [:name, *args])
//   for e in love.event.poll  -> Love::Event.poll.each { |name, a, b, c| ... }
//   love.event.push("x", 1)   -> Love::Event.push(name: "x", args: [1])
//   love.event.quit(0)        -> Love::Event.quit(code: 0)
//
// Backend note: this now drives the REAL SDL event backend
// (event/sdl/Event.cpp), which translates the full SDL event set
// (keyboard/text/mouse/touch/joystick/gamepad/sensor/window/drop/system) by
// resolving the real keyboard/window/graphics/input/audio modules -- all of
// which are now ported, including the real window::sdl::Window the live-resize
// modal-draw hook dynamic_casts to. The Ruby-facing API (pump/poll/push/clear/
// quit/restart) is unchanged; only the C++ backend behind it changed.

#include "common/config.h"
#include "common/mrb_runtime.h"
#include "common/Variant.h"
#include "common/int.h"
#include "Event.h"
#include "sdl/Event.h"

#include <vector>

namespace love
{

namespace event
{

#define instance() (Module::getInstance<Event>(Module::M_EVENT))

// =========================================================================
// Love::Event module functions
// =========================================================================

// Builds the [name_symbol, *args] Array for one message.
static mrb_value pushmessage(mrb_state *mrb, const Message &m)
{
	mrb_value arr = mrb_ary_new_capa(mrb, (mrb_int) m.args.size() + 1);
	mrb_ary_push(mrb, arr, mrb_symbol_value(mrb_intern_cstr(mrb, m.name.c_str())));
	for (const Variant &v : m.args)
		mrb_ary_push(mrb, arr, mrbx_pushvariant(mrb, v));
	return arr;
}

static mrb_value w_pump(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[1];
	mrbx_get_kwargs(mrb, {"timeout"}, 0, v);
	float timeout = mrbx_optfloat(mrb, v[0], 0.0f);
	mrbx_catchexcept(mrb, [&]() { instance()->pump(timeout); });
	return mrb_nil_value();
}

// Drains the event queue, returning an Array of [name_symbol, *args] events.
static mrb_value w_poll(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value arr = mrb_ary_new(mrb);

	Message *m = nullptr;
	while (instance()->poll(m) && m != nullptr)
	{
		mrb_ary_push(mrb, arr, pushmessage(mrb, *m));
		m->release();
		m = nullptr;
	}
	return arr;
}

static mrb_value w_push(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[2];
	mrbx_get_kwargs(mrb, {"name", "args"}, 1, v);

	std::string name = mrbx_checkstring(mrb, v[0]);

	std::vector<Variant> vargs;
	if (!mrb_undef_p(v[1]) && mrb_array_p(v[1]))
	{
		mrb_int n = RARRAY_LEN(v[1]);
		vargs.reserve(n);
		for (mrb_int i = 0; i < n; i++)
		{
			Variant var = mrbx_checkvariant(mrb, mrb_ary_ref(mrb, v[1], i));
			if (var.getType() == Variant::UNKNOWN)
				mrb_raisef(mrb, E_ARGUMENT_ERROR, "Argument %d can't be stored safely.", (int)(i + 1));
			vargs.push_back(var);
		}
	}

	StrongRef<Message> m(new Message(name, vargs), Acquire::NORETAIN);
	instance()->push(m);
	return mrb_true_value();
}

static mrb_value w_clear(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrbx_catchexcept(mrb, [&]() { instance()->clear(); });
	return mrb_nil_value();
}

static mrb_value w_quit(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[1];
	mrbx_get_kwargs(mrb, {"code"}, 0, v);

	std::vector<Variant> args;
	args.emplace_back((double) mrbx_optint(mrb, v[0], 0));

	StrongRef<Message> m(new Message("quit", args), Acquire::NORETAIN);
	instance()->push(m);
	return mrb_true_value();
}

static mrb_value w_restart(mrb_state *mrb, mrb_value self)
{
	(void) self;
	std::vector<Variant> args;
	args.emplace_back("restart", strlen("restart"));

	StrongRef<Message> m(new Message("quit", args), Acquire::NORETAIN);
	instance()->push(m);
	return mrb_true_value();
}

static const MrbReg functions[] =
{
	{ "pump",    w_pump,    MRB_ARGS_KEY(1, 0) },
	{ "poll",    w_poll,    MRB_ARGS_NONE() },
	{ "push",    w_push,    MRB_ARGS_KEY(2, 0) },
	{ "clear",   w_clear,   MRB_ARGS_NONE() },
	{ "quit",    w_quit,    MRB_ARGS_KEY(1, 0) },
	{ "restart", w_restart, MRB_ARGS_NONE() },
	{ nullptr, nullptr, 0 }
};

// Equivalent of luaopen_love_event: creates the backend and registers
// Love::Event with its keyword-argument methods.
extern "C" void mrb_love_event_init(mrb_state *mrb)
{
	Event *inst = instance();
	if (inst == nullptr)
		inst = new sdl::Event();
	else
		inst->retain();

	WrappedModule w;
	w.module = inst;
	w.name = "Event";
	w.type = &Module::type;
	w.functions = functions;

	mrbx_register_module(mrb, w);
}

} // event
} // love
