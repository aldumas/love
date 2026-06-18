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

// mruby port of wrap_Timer.cpp.
//
// API shape: methods live on the Love::Timer module, use snake_case names, and
// take keyword arguments instead of positional parameters. For example, the
// Lua `love.timer.sleep(0.5)` becomes `Love::Timer.sleep(seconds: 0.5)`.

#include "common/config.h"
#include "common/mrb_runtime.h"
#include "Timer.h"

namespace love
{
namespace timer
{

#define instance() (Module::getInstance<Timer>(Module::M_TIMER))

// Love::Timer.step  -> returns the time since the last step (seconds).
static mrb_value w_step(mrb_state *mrb, mrb_value self)
{
	(void) self;
	return mrbx_number(mrb, instance()->step());
}

// Love::Timer.get_delta -> seconds since the last update.
static mrb_value w_getDelta(mrb_state *mrb, mrb_value self)
{
	(void) self;
	return mrbx_number(mrb, instance()->getDelta());
}

// Love::Timer.get_fps -> current frames per second (integer).
static mrb_value w_getFPS(mrb_state *mrb, mrb_value self)
{
	(void) self;
	return mrbx_integer(mrb, instance()->getFPS());
}

// Love::Timer.get_average_delta -> average delta over the last second.
static mrb_value w_getAverageDelta(mrb_state *mrb, mrb_value self)
{
	(void) self;
	return mrbx_number(mrb, instance()->getAverageDelta());
}

// Love::Timer.sleep(seconds:) -> pauses for the given number of seconds.
static mrb_value w_sleep(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[1];
	mrbx_get_kwargs(mrb, {"seconds"}, 1, v);
	instance()->sleep(mrbx_checknumber(mrb, v[0]));
	return mrb_nil_value();
}

// Love::Timer.get_time -> time in seconds since some unspecified epoch.
static mrb_value w_getTime(mrb_state *mrb, mrb_value self)
{
	(void) self;
	return mrbx_number(mrb, instance()->getTime());
}

static const MrbReg functions[] =
{
	{ "step",              w_step,             MRB_ARGS_NONE() },
	{ "get_delta",         w_getDelta,         MRB_ARGS_NONE() },
	{ "get_fps",           w_getFPS,           MRB_ARGS_NONE() },
	{ "get_average_delta", w_getAverageDelta,  MRB_ARGS_NONE() },
	{ "sleep",             w_sleep,            MRB_ARGS_KEY(1, 0) },
	{ "get_time",          w_getTime,          MRB_ARGS_NONE() },
	{ nullptr, nullptr, 0 }
};

// Equivalent of luaopen_love_timer: creates the module instance if needed and
// registers Love::Timer.
extern "C" void mrb_love_timer_init(mrb_state *mrb)
{
	Timer *inst = instance();
	if (inst == nullptr)
		inst = new love::timer::Timer();
	else
		inst->retain();

	WrappedModule w;
	w.module = inst;
	w.name = "Timer";
	w.type = &Module::type;
	w.functions = functions;

	mrbx_register_module(mrb, w);
}

} // timer
} // love
