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

// mruby port of wrap_Touch.cpp. Exposes Love::Touch with snake_case keyword
// methods. The real SDL touch backend is used.
//
// Touch ids: the Lua API returned them as lightuserdata to dodge the 2^53
// precision limit of a Lua double. mruby Integers here are 64-bit (word
// boxing), which holds an SDL touch id exactly, so ids are plain Integers.

#include "common/config.h"
#include "common/mrb_runtime.h"
#include "common/Module.h"
#include "common/int.h"

#include "Touch.h"
#include "sdl/Touch.h"

namespace love
{
namespace touch
{

#define instance() (Module::getInstance<Touch>(Module::M_TOUCH))

static int64 checkTouchId(mrb_state *mrb, mrb_value v)
{
	if (!mrb_integer_p(v))
		mrb_raise(mrb, E_TYPE_ERROR, "touch id must be an Integer (as returned by get_touches)");
	return (int64) mrb_integer(v);
}

// get_touches(device_type: <opt filter>) -> Array of touch-id Integers.
static mrb_value w_getTouches(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[1];
	mrbx_get_kwargs(mrb, {"device_type"}, 0, v);

	bool hasFilter = false;
	Touch::DeviceType filter = Touch::DEVICE_MAX_ENUM;
	if (!mrb_undef_p(v[0]) && !mrb_nil_p(v[0]))
	{
		std::string ts = mrbx_checkstring(mrb, v[0]);
		if (!Touch::getConstant(ts.c_str(), filter))
			mrb_raisef(mrb, E_ARGUMENT_ERROR, "invalid touch device type: %s", ts.c_str());
		hasFilter = true;
	}

	const std::vector<Touch::TouchInfo> &touches = instance()->getTouches();
	mrb_value arr = mrb_ary_new(mrb);
	for (const Touch::TouchInfo &touch : touches)
	{
		if (!hasFilter || filter == touch.deviceType)
			mrb_ary_push(mrb, arr, mrb_int_value(mrb, (mrb_int) touch.id));
	}
	return arr;
}

static mrb_value w_getPosition(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[1];
	mrbx_get_kwargs(mrb, {"id"}, 1, v);
	int64 id = checkTouchId(mrb, v[0]);

	Touch::TouchInfo touch = {};
	if (mrbx_catchexcept(mrb, [&]() { touch = instance()->getTouch(id); }))
		return mrb_nil_value();

	mrb_value h = mrb_hash_new(mrb);
	mrb_hash_set(mrb, h, mrb_symbol_value(mrb_intern_lit(mrb, "x")), mrbx_number(mrb, touch.x));
	mrb_hash_set(mrb, h, mrb_symbol_value(mrb_intern_lit(mrb, "y")), mrbx_number(mrb, touch.y));
	return h;
}

static mrb_value w_getPressure(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[1];
	mrbx_get_kwargs(mrb, {"id"}, 1, v);
	int64 id = checkTouchId(mrb, v[0]);

	Touch::TouchInfo touch = {};
	if (mrbx_catchexcept(mrb, [&]() { touch = instance()->getTouch(id); }))
		return mrb_nil_value();
	return mrbx_number(mrb, touch.pressure);
}

static mrb_value w_getDeviceType(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[1];
	mrbx_get_kwargs(mrb, {"id"}, 1, v);
	int64 id = checkTouchId(mrb, v[0]);

	Touch::TouchInfo touch = {};
	if (mrbx_catchexcept(mrb, [&]() { touch = instance()->getTouch(id); }))
		return mrb_nil_value();

	const char *typestr = nullptr;
	if (!Touch::getConstant(touch.deviceType, typestr))
		mrb_raise(mrb, E_RUNTIME_ERROR, "Unknown touch device type.");
	return mrbx_string(mrb, typestr);
}

static mrb_value w_isMouse(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[1];
	mrbx_get_kwargs(mrb, {"id"}, 1, v);
	int64 id = checkTouchId(mrb, v[0]);

	Touch::TouchInfo touch = {};
	if (mrbx_catchexcept(mrb, [&]() { touch = instance()->getTouch(id); }))
		return mrb_nil_value();
	return mrbx_boolean(mrb, touch.mouse);
}

static const MrbReg functions[] =
{
	{ "get_touches",     w_getTouches,    MRB_ARGS_KEY(1, 0) },
	{ "get_position",    w_getPosition,   MRB_ARGS_KEY(1, 0) },
	{ "get_pressure",    w_getPressure,   MRB_ARGS_KEY(1, 0) },
	{ "get_device_type", w_getDeviceType, MRB_ARGS_KEY(1, 0) },
	{ "mouse?",          w_isMouse,       MRB_ARGS_KEY(1, 0) },
	{ nullptr, nullptr, 0 }
};

extern "C" void mrb_love_touch_init(mrb_state *mrb)
{
	Touch *inst = instance();
	if (inst == nullptr)
		inst = new love::touch::sdl::Touch();
	else
		inst->retain();

	WrappedModule w;
	w.module = inst;
	w.name = "Touch";
	w.type = &Module::type;
	w.functions = functions;

	mrbx_register_module(mrb, w);
}

} // touch
} // love
