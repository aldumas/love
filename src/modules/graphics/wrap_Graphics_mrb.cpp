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

// mruby port of love.graphics -- a deliberately THIN first slice.
//
// The real love.graphics is ~8000 lines across three renderer backends
// (opengl/vulkan/metal) plus shaders, batched rendering, textures, fonts, and a
// full render-state stack. Linking any of it would pull in that entire tree.
//
// Following the lean-backend pattern used for window and event, this provides a
// standalone HarnessGraphics that is NOT love::graphics::Graphics: it's a plain
// love::Module that creates a legacy OpenGL context on the (lean) window and
// implements just enough to put pixels on screen --
//
//   clear, set_color / set_background_color, rectangle, origin, present
//
// using fixed-function immediate-mode GL (no shader pipeline, no GL loader).
// Colors are 0..1 floats and the coordinate space is top-left origin / y-down,
// matching modern LÖVE. This is the "draw a rectangle" milestone; swap it for
// the real backend once the graphics object/shader system is ported. The
// Ruby-facing API (Love::Graphics.clear, .rectangle, ...) is the shape the full
// module will keep.

#include "common/config.h"
#include "common/mrb_runtime.h"
#include "common/Module.h"
#include "window/Window.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>

namespace love
{
namespace graphics
{

// =========================================================================
// Lean immediate-mode OpenGL backend
// =========================================================================

class HarnessGraphics : public love::Module
{
public:

	HarnessGraphics()
		: love::Module(M_GRAPHICS, "love.graphics.harness")
	{
	}

	~HarnessGraphics() override
	{
		if (glcontext != nullptr)
			SDL_GL_DestroyContext(glcontext);
	}

	// Lazily create the GL context on the window. Returns true once a usable
	// context is current. Mirrors love.graphics.isActive(): false when there's
	// no window or context creation failed.
	bool active()
	{
		if (glcontext != nullptr)
			return true;
		if (contextFailed)
			return false;

		SDL_Window *w = windowHandle();
		if (w == nullptr)
			return false;

		// A compatibility profile keeps fixed-function immediate mode available.
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);

		glcontext = SDL_GL_CreateContext(w);
		if (glcontext == nullptr)
		{
			SDL_Log("love.graphics: could not create GL context (%s)", SDL_GetError());
			contextFailed = true;
			return false;
		}

		SDL_GL_MakeCurrent(w, glcontext);
		SDL_GL_SetSwapInterval(1); // vsync; frame-paces the boot loop
		return true;
	}

	void setColor(float r, float g, float b, float a)      { color[0]=r; color[1]=g; color[2]=b; color[3]=a; }
	void setBackgroundColor(float r, float g, float b, float a) { bg[0]=r; bg[1]=g; bg[2]=b; bg[3]=a; }
	const float *getColor() const { return color; }
	const float *getBackground() const { return bg; }

	// Set up the viewport + a top-left-origin orthographic projection for the
	// window's current pixel size. Called at the start of each frame (clear).
	void beginFrame()
	{
		int w = 0, h = 0;
		pixelDimensions(w, h);
		glViewport(0, 0, w, h);
		glMatrixMode(GL_PROJECTION);
		glLoadIdentity();
		glOrtho(0.0, (GLdouble) w, (GLdouble) h, 0.0, -1.0, 1.0);
		glMatrixMode(GL_MODELVIEW);
		glLoadIdentity();
	}

	void clear(float r, float g, float b, float a)
	{
		beginFrame();
		glClearColor(r, g, b, a);
		glClear(GL_COLOR_BUFFER_BIT);
	}

	void origin()
	{
		glMatrixMode(GL_MODELVIEW);
		glLoadIdentity();
	}

	void rectangle(bool fill, float x, float y, float w, float h)
	{
		glColor4f(color[0], color[1], color[2], color[3]);
		glBegin(fill ? GL_QUADS : GL_LINE_LOOP);
			glVertex2f(x,     y);
			glVertex2f(x + w, y);
			glVertex2f(x + w, y + h);
			glVertex2f(x,     y + h);
		glEnd();
	}

	void present()
	{
		SDL_Window *w = windowHandle();
		if (w != nullptr)
			SDL_GL_SwapWindow(w);
	}

	void getDimensions(int &w, int &h) { pixelDimensions(w, h); }

private:

	static SDL_Window *windowHandle()
	{
		auto win = Module::getInstance<love::window::Window>(M_WINDOW);
		return win != nullptr ? (SDL_Window *) win->getHandle() : nullptr;
	}

	static void pixelDimensions(int &w, int &h)
	{
		w = h = 0;
		SDL_Window *win = windowHandle();
		if (win != nullptr && !SDL_GetWindowSizeInPixels(win, &w, &h))
			SDL_GetWindowSize(win, &w, &h);
	}

	SDL_GLContext glcontext = nullptr;
	bool contextFailed = false;
	float color[4] = {1.0f, 1.0f, 1.0f, 1.0f};
	float bg[4]    = {0.0f, 0.0f, 0.0f, 1.0f};

}; // HarnessGraphics

#define instance() (Module::getInstance<HarnessGraphics>(Module::M_GRAPHICS))

// =========================================================================
// Love::Graphics module functions
// =========================================================================

static mrb_value w_active(mrb_state *mrb, mrb_value self)
{
	(void) self;
	return mrbx_boolean(mrb, instance()->active());
}

static mrb_value w_clear(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[4];
	mrbx_get_kwargs(mrb, {"r", "g", "b", "a"}, 0, v);

	const float *bg = instance()->getBackground();
	float r = mrbx_optfloat(mrb, v[0], bg[0]);
	float g = mrbx_optfloat(mrb, v[1], bg[1]);
	float b = mrbx_optfloat(mrb, v[2], bg[2]);
	float a = mrbx_optfloat(mrb, v[3], bg[3]);

	instance()->clear(r, g, b, a);
	return mrb_nil_value();
}

static mrb_value w_set_color(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[4];
	mrbx_get_kwargs(mrb, {"r", "g", "b", "a"}, 3, v);
	instance()->setColor(mrbx_checkfloat(mrb, v[0]), mrbx_checkfloat(mrb, v[1]),
		mrbx_checkfloat(mrb, v[2]), mrbx_optfloat(mrb, v[3], 1.0f));
	return mrb_nil_value();
}

static mrb_value w_set_background_color(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[4];
	mrbx_get_kwargs(mrb, {"r", "g", "b", "a"}, 3, v);
	instance()->setBackgroundColor(mrbx_checkfloat(mrb, v[0]), mrbx_checkfloat(mrb, v[1]),
		mrbx_checkfloat(mrb, v[2]), mrbx_optfloat(mrb, v[3], 1.0f));
	return mrb_nil_value();
}

static mrb_value colorhash(mrb_state *mrb, const float *c)
{
	mrb_value h = mrb_hash_new(mrb);
	mrb_hash_set(mrb, h, mrb_symbol_value(mrb_intern_cstr(mrb, "r")), mrbx_number(mrb, c[0]));
	mrb_hash_set(mrb, h, mrb_symbol_value(mrb_intern_cstr(mrb, "g")), mrbx_number(mrb, c[1]));
	mrb_hash_set(mrb, h, mrb_symbol_value(mrb_intern_cstr(mrb, "b")), mrbx_number(mrb, c[2]));
	mrb_hash_set(mrb, h, mrb_symbol_value(mrb_intern_cstr(mrb, "a")), mrbx_number(mrb, c[3]));
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
	return colorhash(mrb, instance()->getBackground());
}

static mrb_value w_rectangle(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[5];
	mrbx_get_kwargs(mrb, {"mode", "x", "y", "width", "height"}, 5, v);

	std::string mode = mrbx_checkstring(mrb, v[0]);
	bool fill;
	if (mode == "fill")
		fill = true;
	else if (mode == "line")
		fill = false;
	else
	{
		mrb_raisef(mrb, E_ARGUMENT_ERROR, "Invalid draw mode: %s (expected 'fill' or 'line')", mode.c_str());
		return mrb_nil_value();
	}

	instance()->rectangle(fill, mrbx_checkfloat(mrb, v[1]), mrbx_checkfloat(mrb, v[2]),
		mrbx_checkfloat(mrb, v[3]), mrbx_checkfloat(mrb, v[4]));
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
	instance()->present();
	return mrb_nil_value();
}

static mrb_value w_get_width(mrb_state *mrb, mrb_value self)
{
	(void) self;
	int w, h;
	instance()->getDimensions(w, h);
	return mrbx_integer(mrb, w);
}

static mrb_value w_get_height(mrb_state *mrb, mrb_value self)
{
	(void) self;
	int w, h;
	instance()->getDimensions(w, h);
	return mrbx_integer(mrb, h);
}

static mrb_value w_get_dimensions(mrb_state *mrb, mrb_value self)
{
	(void) self;
	int w, h;
	instance()->getDimensions(w, h);
	mrb_value arr = mrb_ary_new_capa(mrb, 2);
	mrb_ary_push(mrb, arr, mrbx_integer(mrb, w));
	mrb_ary_push(mrb, arr, mrbx_integer(mrb, h));
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
	Module *inst = Module::getInstance<Module>(Module::M_GRAPHICS);
	if (inst == nullptr)
		inst = new HarnessGraphics();
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
