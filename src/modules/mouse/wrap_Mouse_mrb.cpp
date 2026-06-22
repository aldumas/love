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

// mruby port of wrap_Mouse.cpp.
//
// API shape: a Love::Mouse module, snake_case names, keyword arguments. Lua
// predicate names map to Ruby `?` methods:
//
//   love.mouse.isDown(1, 2)          -> Love::Mouse.down?(button: [1, 2])
//   love.mouse.getPosition()         -> Love::Mouse.get_position  # {x:, y:}
//   love.mouse.isVisible()           -> Love::Mouse.visible?
//
// Backend note: this now drives the REAL love::mouse::sdl::Mouse backend
// (mouse/Mouse.cpp + mouse/sdl/Mouse.cpp). The Ruby bindings are unchanged --
// every binding calls through the abstract love::mouse::Mouse interface, which
// the SDL backend implements (position/buttons/visibility/grab/relative mode +
// the cursor object family via the real love::mouse::sdl::Cursor).

#include "common/config.h"
#include "common/mrb_runtime.h"
#include "common/Module.h"
#include "image/ImageData.h"
#include "Mouse.h"
#include "sdl/Mouse.h"
#include "sdl/Cursor.h"

#include <vector>

namespace love
{
namespace mouse
{

#define instance() (Module::getInstance<love::mouse::Mouse>(Module::M_MOUSE))

// =========================================================================
// Love::Mouse module functions
// =========================================================================

static void hset(mrb_state *mrb, mrb_value h, const char *key, mrb_value v)
{
	mrb_hash_set(mrb, h, mrb_symbol_value(mrb_intern_cstr(mrb, key)), v);
}

static mrb_value w_get_x(mrb_state *mrb, mrb_value self)
{
	(void) self;
	double x, y;
	instance()->getPosition(x, y);
	return mrbx_number(mrb, x);
}

static mrb_value w_get_y(mrb_state *mrb, mrb_value self)
{
	(void) self;
	double x, y;
	instance()->getPosition(x, y);
	return mrbx_number(mrb, y);
}

static mrb_value w_get_position(mrb_state *mrb, mrb_value self)
{
	(void) self;
	double x, y;
	instance()->getPosition(x, y);
	mrb_value h = mrb_hash_new(mrb);
	hset(mrb, h, "x", mrbx_number(mrb, x));
	hset(mrb, h, "y", mrbx_number(mrb, y));
	return h;
}

static mrb_value w_set_x(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[1];
	mrbx_get_kwargs(mrb, {"x"}, 1, v);
	double x, y;
	instance()->getPosition(x, y);
	instance()->setPosition(mrbx_checknumber(mrb, v[0]), y);
	return mrb_nil_value();
}

static mrb_value w_set_y(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[1];
	mrbx_get_kwargs(mrb, {"y"}, 1, v);
	double x, y;
	instance()->getPosition(x, y);
	instance()->setPosition(x, mrbx_checknumber(mrb, v[0]));
	return mrb_nil_value();
}

static mrb_value w_set_position(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[2];
	mrbx_get_kwargs(mrb, {"x", "y"}, 2, v);
	instance()->setPosition(mrbx_checknumber(mrb, v[0]), mrbx_checknumber(mrb, v[1]));
	return mrb_nil_value();
}

static mrb_value w_get_global_position(mrb_state *mrb, mrb_value self)
{
	(void) self;
	double x, y;
	int display;
	instance()->getGlobalPosition(x, y, display);
	mrb_value h = mrb_hash_new(mrb);
	hset(mrb, h, "x", mrbx_number(mrb, x));
	hset(mrb, h, "y", mrbx_number(mrb, y));
	hset(mrb, h, "display", mrbx_integer(mrb, display + 1)); // 1-based in Ruby
	return h;
}

static mrb_value w_down(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[1];
	mrbx_get_kwargs(mrb, {"button"}, 1, v);

	std::vector<int> buttons;
	if (mrb_array_p(v[0]))
	{
		mrb_int n = RARRAY_LEN(v[0]);
		for (mrb_int i = 0; i < n; i++)
			buttons.push_back((int) mrbx_checkint(mrb, mrb_ary_ref(mrb, v[0], i)));
	}
	else
		buttons.push_back((int) mrbx_checkint(mrb, v[0]));
	return mrbx_boolean(mrb, instance()->isDown(buttons));
}

static mrb_value w_set_visible(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[1];
	mrbx_get_kwargs(mrb, {"visible"}, 1, v);
	instance()->setVisible(mrbx_checkboolean(mrb, v[0]));
	return mrb_nil_value();
}

static mrb_value w_visible(mrb_state *mrb, mrb_value self)
{
	(void) self;
	return mrbx_boolean(mrb, instance()->isVisible());
}

static mrb_value w_cursor_supported(mrb_state *mrb, mrb_value self)
{
	(void) self;
	return mrbx_boolean(mrb, instance()->isCursorSupported());
}

static mrb_value w_set_grabbed(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[1];
	mrbx_get_kwargs(mrb, {"grabbed"}, 1, v);
	instance()->setGrabbed(mrbx_checkboolean(mrb, v[0]));
	return mrb_nil_value();
}

static mrb_value w_grabbed(mrb_state *mrb, mrb_value self)
{
	(void) self;
	return mrbx_boolean(mrb, instance()->isGrabbed());
}

static mrb_value w_set_relative_mode(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[1];
	mrbx_get_kwargs(mrb, {"enable"}, 1, v);
	return mrbx_boolean(mrb, instance()->setRelativeMode(mrbx_checkboolean(mrb, v[0])));
}

static mrb_value w_relative_mode(mrb_state *mrb, mrb_value self)
{
	(void) self;
	return mrbx_boolean(mrb, instance()->getRelativeMode());
}

// Love::Mouse.new_cursor(image_data:, hotx: 0, hoty: 0) — image_data is a single
// ImageData or an Array of them (alternate DPI representations).
static mrb_value w_new_cursor(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[3];
	mrbx_get_kwargs(mrb, {"image_data", "hotx", "hoty"}, 1, v);

	std::vector<image::ImageData *> data;
	if (mrb_array_p(v[0]))
	{
		mrb_int n = RARRAY_LEN(v[0]);
		for (mrb_int i = 0; i < n; i++)
			data.push_back(mrbx_checktype<image::ImageData>(mrb, mrb_ary_ref(mrb, v[0], i)));
	}
	else
		data.push_back(mrbx_checktype<image::ImageData>(mrb, v[0]));

	int hotx = mrbx_optint(mrb, v[1], 0);
	int hoty = mrbx_optint(mrb, v[2], 0);

	Cursor *cursor = nullptr;
	if (mrbx_catchexcept(mrb, [&]() { cursor = instance()->newCursor(data, hotx, hoty); }))
		return mrb_nil_value();

	mrb_value r = mrbx_pushtype(mrb, cursor);
	cursor->release();
	return r;
}

static mrb_value w_get_system_cursor(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[1];
	mrbx_get_kwargs(mrb, {"type"}, 1, v);

	std::string str = mrbx_checkstring(mrb, v[0]);
	Cursor::SystemCursor systemCursor;
	if (!Cursor::getConstant(str.c_str(), systemCursor))
		mrb_raisef(mrb, E_ARGUMENT_ERROR, "invalid system cursor type: %s", str.c_str());

	Cursor *cursor = nullptr;
	if (mrbx_catchexcept(mrb, [&]() { cursor = instance()->getSystemCursor(systemCursor); }))
		return mrb_nil_value();
	return mrbx_pushtype(mrb, cursor);
}

// Love::Mouse.set_cursor(cursor: <Cursor>) — omit/nil reverts to the default.
static mrb_value w_set_cursor(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[1];
	mrbx_get_kwargs(mrb, {"cursor"}, 0, v);

	if (mrb_undef_p(v[0]) || mrb_nil_p(v[0]))
		instance()->setCursor();
	else
		instance()->setCursor(mrbx_checktype<Cursor>(mrb, v[0]));
	return mrb_nil_value();
}

static mrb_value w_get_cursor(mrb_state *mrb, mrb_value self)
{
	(void) self;
	return mrbx_pushtype(mrb, instance()->getCursor());
}

static const MrbReg functions[] =
{
	{ "get_x",                w_get_x,               MRB_ARGS_NONE() },
	{ "get_y",                w_get_y,               MRB_ARGS_NONE() },
	{ "get_position",         w_get_position,        MRB_ARGS_NONE() },
	{ "set_x",                w_set_x,               MRB_ARGS_KEY(1, 0) },
	{ "set_y",                w_set_y,               MRB_ARGS_KEY(1, 0) },
	{ "set_position",         w_set_position,        MRB_ARGS_KEY(2, 0) },
	{ "get_global_position",  w_get_global_position, MRB_ARGS_NONE() },
	{ "down?",                w_down,                MRB_ARGS_KEY(1, 0) },
	{ "set_visible",          w_set_visible,         MRB_ARGS_KEY(1, 0) },
	{ "visible?",             w_visible,             MRB_ARGS_NONE() },
	{ "cursor_supported?",    w_cursor_supported,    MRB_ARGS_NONE() },
	{ "set_grabbed",          w_set_grabbed,         MRB_ARGS_KEY(1, 0) },
	{ "grabbed?",             w_grabbed,             MRB_ARGS_NONE() },
	{ "set_relative_mode",    w_set_relative_mode,   MRB_ARGS_KEY(1, 0) },
	{ "relative_mode?",       w_relative_mode,       MRB_ARGS_NONE() },
	{ "new_cursor",           w_new_cursor,          MRB_ARGS_KEY(3, 0) },
	{ "get_system_cursor",    w_get_system_cursor,   MRB_ARGS_KEY(1, 0) },
	{ "set_cursor",           w_set_cursor,          MRB_ARGS_KEY(1, 0) },
	{ "get_cursor",           w_get_cursor,          MRB_ARGS_NONE() },
	{ nullptr, nullptr, 0 }
};

// --- Love::Cursor object type --------------------------------------------

static mrb_value cursor_getType(mrb_state *mrb, mrb_value self)
{
	Cursor *cursor = mrbx_checktype<Cursor>(mrb, self);
	Cursor::CursorType ctype = cursor->getType();
	const char *typestr = nullptr;

	if (ctype == Cursor::CURSORTYPE_IMAGE)
		Cursor::getConstant(ctype, typestr);
	else if (ctype == Cursor::CURSORTYPE_SYSTEM)
		Cursor::getConstant(cursor->getSystemType(), typestr);

	if (typestr == nullptr)
		mrb_raise(mrb, E_RUNTIME_ERROR, "Unknown cursor type.");
	return mrbx_string(mrb, typestr);
}

static const MrbReg cursorFunctions[] =
{
	{ "get_type", cursor_getType, MRB_ARGS_NONE() },
	{ nullptr, nullptr, 0 }
};

extern "C" void mrb_love_mouse_init(mrb_state *mrb)
{
	Module *inst = Module::getInstance<Module>(Module::M_MOUSE);
	if (inst == nullptr)
		inst = new sdl::Mouse();
	else
		inst->retain();

	WrappedModule w;
	w.module = inst;
	w.name = "Mouse";
	w.type = &Module::type;
	w.functions = functions;

	mrbx_register_module(mrb, w);
	mrbx_register_type(mrb, Cursor::type, cursorFunctions);
}

} // mouse
} // love
