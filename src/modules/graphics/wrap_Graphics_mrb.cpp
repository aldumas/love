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
#include "common/Matrix.h"
#include "common/Object.h"
#include "Graphics.h"
#include "Texture.h"
#include "Quad.h"
#include "image/Image.h"
#include "image/ImageData.h"
#include "image/CompressedImageData.h"
#include "filesystem/Filesystem.h"

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

static void hset(mrb_state *mrb, mrb_value h, const char *key, mrb_value val)
{
	mrb_hash_set(mrb, h, mrb_symbol_value(mrb_intern_cstr(mrb, key)), val);
}

// =========================================================================
// Love::Texture  (new_image returns a Texture -- modern LÖVE merged Image
// into Texture). The Ruby class hierarchy mirrors love::Type, so
// Love::Texture is-a Love::Drawable.
// =========================================================================

static mrb_value w_tex_get_width(mrb_state *mrb, mrb_value self)
{
	return mrbx_integer(mrb, mrbx_checktype<Texture>(mrb, self)->getWidth());
}

static mrb_value w_tex_get_height(mrb_state *mrb, mrb_value self)
{
	return mrbx_integer(mrb, mrbx_checktype<Texture>(mrb, self)->getHeight());
}

static mrb_value w_tex_get_dimensions(mrb_state *mrb, mrb_value self)
{
	Texture *t = mrbx_checktype<Texture>(mrb, self);
	mrb_value arr = mrb_ary_new_capa(mrb, 2);
	mrb_ary_push(mrb, arr, mrbx_integer(mrb, t->getWidth()));
	mrb_ary_push(mrb, arr, mrbx_integer(mrb, t->getHeight()));
	return arr;
}

static mrb_value w_tex_get_pixel_width(mrb_state *mrb, mrb_value self)
{
	return mrbx_integer(mrb, mrbx_checktype<Texture>(mrb, self)->getPixelWidth());
}

static mrb_value w_tex_get_pixel_height(mrb_state *mrb, mrb_value self)
{
	return mrbx_integer(mrb, mrbx_checktype<Texture>(mrb, self)->getPixelHeight());
}

static mrb_value w_tex_get_pixel_dimensions(mrb_state *mrb, mrb_value self)
{
	Texture *t = mrbx_checktype<Texture>(mrb, self);
	mrb_value arr = mrb_ary_new_capa(mrb, 2);
	mrb_ary_push(mrb, arr, mrbx_integer(mrb, t->getPixelWidth()));
	mrb_ary_push(mrb, arr, mrbx_integer(mrb, t->getPixelHeight()));
	return arr;
}

static mrb_value w_tex_get_dpi_scale(mrb_state *mrb, mrb_value self)
{
	return mrbx_number(mrb, mrbx_checktype<Texture>(mrb, self)->getDPIScale());
}

static mrb_value w_tex_is_compressed(mrb_state *mrb, mrb_value self)
{
	return mrbx_boolean(mrb, mrbx_checktype<Texture>(mrb, self)->isCompressed());
}

// set_filter(min:, mag:) -- mag defaults to min. Values are "linear"/"nearest".
static mrb_value w_tex_set_filter(mrb_state *mrb, mrb_value self)
{
	mrb_value v[2];
	mrbx_get_kwargs(mrb, {"min", "mag"}, 1, v);
	Texture *t = mrbx_checktype<Texture>(mrb, self);
	SamplerState s = t->getSamplerState();

	std::string minstr = mrbx_checkstring(mrb, v[0]);
	if (!SamplerState::getConstant(minstr.c_str(), s.minFilter))
		mrb_raisef(mrb, E_ARGUMENT_ERROR, "Invalid filter mode: %s", minstr.c_str());

	if (!mrb_undef_p(v[1]))
	{
		std::string magstr = mrbx_checkstring(mrb, v[1]);
		if (!SamplerState::getConstant(magstr.c_str(), s.magFilter))
			mrb_raisef(mrb, E_ARGUMENT_ERROR, "Invalid filter mode: %s", magstr.c_str());
	}
	else
		s.magFilter = s.minFilter;

	mrbx_catchexcept(mrb, [&]() { t->setSamplerState(s); });
	return mrb_nil_value();
}

static mrb_value w_tex_get_filter(mrb_state *mrb, mrb_value self)
{
	const SamplerState &s = mrbx_checktype<Texture>(mrb, self)->getSamplerState();
	const char *mins = "linear", *mags = "linear";
	SamplerState::getConstant(s.minFilter, mins);
	SamplerState::getConstant(s.magFilter, mags);
	mrb_value h = mrb_hash_new(mrb);
	hset(mrb, h, "min", mrbx_string(mrb, mins));
	hset(mrb, h, "mag", mrbx_string(mrb, mags));
	return h;
}

static const MrbReg textureFunctions[] =
{
	{ "get_width",            w_tex_get_width,            MRB_ARGS_NONE() },
	{ "get_height",           w_tex_get_height,           MRB_ARGS_NONE() },
	{ "get_dimensions",       w_tex_get_dimensions,       MRB_ARGS_NONE() },
	{ "get_pixel_width",      w_tex_get_pixel_width,      MRB_ARGS_NONE() },
	{ "get_pixel_height",     w_tex_get_pixel_height,     MRB_ARGS_NONE() },
	{ "get_pixel_dimensions", w_tex_get_pixel_dimensions, MRB_ARGS_NONE() },
	{ "get_dpi_scale",        w_tex_get_dpi_scale,        MRB_ARGS_NONE() },
	{ "is_compressed",        w_tex_is_compressed,        MRB_ARGS_NONE() },
	{ "set_filter",           w_tex_set_filter,           MRB_ARGS_KEY(2, 0) },
	{ "get_filter",           w_tex_get_filter,           MRB_ARGS_NONE() },
	{ nullptr, nullptr, 0 }
};

// =========================================================================
// Love::Quad  (a sub-rectangle of a texture's source coordinate space)
// =========================================================================

static mrb_value w_quad_get_viewport(mrb_state *mrb, mrb_value self)
{
	Quad::Viewport v = mrbx_checktype<Quad>(mrb, self)->getViewport();
	mrb_value h = mrb_hash_new(mrb);
	hset(mrb, h, "x", mrbx_number(mrb, v.x));
	hset(mrb, h, "y", mrbx_number(mrb, v.y));
	hset(mrb, h, "width", mrbx_number(mrb, v.w));
	hset(mrb, h, "height", mrbx_number(mrb, v.h));
	return h;
}

static mrb_value w_quad_set_viewport(mrb_state *mrb, mrb_value self)
{
	mrb_value v[4];
	mrbx_get_kwargs(mrb, {"x", "y", "width", "height"}, 4, v);
	Quad::Viewport vp;
	vp.x = mrbx_checkfloat(mrb, v[0]);
	vp.y = mrbx_checkfloat(mrb, v[1]);
	vp.w = mrbx_checkfloat(mrb, v[2]);
	vp.h = mrbx_checkfloat(mrb, v[3]);
	mrbx_checktype<Quad>(mrb, self)->setViewport(vp);
	return mrb_nil_value();
}

static const MrbReg quadFunctions[] =
{
	{ "get_viewport", w_quad_get_viewport, MRB_ARGS_NONE() },
	{ "set_viewport", w_quad_set_viewport, MRB_ARGS_KEY(4, 0) },
	{ nullptr, nullptr, 0 }
};

// =========================================================================
// Love::Graphics object-creating + drawing functions
// =========================================================================

// new_image(file:, linear: false). `file:` is a filename String, or an
// ImageData / CompressedImageData object. Returns a Love::Texture.
static mrb_value w_new_image(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[2];
	mrbx_get_kwargs(mrb, {"file", "linear"}, 1, v);

	// Resolve `file:` to image data we hold a reference to (release before return).
	image::ImageData *idata = nullptr;
	image::CompressedImageData *cdata = nullptr;

	if (mrbx_istype<image::ImageData>(mrb, v[0]))
	{
		idata = mrbx_checktype<image::ImageData>(mrb, v[0]);
		idata->retain();
	}
	else if (mrbx_istype<image::CompressedImageData>(mrb, v[0]))
	{
		cdata = mrbx_checktype<image::CompressedImageData>(mrb, v[0]);
		cdata->retain();
	}
	else
	{
		std::string filename = mrbx_checkstring(mrb, v[0]);
		auto imagemodule = Module::getInstance<image::Image>(Module::M_IMAGE);
		if (imagemodule == nullptr)
			mrb_raise(mrb, E_RUNTIME_ERROR, "Cannot load images without the love.image module.");
		auto fs = Module::getInstance<filesystem::Filesystem>(Module::M_FILESYSTEM);
		if (fs == nullptr)
			mrb_raise(mrb, E_RUNTIME_ERROR, "Cannot load an image from a filename without the love.filesystem module.");

		Data *fdata = nullptr;
		if (mrbx_catchexcept(mrb, [&]() { fdata = fs->read(filename.c_str()); }))
			return mrb_nil_value();
		bool err = mrbx_catchexcept(mrb, [&]() {
			if (imagemodule->isCompressed(fdata))
				cdata = imagemodule->newCompressedData(fdata);
			else
				idata = imagemodule->newImageData(fdata);
		});
		fdata->release();
		if (err)
			return mrb_nil_value();
	}

	Texture::Settings settings;
	settings.type = TEXTURE_2D;
	settings.linear = mrbx_optboolean(mrb, v[1], false);

	Texture::Slices slices(TEXTURE_2D);
	if (idata != nullptr)
		slices.set(0, 0, idata);
	else
		slices.add(cdata, 0, 0, false, false);

	Texture *tex = nullptr;
	bool err = mrbx_catchexcept(mrb, [&]() { tex = instance()->newTexture(settings, &slices); });
	if (idata != nullptr) idata->release();
	if (cdata != nullptr) cdata->release();
	if (err)
		return mrb_nil_value();

	mrb_value r = mrbx_pushtype(mrb, tex);
	tex->release();
	return r;
}

// new_quad(x:, y:, width:, height:, sw:, sh:) or new_quad(x:, y:, width:,
// height:, texture:) -- sw/sh are the source texture's reference dimensions.
static mrb_value w_new_quad(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[7];
	mrbx_get_kwargs(mrb, {"x", "y", "width", "height", "sw", "sh", "texture"}, 4, v);

	Quad::Viewport vp;
	vp.x = mrbx_checkfloat(mrb, v[0]);
	vp.y = mrbx_checkfloat(mrb, v[1]);
	vp.w = mrbx_checkfloat(mrb, v[2]);
	vp.h = mrbx_checkfloat(mrb, v[3]);

	double sw = 0.0, sh = 0.0;
	if (!mrb_undef_p(v[6]))
	{
		Texture *t = mrbx_checktype<Texture>(mrb, v[6]);
		sw = t->getWidth();
		sh = t->getHeight();
	}
	else
	{
		sw = mrbx_checkfloat(mrb, v[4]);
		sh = mrbx_checkfloat(mrb, v[5]);
	}

	Quad *q = instance()->newQuad(vp, sw, sh);
	mrb_value r = mrbx_pushtype(mrb, q);
	q->release();
	return r;
}

// draw(drawable:, x:, y:, r:, sx:, sy:, ox:, oy:, kx:, ky:, quad:) -- draws a
// Drawable (e.g. a Texture) with the given transform. If `quad:` is supplied,
// `drawable:` must be a Texture and only that sub-region is drawn.
static mrb_value w_draw(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[11];
	mrbx_get_kwargs(mrb, {"drawable", "quad", "x", "y", "r", "sx", "sy",
		"ox", "oy", "kx", "ky"}, 1, v);

	float x  = mrbx_optfloat(mrb, v[2], 0.0f);
	float y  = mrbx_optfloat(mrb, v[3], 0.0f);
	float angle = mrbx_optfloat(mrb, v[4], 0.0f);
	float sx = mrbx_optfloat(mrb, v[5], 1.0f);
	float sy = mrbx_optfloat(mrb, v[6], sx);
	float ox = mrbx_optfloat(mrb, v[7], 0.0f);
	float oy = mrbx_optfloat(mrb, v[8], 0.0f);
	float kx = mrbx_optfloat(mrb, v[9], 0.0f);
	float ky = mrbx_optfloat(mrb, v[10], 0.0f);

	Matrix4 m(x, y, angle, sx, sy, ox, oy, kx, ky);

	if (!mrb_undef_p(v[1]) && !mrb_nil_p(v[1]))
	{
		Texture *tex = mrbx_checktype<Texture>(mrb, v[0]);
		Quad *quad = mrbx_checktype<Quad>(mrb, v[1]);
		mrbx_catchexcept(mrb, [&]() { instance()->draw(tex, quad, m); });
	}
	else
	{
		Drawable *d = mrbx_checktype<Drawable>(mrb, v[0]);
		mrbx_catchexcept(mrb, [&]() { instance()->draw(d, m); });
	}
	return mrb_nil_value();
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
	{ "new_image",            w_new_image,            MRB_ARGS_KEY(2, 0) },
	{ "new_quad",             w_new_quad,             MRB_ARGS_KEY(7, 0) },
	{ "draw",                 w_draw,                 MRB_ARGS_KEY(11, 0) },
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

	// Object types created by this module. Their Ruby superclasses follow the
	// love::Type hierarchy (Texture is-a Drawable), set up lazily on first use.
	mrbx_register_type(mrb, Texture::type, textureFunctions);
	mrbx_register_type(mrb, Quad::type, quadFunctions);
}

} // graphics
} // love
