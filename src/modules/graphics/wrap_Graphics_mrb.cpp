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

// mruby port of love.graphics.
//
// This now drives the REAL shader-based batched renderer: the module instance
// at M_GRAPHICS is a love::graphics::Graphics created via Graphics::createInstance()
// (OpenGL backend in this harness; Vulkan/Metal are out of the build, see
// LOVE_MRUBY_NO_VULKAN). The renderer context is created by the real SDL window
// backend's setMode() (window/sdl/Window.cpp), which resolves this instance from
// M_GRAPHICS and calls setMode()/backbufferChanged() on it -- so graphics and
// window are coupled and come up together.
//
// The Ruby-facing API is still a thin first slice over the real backend --
//
//   active?, clear, set_color / set_background_color, rectangle, origin, present
//
// -- exercising the real batched-draw path (a rectangle goes through the default
// shader and the streaming vertex buffer). Textures, shaders, transforms beyond
// origin, blend/stencil state, fonts, and the object types (Image, Quad,
// SpriteBatch, Mesh, ...) are still to be exposed; the binding will grow onto
// the same real Graphics instance.

#include "common/config.h"
#include "common/mrb_runtime.h"
#include "common/Module.h"
#include "common/Color.h"
#include "common/Optional.h"
#include "Graphics.h"

namespace love
{
namespace graphics
{

#define instance() (Module::getInstance<Graphics>(Module::M_GRAPHICS))

// =========================================================================
// Love::Graphics module functions
// =========================================================================

static mrb_value w_active(mrb_state *mrb, mrb_value self)
{
	(void) self;
	return mrbx_boolean(mrb, instance()->isActive());
}

static mrb_value w_clear(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[4];
	mrbx_get_kwargs(mrb, {"r", "g", "b", "a"}, 0, v);

	Colorf bg = instance()->getBackgroundColor();
	ColorD c;
	c.r = mrbx_optfloat(mrb, v[0], bg.r);
	c.g = mrbx_optfloat(mrb, v[1], bg.g);
	c.b = mrbx_optfloat(mrb, v[2], bg.b);
	c.a = mrbx_optfloat(mrb, v[3], bg.a);

	mrbx_catchexcept(mrb, [&]() {
		instance()->clear(OptionalColorD(c), OptionalInt(), OptionalDouble());
	});
	return mrb_nil_value();
}

static mrb_value w_set_color(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[4];
	mrbx_get_kwargs(mrb, {"r", "g", "b", "a"}, 3, v);
	Colorf c(mrbx_checkfloat(mrb, v[0]), mrbx_checkfloat(mrb, v[1]),
		mrbx_checkfloat(mrb, v[2]), mrbx_optfloat(mrb, v[3], 1.0f));
	instance()->setColor(c);
	return mrb_nil_value();
}

static mrb_value w_set_background_color(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[4];
	mrbx_get_kwargs(mrb, {"r", "g", "b", "a"}, 3, v);
	Colorf c(mrbx_checkfloat(mrb, v[0]), mrbx_checkfloat(mrb, v[1]),
		mrbx_checkfloat(mrb, v[2]), mrbx_optfloat(mrb, v[3], 1.0f));
	instance()->setBackgroundColor(c);
	return mrb_nil_value();
}

static mrb_value colorhash(mrb_state *mrb, Colorf c)
{
	mrb_value h = mrb_hash_new(mrb);
	mrb_hash_set(mrb, h, mrb_symbol_value(mrb_intern_cstr(mrb, "r")), mrbx_number(mrb, c.r));
	mrb_hash_set(mrb, h, mrb_symbol_value(mrb_intern_cstr(mrb, "g")), mrbx_number(mrb, c.g));
	mrb_hash_set(mrb, h, mrb_symbol_value(mrb_intern_cstr(mrb, "b")), mrbx_number(mrb, c.b));
	mrb_hash_set(mrb, h, mrb_symbol_value(mrb_intern_cstr(mrb, "a")), mrbx_number(mrb, c.a));
	return h;
}

static mrb_value w_get_color(mrb_state *mrb, mrb_value self)
{
	(void) self;
	return colorhash(mrb, instance()->getColor());
}

static mrb_value w_get_background_color(mrb_state *mrb, mrb_value self)
{
	(void) self;
	return colorhash(mrb, instance()->getBackgroundColor());
}

static mrb_value w_rectangle(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[5];
	mrbx_get_kwargs(mrb, {"mode", "x", "y", "width", "height"}, 5, v);

	std::string mode = mrbx_checkstring(mrb, v[0]);
	Graphics::DrawMode drawmode;
	if (mode == "fill")
		drawmode = Graphics::DRAW_FILL;
	else if (mode == "line")
		drawmode = Graphics::DRAW_LINE;
	else
	{
		mrb_raisef(mrb, E_ARGUMENT_ERROR, "Invalid draw mode: %s (expected 'fill' or 'line')", mode.c_str());
		return mrb_nil_value();
	}

	mrbx_catchexcept(mrb, [&]() {
		instance()->rectangle(drawmode, mrbx_checkfloat(mrb, v[1]), mrbx_checkfloat(mrb, v[2]),
			mrbx_checkfloat(mrb, v[3]), mrbx_checkfloat(mrb, v[4]));
	});
	return mrb_nil_value();
}

static mrb_value w_origin(mrb_state *mrb, mrb_value self)
{
	(void) self;
	instance()->origin();
	return mrb_nil_value();
}

static mrb_value w_present(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrbx_catchexcept(mrb, [&]() { instance()->present(nullptr); });
	return mrb_nil_value();
}

static mrb_value w_get_width(mrb_state *mrb, mrb_value self)
{
	(void) self;
	return mrbx_integer(mrb, instance()->getWidth());
}

static mrb_value w_get_height(mrb_state *mrb, mrb_value self)
{
	(void) self;
	return mrbx_integer(mrb, instance()->getHeight());
}

static mrb_value w_get_dimensions(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value arr = mrb_ary_new_capa(mrb, 2);
	mrb_ary_push(mrb, arr, mrbx_integer(mrb, instance()->getWidth()));
	mrb_ary_push(mrb, arr, mrbx_integer(mrb, instance()->getHeight()));
	return arr;
}

static const MrbReg functions[] =
{
	{ "active?",              w_active,               MRB_ARGS_NONE() },
	{ "clear",                w_clear,                MRB_ARGS_KEY(4, 0) },
	{ "set_color",            w_set_color,            MRB_ARGS_KEY(4, 0) },
	{ "get_color",            w_get_color,            MRB_ARGS_NONE() },
	{ "set_background_color", w_set_background_color, MRB_ARGS_KEY(4, 0) },
	{ "get_background_color", w_get_background_color, MRB_ARGS_NONE() },
	{ "rectangle",            w_rectangle,            MRB_ARGS_KEY(5, 0) },
	{ "origin",               w_origin,               MRB_ARGS_NONE() },
	{ "present",              w_present,              MRB_ARGS_NONE() },
	{ "get_width",            w_get_width,            MRB_ARGS_NONE() },
	{ "get_height",           w_get_height,           MRB_ARGS_NONE() },
	{ "get_dimensions",       w_get_dimensions,       MRB_ARGS_NONE() },
	{ nullptr, nullptr, 0 }
};

extern "C" void mrb_love_graphics_init(mrb_state *mrb)
{
	// The real renderer-backed instance. createInstance() picks the renderer
	// (OpenGL here) and registers itself at M_GRAPHICS; the window backend's
	// setMode() later creates the GL context and drives setMode() on it.
	Graphics *inst = Module::getInstance<Graphics>(Module::M_GRAPHICS);
	if (inst == nullptr)
		inst = Graphics::createInstance();
	else
		inst->retain();

	WrappedModule w;
	w.module = inst;
	w.name = "Graphics";
	w.type = &Module::type;
	w.functions = functions;

	mrbx_register_module(mrb, w);
}

} // graphics
} // love
