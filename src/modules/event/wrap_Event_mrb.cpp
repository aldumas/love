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
// Backend note: LÖVE's full SDL event backend (event/sdl/Event.cpp) pulls in
// the entire input/window/graphics/audio dependency graph to translate every
// SDL event. Until those modules are ported, this file provides a lean backend
// (HarnessEvent) that reuses the platform-independent message queue from
// event/Event.cpp and converts only the window-independent SDL events, using
// SDL directly (no love::keyboard / love::window dependency). When window and
// the input modules land, this can be swapped for the real sdl::Event with no
// change to the Ruby-facing API.

#include "common/config.h"
#include "common/mrb_runtime.h"
#include "common/Variant.h"
#include "Event.h"

#include <SDL3/SDL.h>

#include <cmath>
#include <cstring>
#include <vector>

namespace love
{

// Defined in the keyboard wrapper: whether repeat keypressed events should be
// forwarded (true if the keyboard module is absent or key repeat is enabled).
namespace keyboard { bool harnessKeyRepeatEnabled(); }

namespace event
{

#define instance() (Module::getInstance<Event>(Module::M_EVENT))

// =========================================================================
// Lean, window-independent SDL backend
// =========================================================================

// TODO(mruby) #event-backend: lean backend — window-independent SDL event
// conversion only; swap for event/sdl/Event.cpp once joystick/touch/sensor
// land (see PORTING.md §B)
class HarnessEvent : public love::event::Event
{
public:

	HarnessEvent()
		: love::event::Event("love.event.harness")
	{
		if (!SDL_InitSubSystem(SDL_INIT_EVENTS))
			throw love::Exception("Could not initialize SDL events subsystem (%s)", SDL_GetError());
	}

	~HarnessEvent() override
	{
		SDL_QuitSubSystem(SDL_INIT_EVENTS);
	}

	void pump(float waitTimeout) override
	{
		SDL_Event e;

		if (waitTimeout != 0.0f)
		{
			int ms = 0;
			if (std::isinf(waitTimeout) || waitTimeout < 0.0f)
				ms = -1; // wait forever
			else
				ms = (int) std::min<double>(2147483647.0, 1000.0 * waitTimeout);

			if (SDL_WaitEventTimeout(&e, ms))
			{
				StrongRef<Message> msg(convert(e), Acquire::NORETAIN);
				if (msg)
					push(msg);
			}
		}
		else
		{
			SDL_PumpEvents();
		}

		while (SDL_PollEvent(&e))
		{
			StrongRef<Message> msg(convert(e), Acquire::NORETAIN);
			if (msg)
				push(msg);
		}
	}

	Message *wait() override
	{
		SDL_Event e;
		if (!SDL_WaitEvent(&e))
			return nullptr;
		return convert(e);
	}

private:

	// Translates an SDL event into a LÖVE Message, limited to events that don't
	// require the window/input modules. Returns nullptr for events we ignore.
	static Message *convert(const SDL_Event &e)
	{
		std::vector<Variant> a;
		const char *txt;

		switch (e.type)
		{
		case SDL_EVENT_QUIT:
		case SDL_EVENT_TERMINATING:
			return new Message("quit");

		case SDL_EVENT_KEY_DOWN:
			// Drop auto-repeat keypresses when key repeat is disabled, matching
			// the real event backend (#kbd-keyrepeat).
			if (e.key.repeat != 0 && !love::keyboard::harnessKeyRepeatEnabled())
				return nullptr;
			txt = SDL_GetKeyName(e.key.key);
			a.emplace_back(txt, strlen(txt));
			txt = SDL_GetScancodeName(e.key.scancode);
			a.emplace_back(txt, strlen(txt));
			a.emplace_back(e.key.repeat != 0);
			return new Message("keypressed", a);

		case SDL_EVENT_KEY_UP:
			txt = SDL_GetKeyName(e.key.key);
			a.emplace_back(txt, strlen(txt));
			txt = SDL_GetScancodeName(e.key.scancode);
			a.emplace_back(txt, strlen(txt));
			return new Message("keyreleased", a);

		case SDL_EVENT_TEXT_INPUT:
			txt = e.text.text;
			a.emplace_back(txt, strlen(txt));
			return new Message("textinput", a);

		case SDL_EVENT_MOUSE_MOTION:
			a.emplace_back((double) e.motion.x);
			a.emplace_back((double) e.motion.y);
			a.emplace_back((double) e.motion.xrel);
			a.emplace_back((double) e.motion.yrel);
			a.emplace_back(e.motion.which == SDL_TOUCH_MOUSEID);
			return new Message("mousemoved", a);

		case SDL_EVENT_MOUSE_BUTTON_DOWN:
		case SDL_EVENT_MOUSE_BUTTON_UP:
		{
			// LÖVE uses button 2 for right and 3 for middle.
			int button = e.button.button;
			if (button == SDL_BUTTON_RIGHT)  button = 2;
			else if (button == SDL_BUTTON_MIDDLE) button = 3;

			a.emplace_back((double) e.button.x);
			a.emplace_back((double) e.button.y);
			a.emplace_back((double) button);
			a.emplace_back(e.button.which == SDL_TOUCH_MOUSEID);
			a.emplace_back((double) e.button.clicks);
			return new Message(e.type == SDL_EVENT_MOUSE_BUTTON_DOWN ? "mousepressed" : "mousereleased", a);
		}

		case SDL_EVENT_MOUSE_WHEEL:
			a.emplace_back((double) e.wheel.x);
			a.emplace_back((double) e.wheel.y);
			return new Message("wheelmoved", a);

		case SDL_EVENT_WINDOW_RESIZED:
		case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
			a.emplace_back((double) e.window.data1);
			a.emplace_back((double) e.window.data2);
			return new Message("resize", a);

		case SDL_EVENT_WINDOW_FOCUS_GAINED:
		case SDL_EVENT_WINDOW_FOCUS_LOST:
			a.emplace_back(e.type == SDL_EVENT_WINDOW_FOCUS_GAINED);
			return new Message("focus", a);

		default:
			return nullptr;
		}
	}

}; // HarnessEvent

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
		inst = new HarnessEvent();
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
