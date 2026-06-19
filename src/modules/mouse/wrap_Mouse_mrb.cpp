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
// Backend note: like the keyboard module, this is a LEAN backend. The real
// mouse/sdl/Mouse.cpp depends on the Cursor object type and the image module
// (newCursor takes ImageData). HarnessMouse is instead a plain love::Module
// that drives SDL's mouse state directly. The cursor object family
// (new_cursor / get_system_cursor / set_cursor / get_cursor) is DEFERRED until
// the image module and a Cursor Type are ported -- symmetric with how the lean
// window backend deferred set_icon/get_icon for the same reason. Everything
// else (position, buttons, visibility, grab, relative mode) is here.

#include "common/config.h"
#include "common/mrb_runtime.h"
#include "common/Module.h"
#include "window/Window.h"

#include <SDL3/SDL.h>

namespace love
{
namespace mouse
{

// TODO(mruby) #mouse-backend: lean backend — plain Module driving SDL state
// directly instead of the real Mouse base (needs Cursor + image); swap for
// mouse/sdl/Mouse.cpp (see PORTING.md §B)
class HarnessMouse : public love::Module
{
public:

	HarnessMouse()
		: love::Module(M_MOUSE, "love.mouse.harness")
	{
	}

	void getPosition(double &x, double &y) const
	{
		float mx = 0.0f, my = 0.0f;
		SDL_GetMouseState(&mx, &my);
		x = (double) mx;
		y = (double) my;
	}

	void setPosition(double x, double y)
	{
		SDL_WarpMouseInWindow(windowHandle(), (float) x, (float) y);
		// Warp doesn't update SDL's internal state immediately on some
		// platforms; pump so the next getPosition reflects the new spot.
		SDL_PumpEvents();
	}

	void getGlobalPosition(double &x, double &y, int &displayindex) const
	{
		float gx = 0.0f, gy = 0.0f;
		SDL_GetGlobalMouseState(&gx, &gy);

		double mx = gx, my = gy;
		int count = 0;
		SDL_DisplayID *displays = SDL_GetDisplays(&count);

		for (displayindex = 0; displayindex < count; displayindex++)
		{
			SDL_Rect r = {};
			SDL_GetDisplayBounds(displays[displayindex], &r);
			SDL_FPoint p = {gx, gy};
			SDL_FRect frect = {(float) r.x, (float) r.y, (float) r.w, (float) r.h};
			mx = gx - r.x;
			my = gy - r.y;
			if (SDL_PointInRectFloat(&p, &frect))
				break;
		}

		if (displays != nullptr)
			SDL_free(displays);
		if (displayindex >= count)
			displayindex = 0;

		x = mx;
		y = my;
	}

	// LÖVE button index 2 is the RIGHT button and 3 is MIDDLE; SDL is the
	// reverse. Translate before testing the state mask.
	bool isDown(int button) const
	{
		if (button <= 0)
			return false;
		switch (button)
		{
		case 2: button = SDL_BUTTON_RIGHT;  break;
		case 3: button = SDL_BUTTON_MIDDLE; break;
		}
		Uint32 state = SDL_GetMouseState(nullptr, nullptr);
		return (state & SDL_BUTTON_MASK(button)) != 0;
	}

	void setVisible(bool visible)
	{
		if (visible)
			SDL_ShowCursor();
		else
			SDL_HideCursor();
	}

	bool isVisible() const { return SDL_CursorVisible(); }

	bool isCursorSupported() const { return SDL_GetDefaultCursor() != nullptr; }

	void setGrabbed(bool grab)
	{
		SDL_Window *w = windowHandle();
		if (w != nullptr)
			SDL_SetWindowMouseGrab(w, grab);
	}

	bool isGrabbed() const
	{
		SDL_Window *w = windowHandle();
		return w != nullptr && SDL_GetWindowMouseGrab(w);
	}

	bool setRelativeMode(bool relative)
	{
		SDL_Window *w = windowHandle();
		return w != nullptr && SDL_SetWindowRelativeMouseMode(w, relative);
	}

	bool getRelativeMode() const
	{
		SDL_Window *w = windowHandle();
		return w != nullptr && SDL_GetWindowRelativeMouseMode(w);
	}

private:

	static SDL_Window *windowHandle()
	{
		auto win = Module::getInstance<love::window::Window>(M_WINDOW);
		return win != nullptr ? (SDL_Window *) win->getHandle() : nullptr;
	}

}; // HarnessMouse

#define instance() (Module::getInstance<HarnessMouse>(Module::M_MOUSE))

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

	bool down = false;
	if (mrb_array_p(v[0]))
	{
		mrb_int n = RARRAY_LEN(v[0]);
		for (mrb_int i = 0; i < n && !down; i++)
			down = instance()->isDown((int) mrbx_checkint(mrb, mrb_ary_ref(mrb, v[0], i)));
	}
	else
	{
		down = instance()->isDown((int) mrbx_checkint(mrb, v[0]));
	}
	return mrbx_boolean(mrb, down);
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
	// TODO(mruby) #mouse-cursor: cursor object family deferred — new_cursor (needs
	// image), get_system_cursor, set_cursor, get_cursor (need a Cursor Type). §A
	{ nullptr, nullptr, 0 }
};

extern "C" void mrb_love_mouse_init(mrb_state *mrb)
{
	Module *inst = Module::getInstance<Module>(Module::M_MOUSE);
	if (inst == nullptr)
		inst = new HarnessMouse();
	else
		inst->retain();

	WrappedModule w;
	w.module = inst;
	w.name = "Mouse";
	w.type = &Module::type;
	w.functions = functions;

	mrbx_register_module(mrb, w);
}

} // mouse
} // love
