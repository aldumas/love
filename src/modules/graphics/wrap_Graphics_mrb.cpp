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
// The Ruby-facing API is a growing slice over the real backend --
//
//   active?, clear, set_color / set_background_color, rectangle, present,
//   the coordinate-system transform stack (origin / push / pop / translate /
//   rotate / scale / shear / apply_transform / replace_transform /
//   transform_point / inverse_transform_point), render state (blend mode,
//   scissor, color mask, line width/style/join, point size, wireframe),
//   new_image / new_quad / draw, new_font / print / printf, shaders
//   (new_shader / set_shader / get_shader + the Shader type), and canvas /
//   render targets (new_canvas / set_canvas / get_canvas)
//
// -- exercising the real batched-draw path (a rectangle goes through the default
// shader and the streaming vertex buffer). Stencil/depth state and the remaining
// object types (SpriteBatch, Mesh, ParticleSystem, TextBatch, Video) are still
// to be exposed; the binding will grow onto the same real Graphics instance.

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
#include "Font.h"
#include "Shader.h"
#include "math/Transform.h"
#include "math/MathModule.h"
#include "image/Image.h"
#include "image/ImageData.h"
#include "image/CompressedImageData.h"
#include "filesystem/Filesystem.h"
#include "filesystem/FileData.h"
#include "font/Font.h"
#include "font/Rasterizer.h"
#include "font/TrueTypeRasterizer.h"
#include "font/TextShaper.h"

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
// Coordinate-system transform stack. Faithful to wrap_Graphics.cpp; the
// positional Lua args become keyword args.
// =========================================================================

// push(type:) optional ("transform" default, or "all"); push(transform:) also
// applies a Love::Transform after the push (mirrors w_push's optional 2nd arg).
static mrb_value w_push(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[2];
	mrbx_get_kwargs(mrb, {"type", "transform"}, 0, v);

	Graphics::StackType stype = Graphics::STACK_TRANSFORM;
	if (!mrb_undef_p(v[0]))
	{
		std::string sname = mrbx_checkstring(mrb, v[0]);
		if (!Graphics::getConstant(sname.c_str(), stype))
			mrb_raisef(mrb, E_ARGUMENT_ERROR, "Invalid graphics stack type: %s", sname.c_str());
	}

	mrbx_catchexcept(mrb, [&]() { instance()->push(stype); });

	if (!mrb_undef_p(v[1]))
	{
		math::Transform *t = mrbx_checktype<math::Transform>(mrb, v[1]);
		mrbx_catchexcept(mrb, [&]() { instance()->applyTransform(t->getMatrix()); });
	}
	return mrb_nil_value();
}

static mrb_value w_pop(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrbx_catchexcept(mrb, [&]() { instance()->pop(); });
	return mrb_nil_value();
}

static mrb_value w_translate(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[2];
	mrbx_get_kwargs(mrb, {"x", "y"}, 2, v);
	instance()->translate(mrbx_checkfloat(mrb, v[0]), mrbx_checkfloat(mrb, v[1]));
	return mrb_nil_value();
}

static mrb_value w_rotate(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[1];
	mrbx_get_kwargs(mrb, {"angle"}, 1, v);
	instance()->rotate(mrbx_checkfloat(mrb, v[0]));
	return mrb_nil_value();
}

// scale(x:) defaults y to x; scale(x:, y:) sets both. Both default to 1.0.
static mrb_value w_scale(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[2];
	mrbx_get_kwargs(mrb, {"x", "y"}, 0, v);
	float sx = mrbx_optfloat(mrb, v[0], 1.0f);
	float sy = mrbx_optfloat(mrb, v[1], sx);
	instance()->scale(sx, sy);
	return mrb_nil_value();
}

static mrb_value w_shear(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[2];
	mrbx_get_kwargs(mrb, {"kx", "ky"}, 2, v);
	instance()->shear(mrbx_checkfloat(mrb, v[0]), mrbx_checkfloat(mrb, v[1]));
	return mrb_nil_value();
}

static mrb_value w_apply_transform(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[1];
	mrbx_get_kwargs(mrb, {"transform"}, 1, v);
	math::Transform *t = mrbx_checktype<math::Transform>(mrb, v[0]);
	mrbx_catchexcept(mrb, [&]() { instance()->applyTransform(t->getMatrix()); });
	return mrb_nil_value();
}

static mrb_value w_replace_transform(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[1];
	mrbx_get_kwargs(mrb, {"transform"}, 1, v);
	math::Transform *t = mrbx_checktype<math::Transform>(mrb, v[0]);
	mrbx_catchexcept(mrb, [&]() { instance()->replaceTransform(t->getMatrix()); });
	return mrb_nil_value();
}

// transform_point(x:, y:) -> Hash {x:, y:} (Lua returned two numbers).
static mrb_value w_transform_point(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[2];
	mrbx_get_kwargs(mrb, {"x", "y"}, 2, v);
	Vector2 p(mrbx_checkfloat(mrb, v[0]), mrbx_checkfloat(mrb, v[1]));
	p = instance()->transformPoint(p);
	mrb_value out = mrb_hash_new(mrb);
	hset(mrb, out, "x", mrbx_number(mrb, p.x));
	hset(mrb, out, "y", mrbx_number(mrb, p.y));
	return out;
}

static mrb_value w_inverse_transform_point(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[2];
	mrbx_get_kwargs(mrb, {"x", "y"}, 2, v);
	Vector2 p(mrbx_checkfloat(mrb, v[0]), mrbx_checkfloat(mrb, v[1]));
	p = instance()->inverseTransformPoint(p);
	mrb_value out = mrb_hash_new(mrb);
	hset(mrb, out, "x", mrbx_number(mrb, p.x));
	hset(mrb, out, "y", mrbx_number(mrb, p.y));
	return out;
}

// =========================================================================
// Render state. Faithful to wrap_Graphics.cpp; the positional Lua args become
// keyword args. (Stencil/depth state is deferred -- it depends on render
// targets / a stencil buffer, which aren't exposed yet.)
// =========================================================================

// set_blend_mode(mode:[, alpha_mode:]); alpha_mode defaults to "alphamultiply".
static mrb_value w_set_blend_mode(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[2];
	mrbx_get_kwargs(mrb, {"mode", "alpha_mode"}, 1, v);

	std::string modestr = mrbx_checkstring(mrb, v[0]);
	BlendMode mode;
	if (!getConstant(modestr.c_str(), mode))
		mrb_raisef(mrb, E_ARGUMENT_ERROR, "Invalid blend mode: %s", modestr.c_str());

	BlendAlpha alphamode = BLENDALPHA_MULTIPLY;
	if (!mrb_undef_p(v[1]))
	{
		std::string alphastr = mrbx_checkstring(mrb, v[1]);
		if (!getConstant(alphastr.c_str(), alphamode))
			mrb_raisef(mrb, E_ARGUMENT_ERROR, "Invalid blend alpha mode: %s", alphastr.c_str());
	}

	mrbx_catchexcept(mrb, [&]() { instance()->setBlendMode(mode, alphamode); });
	return mrb_nil_value();
}

// get_blend_mode -> Hash {mode:, alpha_mode:}.
static mrb_value w_get_blend_mode(mrb_state *mrb, mrb_value self)
{
	(void) self;
	BlendAlpha alphamode;
	BlendMode mode = instance()->getBlendMode(alphamode);
	const char *modestr = nullptr;
	const char *alphastr = nullptr;
	getConstant(mode, modestr);
	getConstant(alphamode, alphastr);
	mrb_value out = mrb_hash_new(mrb);
	hset(mrb, out, "mode", mrbx_string(mrb, modestr ? modestr : ""));
	hset(mrb, out, "alpha_mode", mrbx_string(mrb, alphastr ? alphastr : ""));
	return out;
}

// set_scissor(x:, y:, width:, height:) -- all optional; with none given the
// scissor is disabled. Negative width/height raises.
static mrb_value w_set_scissor(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[4];
	mrbx_get_kwargs(mrb, {"x", "y", "width", "height"}, 0, v);

	if (mrb_undef_p(v[0]) && mrb_undef_p(v[1]) && mrb_undef_p(v[2]) && mrb_undef_p(v[3]))
	{
		instance()->setScissor();
		return mrb_nil_value();
	}

	Rect rect;
	rect.x = (int) mrbx_checkint(mrb, v[0]);
	rect.y = (int) mrbx_checkint(mrb, v[1]);
	rect.w = (int) mrbx_checkint(mrb, v[2]);
	rect.h = (int) mrbx_checkint(mrb, v[3]);
	if (rect.w < 0 || rect.h < 0)
		mrb_raise(mrb, E_ARGUMENT_ERROR, "Can't set scissor with negative width and/or height.");

	instance()->setScissor(rect);
	return mrb_nil_value();
}

static mrb_value w_intersect_scissor(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[4];
	mrbx_get_kwargs(mrb, {"x", "y", "width", "height"}, 4, v);
	Rect rect;
	rect.x = (int) mrbx_checkint(mrb, v[0]);
	rect.y = (int) mrbx_checkint(mrb, v[1]);
	rect.w = (int) mrbx_checkint(mrb, v[2]);
	rect.h = (int) mrbx_checkint(mrb, v[3]);
	if (rect.w < 0 || rect.h < 0)
		mrb_raise(mrb, E_ARGUMENT_ERROR, "Can't set scissor with negative width and/or height.");

	instance()->intersectScissor(rect);
	return mrb_nil_value();
}

// get_scissor -> Hash {x:, y:, width:, height:}, or nil if no scissor is set.
static mrb_value w_get_scissor(mrb_state *mrb, mrb_value self)
{
	(void) self;
	Rect rect;
	if (!instance()->getScissor(rect))
		return mrb_nil_value();
	mrb_value out = mrb_hash_new(mrb);
	hset(mrb, out, "x", mrbx_integer(mrb, rect.x));
	hset(mrb, out, "y", mrbx_integer(mrb, rect.y));
	hset(mrb, out, "width", mrbx_integer(mrb, rect.w));
	hset(mrb, out, "height", mrbx_integer(mrb, rect.h));
	return out;
}

// set_color_mask(r:, g:, b:, a:) -- each optional, defaulting to true (so
// set_color_mask(r: false) masks only red). With no args, all channels enable.
static mrb_value w_set_color_mask(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[4];
	mrbx_get_kwargs(mrb, {"r", "g", "b", "a"}, 0, v);
	ColorChannelMask mask;
	mask.r = mrbx_optboolean(mrb, v[0], true);
	mask.g = mrbx_optboolean(mrb, v[1], true);
	mask.b = mrbx_optboolean(mrb, v[2], true);
	mask.a = mrbx_optboolean(mrb, v[3], true);
	instance()->setColorMask(mask);
	return mrb_nil_value();
}

static mrb_value w_get_color_mask(mrb_state *mrb, mrb_value self)
{
	(void) self;
	ColorChannelMask mask = instance()->getColorMask();
	mrb_value out = mrb_hash_new(mrb);
	hset(mrb, out, "r", mrbx_boolean(mrb, mask.r));
	hset(mrb, out, "g", mrbx_boolean(mrb, mask.g));
	hset(mrb, out, "b", mrbx_boolean(mrb, mask.b));
	hset(mrb, out, "a", mrbx_boolean(mrb, mask.a));
	return out;
}

static mrb_value w_set_line_width(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[1];
	mrbx_get_kwargs(mrb, {"width"}, 1, v);
	instance()->setLineWidth(mrbx_checkfloat(mrb, v[0]));
	return mrb_nil_value();
}

static mrb_value w_get_line_width(mrb_state *mrb, mrb_value self)
{
	(void) self;
	return mrbx_number(mrb, instance()->getLineWidth());
}

static mrb_value w_set_line_style(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[1];
	mrbx_get_kwargs(mrb, {"style"}, 1, v);
	std::string str = mrbx_checkstring(mrb, v[0]);
	Graphics::LineStyle style;
	if (!Graphics::getConstant(str.c_str(), style))
		mrb_raisef(mrb, E_ARGUMENT_ERROR, "Invalid line style: %s", str.c_str());
	instance()->setLineStyle(style);
	return mrb_nil_value();
}

static mrb_value w_get_line_style(mrb_state *mrb, mrb_value self)
{
	(void) self;
	const char *str = nullptr;
	Graphics::getConstant(instance()->getLineStyle(), str);
	return mrbx_string(mrb, str ? str : "");
}

static mrb_value w_set_line_join(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[1];
	mrbx_get_kwargs(mrb, {"join"}, 1, v);
	std::string str = mrbx_checkstring(mrb, v[0]);
	Graphics::LineJoin join;
	if (!Graphics::getConstant(str.c_str(), join))
		mrb_raisef(mrb, E_ARGUMENT_ERROR, "Invalid line join: %s", str.c_str());
	instance()->setLineJoin(join);
	return mrb_nil_value();
}

static mrb_value w_get_line_join(mrb_state *mrb, mrb_value self)
{
	(void) self;
	const char *str = nullptr;
	Graphics::getConstant(instance()->getLineJoin(), str);
	return mrbx_string(mrb, str ? str : "");
}

static mrb_value w_set_point_size(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[1];
	mrbx_get_kwargs(mrb, {"size"}, 1, v);
	instance()->setPointSize(mrbx_checkfloat(mrb, v[0]));
	return mrb_nil_value();
}

static mrb_value w_get_point_size(mrb_state *mrb, mrb_value self)
{
	(void) self;
	return mrbx_number(mrb, instance()->getPointSize());
}

static mrb_value w_set_wireframe(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[1];
	mrbx_get_kwargs(mrb, {"enable"}, 1, v);
	instance()->setWireframe(mrbx_checkboolean(mrb, v[0]));
	return mrb_nil_value();
}

static mrb_value w_is_wireframe(mrb_state *mrb, mrb_value self)
{
	(void) self;
	return mrbx_boolean(mrb, instance()->isWireframe());
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

// =========================================================================
// Love::Font  (a graphics::Font -- a rasterizer uploaded into a glyph atlas,
// used by print/printf). Distinct from love.font's Rasterizer.
// =========================================================================

static mrb_value w_font_get_height(mrb_state *mrb, mrb_value self)
{
	return mrbx_number(mrb, mrbx_checktype<Font>(mrb, self)->getHeight());
}

static mrb_value w_font_get_width(mrb_state *mrb, mrb_value self)
{
	mrb_value v[1];
	mrbx_get_kwargs(mrb, {"text"}, 1, v);
	int w = 0;
	mrbx_catchexcept(mrb, [&]() { w = mrbx_checktype<Font>(mrb, self)->getWidth(mrbx_checkstring(mrb, v[0])); });
	return mrbx_integer(mrb, w);
}

static mrb_value w_font_get_ascent(mrb_state *mrb, mrb_value self)
{
	return mrbx_integer(mrb, mrbx_checktype<Font>(mrb, self)->getAscent());
}

static mrb_value w_font_get_descent(mrb_state *mrb, mrb_value self)
{
	return mrbx_integer(mrb, mrbx_checktype<Font>(mrb, self)->getDescent());
}

static mrb_value w_font_get_baseline(mrb_state *mrb, mrb_value self)
{
	return mrbx_number(mrb, mrbx_checktype<Font>(mrb, self)->getBaseline());
}

static mrb_value w_font_get_line_height(mrb_state *mrb, mrb_value self)
{
	return mrbx_number(mrb, mrbx_checktype<Font>(mrb, self)->getLineHeight());
}

static mrb_value w_font_set_line_height(mrb_state *mrb, mrb_value self)
{
	mrb_value v[1];
	mrbx_get_kwargs(mrb, {"height"}, 1, v);
	mrbx_checktype<Font>(mrb, self)->setLineHeight(mrbx_checkfloat(mrb, v[0]));
	return mrb_nil_value();
}

static mrb_value w_font_get_dpi_scale(mrb_state *mrb, mrb_value self)
{
	return mrbx_number(mrb, mrbx_checktype<Font>(mrb, self)->getDPIScale());
}

static mrb_value w_font_has_glyphs(mrb_state *mrb, mrb_value self)
{
	mrb_value v[1];
	mrbx_get_kwargs(mrb, {"text"}, 1, v);
	bool has = false;
	mrbx_catchexcept(mrb, [&]() { has = mrbx_checktype<Font>(mrb, self)->hasGlyphs(mrbx_checkstring(mrb, v[0])); });
	return mrbx_boolean(mrb, has);
}

// get_wrap(text:, width:) -> { width: <max line width>, lines: [String, ...] }
static mrb_value w_font_get_wrap(mrb_state *mrb, mrb_value self)
{
	mrb_value v[2];
	mrbx_get_kwargs(mrb, {"text", "width"}, 2, v);
	Font *f = mrbx_checktype<Font>(mrb, self);

	std::vector<love::font::ColoredString> text;
	text.push_back({ mrbx_checkstring(mrb, v[0]), Colorf(1, 1, 1, 1) });
	float wraplimit = mrbx_checkfloat(mrb, v[1]);

	std::vector<std::string> lines;
	std::vector<float> widths;
	mrbx_catchexcept(mrb, [&]() { f->getWrap(text, wraplimit, lines, &widths); });

	float maxwidth = 0.0f;
	for (float w : widths)
		maxwidth = std::max(maxwidth, w);

	mrb_value linesary = mrb_ary_new_capa(mrb, (mrb_int) lines.size());
	for (const std::string &line : lines)
		mrb_ary_push(mrb, linesary, mrbx_string(mrb, line));

	mrb_value h = mrb_hash_new(mrb);
	hset(mrb, h, "width", mrbx_number(mrb, maxwidth));
	hset(mrb, h, "lines", linesary);
	return h;
}

static const MrbReg fontFunctions[] =
{
	{ "get_height",      w_font_get_height,      MRB_ARGS_NONE() },
	{ "get_width",       w_font_get_width,       MRB_ARGS_KEY(1, 0) },
	{ "get_ascent",      w_font_get_ascent,      MRB_ARGS_NONE() },
	{ "get_descent",     w_font_get_descent,     MRB_ARGS_NONE() },
	{ "get_baseline",    w_font_get_baseline,    MRB_ARGS_NONE() },
	{ "get_line_height", w_font_get_line_height, MRB_ARGS_NONE() },
	{ "set_line_height", w_font_set_line_height, MRB_ARGS_KEY(1, 0) },
	{ "get_dpi_scale",   w_font_get_dpi_scale,   MRB_ARGS_NONE() },
	{ "has_glyphs",      w_font_has_glyphs,      MRB_ARGS_KEY(1, 0) },
	{ "get_wrap",        w_font_get_wrap,        MRB_ARGS_KEY(2, 0) },
	{ nullptr, nullptr, 0 }
};

// =========================================================================
// Love::Graphics font + text functions
// =========================================================================

// new_font(size:) for the embedded default font, or new_font(file:, size:) for
// a TrueType file. Returns a Love::Font.
static mrb_value w_new_font(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[2];
	mrbx_get_kwargs(mrb, {"file", "size"}, 0, v);

	int size = mrbx_optint(mrb, v[1], 13);
	love::font::TrueTypeRasterizer::Settings settings;

	Font *font = nullptr;

	if (mrb_undef_p(v[0]))
	{
		// Default (embedded) font at the requested size.
		mrbx_catchexcept(mrb, [&]() { font = instance()->newDefaultFont(size, settings); });
	}
	else
	{
		std::string filename = mrbx_checkstring(mrb, v[0]);
		auto fontmodule = Module::getInstance<love::font::Font>(Module::M_FONT);
		if (fontmodule == nullptr)
			mrb_raise(mrb, E_RUNTIME_ERROR, "Cannot create a font without the love.font module.");
		auto fs = Module::getInstance<filesystem::Filesystem>(Module::M_FILESYSTEM);
		if (fs == nullptr)
			mrb_raise(mrb, E_RUNTIME_ERROR, "Cannot load a font from a filename without the love.filesystem module.");

		love::filesystem::FileData *fdata = nullptr;
		if (mrbx_catchexcept(mrb, [&]() { fdata = fs->read(filename.c_str()); }))
			return mrb_nil_value();

		love::font::Rasterizer *r = nullptr;
		bool err = mrbx_catchexcept(mrb, [&]() { r = fontmodule->newTrueTypeRasterizer(fdata, size, settings); });
		fdata->release();
		if (err)
			return mrb_nil_value();

		bool err2 = mrbx_catchexcept(mrb, [&]() { font = instance()->newFont(r); });
		r->release();
		if (err2)
			return mrb_nil_value();
	}

	mrb_value res = mrbx_pushtype(mrb, font);
	font->release();
	return res;
}

static mrb_value w_set_font(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[1];
	mrbx_get_kwargs(mrb, {"font"}, 1, v);
	// nil clears the active font (falls back to the default on next print).
	Font *font = mrb_nil_p(v[0]) ? nullptr : mrbx_checktype<Font>(mrb, v[0]);
	instance()->setFont(font);
	return mrb_nil_value();
}

static mrb_value w_get_font(mrb_state *mrb, mrb_value self)
{
	(void) self;
	Font *font = nullptr;
	mrbx_catchexcept(mrb, [&]() { font = instance()->getFont(); });
	return mrbx_pushtype(mrb, font);
}

// print(text:, x:, y:, r:, sx:, sy:, ox:, oy:, kx:, ky:, font:) -- draws text
// with the current (or given) font in the current color.
static mrb_value w_print(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[11];
	mrbx_get_kwargs(mrb, {"text", "x", "y", "r", "sx", "sy", "ox", "oy",
		"kx", "ky", "font"}, 1, v);

	std::vector<love::font::ColoredString> text;
	text.push_back({ mrbx_checkstring(mrb, v[0]), Colorf(1, 1, 1, 1) });

	float x  = mrbx_optfloat(mrb, v[1], 0.0f);
	float y  = mrbx_optfloat(mrb, v[2], 0.0f);
	float angle = mrbx_optfloat(mrb, v[3], 0.0f);
	float sx = mrbx_optfloat(mrb, v[4], 1.0f);
	float sy = mrbx_optfloat(mrb, v[5], sx);
	float ox = mrbx_optfloat(mrb, v[6], 0.0f);
	float oy = mrbx_optfloat(mrb, v[7], 0.0f);
	float kx = mrbx_optfloat(mrb, v[8], 0.0f);
	float ky = mrbx_optfloat(mrb, v[9], 0.0f);

	Matrix4 m(x, y, angle, sx, sy, ox, oy, kx, ky);

	if (!mrb_undef_p(v[10]) && !mrb_nil_p(v[10]))
	{
		Font *font = mrbx_checktype<Font>(mrb, v[10]);
		mrbx_catchexcept(mrb, [&]() { instance()->print(text, font, m); });
	}
	else
		mrbx_catchexcept(mrb, [&]() { instance()->print(text, m); });
	return mrb_nil_value();
}

// printf(text:, x:, y:, limit:, align:, r:, sx:, sy:, ox:, oy:, kx:, ky:, font:)
// -- word-wrapped text within `limit` pixels, aligned left/center/right/justify.
static mrb_value w_printf(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[13];
	mrbx_get_kwargs(mrb, {"text", "x", "y", "limit", "align", "r", "sx", "sy",
		"ox", "oy", "kx", "ky", "font"}, 4, v);

	std::vector<love::font::ColoredString> text;
	text.push_back({ mrbx_checkstring(mrb, v[0]), Colorf(1, 1, 1, 1) });

	float x = mrbx_checkfloat(mrb, v[1]);
	float y = mrbx_checkfloat(mrb, v[2]);
	float limit = mrbx_checkfloat(mrb, v[3]);

	Font::AlignMode align = Font::ALIGN_LEFT;
	if (!mrb_undef_p(v[4]))
	{
		std::string alignstr = mrbx_checkstring(mrb, v[4]);
		if (!Font::getConstant(alignstr.c_str(), align))
			mrb_raisef(mrb, E_ARGUMENT_ERROR, "Invalid alignment: %s", alignstr.c_str());
	}

	float angle = mrbx_optfloat(mrb, v[5], 0.0f);
	float sx = mrbx_optfloat(mrb, v[6], 1.0f);
	float sy = mrbx_optfloat(mrb, v[7], sx);
	float ox = mrbx_optfloat(mrb, v[8], 0.0f);
	float oy = mrbx_optfloat(mrb, v[9], 0.0f);
	float kx = mrbx_optfloat(mrb, v[10], 0.0f);
	float ky = mrbx_optfloat(mrb, v[11], 0.0f);

	Matrix4 m(x, y, angle, sx, sy, ox, oy, kx, ky);

	if (!mrb_undef_p(v[12]) && !mrb_nil_p(v[12]))
	{
		Font *font = mrbx_checktype<Font>(mrb, v[12]);
		mrbx_catchexcept(mrb, [&]() { instance()->printf(text, font, limit, align, m); });
	}
	else
		mrbx_catchexcept(mrb, [&]() { instance()->printf(text, limit, align, m); });
	return mrb_nil_value();
}

// =========================================================================
// Love::Shader  (a compiled shader program). A faithful-enough port of
// wrap_Shader.cpp's send dispatch: floats/ints/uints/bools (scalars, vectors,
// or arrays of either), matrices (a Love::Transform or a row-major number
// array), and samplers (a Love::Texture or an array of them).
// =========================================================================

static float clamp01(float x) { return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x); }

// Pull component c of element `el` -- a bare scalar (components==1) or an Array.
static mrb_value uniform_component(mrb_state *mrb, mrb_value el, int components, int c)
{
	if (components == 1 && !mrb_array_p(el))
		return el;
	return mrb_ary_ref(mrb, el, c);
}

static void shader_send_numbers(mrb_state *mrb, Shader *shader,
	const Shader::UniformInfo *info, mrb_value value, bool colors)
{
	int components = info->components;

	// Normalize `value` into a list of elements; each element supplies
	// `components` scalars.
	std::vector<mrb_value> elements;
	if (mrb_array_p(value))
	{
		mrb_int n = RARRAY_LEN(value);
		bool firstIsArray = n > 0 && mrb_array_p(mrb_ary_ref(mrb, value, 0));
		if (firstIsArray)
			for (mrb_int i = 0; i < n; i++) elements.push_back(mrb_ary_ref(mrb, value, i));
		else if (components == 1)
			for (mrb_int i = 0; i < n; i++) elements.push_back(mrb_ary_ref(mrb, value, i));
		else
			elements.push_back(value); // a single flat vector
	}
	else
		elements.push_back(value); // a single scalar

	int count = std::min((int) elements.size(), info->count);
	if (count < 1)
		mrb_raise(mrb, E_ARGUMENT_ERROR, "No values given to Shader#send.");

	for (int i = 0; i < count; i++)
	{
		mrb_value el = elements[i];
		for (int c = 0; c < components; c++)
		{
			mrb_value comp = uniform_component(mrb, el, components, c);
			int idx = i * components + c;
			switch (info->baseType)
			{
			case Shader::UNIFORM_FLOAT:
				info->floats[idx] = colors ? clamp01(mrbx_checkfloat(mrb, comp)) : mrbx_checkfloat(mrb, comp);
				break;
			case Shader::UNIFORM_INT:
				info->ints[idx] = mrbx_checkint(mrb, comp);
				break;
			case Shader::UNIFORM_UINT:
				info->uints[idx] = (unsigned int) mrbx_checkint(mrb, comp);
				break;
			case Shader::UNIFORM_BOOL:
				info->ints[idx] = mrbx_checkboolean(mrb, comp) ? 1 : 0;
				break;
			default:
				mrb_raise(mrb, E_ARGUMENT_ERROR, "Unsupported uniform type for Shader#send.");
			}
		}
	}

	if (colors && info->baseType == Shader::UNIFORM_FLOAT && graphics::isGammaCorrect())
	{
		int gammacomponents = std::min(components, 3); // alpha stays linear
		for (int i = 0; i < count; i++)
			for (int j = 0; j < gammacomponents; j++)
				info->floats[i * components + j] = math::gammaToLinear(info->floats[i * components + j]);
	}

	mrbx_catchexcept(mrb, [&]() { shader->updateUniform(info, count); });
}

static void shader_send_matrices(mrb_state *mrb, Shader *shader,
	const Shader::UniformInfo *info, mrb_value value)
{
	int columns = info->matrix.columns;
	int rows = info->matrix.rows;
	int elements = columns * rows;

	auto isMatrixElement = [&](mrb_value v) {
		return mrb_array_p(v) || mrbx_istype<math::Transform>(mrb, v);
	};

	std::vector<mrb_value> mats;
	if (mrbx_istype<math::Transform>(mrb, value))
		mats.push_back(value);
	else if (mrb_array_p(value))
	{
		mrb_int n = RARRAY_LEN(value);
		if (n > 0 && isMatrixElement(mrb_ary_ref(mrb, value, 0)))
			for (mrb_int i = 0; i < n; i++) mats.push_back(mrb_ary_ref(mrb, value, i));
		else
			mats.push_back(value); // a single flat, row-major matrix
	}
	else
		mrb_raise(mrb, E_ARGUMENT_ERROR, "Expected a Transform or a number array for a matrix uniform.");

	int count = std::min((int) mats.size(), info->count);
	float *values = info->floats;

	for (int i = 0; i < count; i++)
	{
		mrb_value m = mats[i];
		if (columns == 4 && rows == 4 && mrbx_istype<math::Transform>(mrb, m))
		{
			math::Transform *t = mrbx_checktype<math::Transform>(mrb, m);
			memcpy(&values[i * 16], t->getMatrix().getElements(), sizeof(float) * 16);
			continue;
		}
		// A flat array laid out row-major; store column-major in memory.
		for (int col = 0; col < columns; col++)
			for (int row = 0; row < rows; row++)
				values[i * elements + (col * rows + row)] =
					mrbx_checkfloat(mrb, mrb_ary_ref(mrb, m, row * columns + col));
	}

	mrbx_catchexcept(mrb, [&]() { shader->updateUniform(info, count); });
}

static void shader_send_value(mrb_state *mrb, Shader *shader,
	const Shader::UniformInfo *info, mrb_value value, bool colors)
{
	switch (info->baseType)
	{
	case Shader::UNIFORM_SAMPLER:
	case Shader::UNIFORM_STORAGETEXTURE:
	{
		std::vector<Texture *> textures;
		if (mrb_array_p(value))
		{
			mrb_int n = RARRAY_LEN(value);
			for (mrb_int i = 0; i < n; i++)
				textures.push_back(mrbx_checktype<Texture>(mrb, mrb_ary_ref(mrb, value, i)));
		}
		else
			textures.push_back(mrbx_checktype<Texture>(mrb, value));
		int count = std::min((int) textures.size(), info->count);
		mrbx_catchexcept(mrb, [&]() { shader->sendTextures(info, textures.data(), count); });
		return;
	}
	case Shader::UNIFORM_MATRIX:
		shader_send_matrices(mrb, shader, info, value);
		return;
	default:
		shader_send_numbers(mrb, shader, info, value, colors);
		return;
	}
}

static const Shader::UniformInfo *shader_lookup(mrb_state *mrb, Shader *shader, const std::string &name)
{
	const Shader::UniformInfo *info = shader->getUniformInfo(name);
	if (info == nullptr || !info->active)
		mrb_raisef(mrb, E_ARGUMENT_ERROR,
			"Shader uniform '%s' does not exist or is not used.", name.c_str());
	return info;
}

static mrb_value w_shader_send(mrb_state *mrb, mrb_value self)
{
	Shader *shader = mrbx_checktype<Shader>(mrb, self);
	mrb_value v[2];
	mrbx_get_kwargs(mrb, {"name", "value"}, 2, v);
	std::string name = mrbx_checkstring(mrb, v[0]);
	shader_send_value(mrb, shader, shader_lookup(mrb, shader, name), v[1], false);
	return mrb_nil_value();
}

static mrb_value w_shader_send_color(mrb_state *mrb, mrb_value self)
{
	Shader *shader = mrbx_checktype<Shader>(mrb, self);
	mrb_value v[2];
	mrbx_get_kwargs(mrb, {"name", "value"}, 2, v);
	std::string name = mrbx_checkstring(mrb, v[0]);
	const Shader::UniformInfo *info = shader_lookup(mrb, shader, name);
	if (info->baseType != Shader::UNIFORM_FLOAT || info->components < 3)
		mrb_raise(mrb, E_ARGUMENT_ERROR, "send_color can only be used with vec3 or vec4 uniforms.");
	shader_send_numbers(mrb, shader, info, v[1], true);
	return mrb_nil_value();
}

static mrb_value w_shader_has_uniform(mrb_state *mrb, mrb_value self)
{
	Shader *shader = mrbx_checktype<Shader>(mrb, self);
	mrb_value v[1];
	mrbx_get_kwargs(mrb, {"name"}, 1, v);
	return mrbx_boolean(mrb, shader->hasUniform(mrbx_checkstring(mrb, v[0])));
}

static mrb_value w_shader_get_warnings(mrb_state *mrb, mrb_value self)
{
	Shader *shader = mrbx_checktype<Shader>(mrb, self);
	return mrbx_string(mrb, shader->getWarnings());
}

static const MrbReg shaderFunctions[] =
{
	{ "send",         w_shader_send,         MRB_ARGS_KEY(2, 0) },
	{ "send_color",   w_shader_send_color,   MRB_ARGS_KEY(2, 0) },
	{ "has_uniform?", w_shader_has_uniform,  MRB_ARGS_KEY(1, 0) },
	{ "get_warnings", w_shader_get_warnings, MRB_ARGS_NONE() },
	{ nullptr, nullptr, 0 }
};

// Read a shader-stage argument: a source String, or a filename String read via
// the filesystem, or a FileData. Appends the GLSL source to `stages`.
static void shader_push_stage(mrb_state *mrb, mrb_value v, std::vector<std::string> &stages)
{
	if (mrbx_istype<love::filesystem::FileData>(mrb, v))
	{
		auto fd = mrbx_checktype<love::filesystem::FileData>(mrb, v);
		stages.push_back(std::string((const char *) fd->getData(), fd->getSize()));
		return;
	}

	std::string s = mrbx_checkstring(mrb, v);
	auto fs = Module::getInstance<filesystem::Filesystem>(Module::M_FILESYSTEM);
	filesystem::Filesystem::Info finfo = {};
	if (fs != nullptr && fs->getInfo(s.c_str(), finfo) && finfo.type == filesystem::Filesystem::FILETYPE_FILE)
	{
		love::filesystem::FileData *fd = nullptr;
		if (mrbx_catchexcept(mrb, [&]() { fd = fs->read(s.c_str()); }))
			return;
		stages.push_back(std::string((const char *) fd->getData(), fd->getSize()));
		fd->release();
	}
	else
		stages.push_back(s); // treat as inline GLSL source
}

// new_shader(pixel:, vertex:) -- each optional but at least one required; each
// is GLSL source, a filename, or a FileData. Plus optional defines: (Hash) and
// debug_name: (String). The engine auto-detects each stage from its content.
static mrb_value w_new_shader(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[4];
	mrbx_get_kwargs(mrb, {"pixel", "vertex", "defines", "debug_name"}, 0, v);

	std::vector<std::string> stages;
	if (!mrb_undef_p(v[0])) shader_push_stage(mrb, v[0], stages);
	if (!mrb_undef_p(v[1])) shader_push_stage(mrb, v[1], stages);
	if (stages.empty())
		mrb_raise(mrb, E_ARGUMENT_ERROR, "new_shader needs at least a pixel: or vertex: source.");

	Shader::CompileOptions options;
	if (!mrb_undef_p(v[2]) && mrb_hash_p(v[2]))
	{
		mrb_value keys = mrb_hash_keys(mrb, v[2]);
		mrb_int n = RARRAY_LEN(keys);
		for (mrb_int i = 0; i < n; i++)
		{
			mrb_value k = mrb_ary_ref(mrb, keys, i);
			mrb_value val = mrb_hash_get(mrb, v[2], k);
			options.defines[mrbx_checkstring(mrb, mrb_obj_as_string(mrb, k))] =
				mrbx_checkstring(mrb, mrb_obj_as_string(mrb, val));
		}
	}
	if (!mrb_undef_p(v[3]))
		options.debugName = mrbx_checkstring(mrb, v[3]);

	Shader *shader = nullptr;
	if (mrbx_catchexcept(mrb, [&]() { shader = instance()->newShader(stages, options); }))
		return mrb_nil_value();

	mrb_value res = mrbx_pushtype(mrb, shader);
	shader->release();
	return res;
}

static mrb_value w_set_shader(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[1];
	mrbx_get_kwargs(mrb, {"shader"}, 0, v);
	if (mrb_undef_p(v[0]) || mrb_nil_p(v[0]))
		instance()->setShader();
	else
		instance()->setShader(mrbx_checktype<Shader>(mrb, v[0]));
	return mrb_nil_value();
}

static mrb_value w_get_shader(mrb_state *mrb, mrb_value self)
{
	(void) self;
	Shader *shader = instance()->getShader();
	if (shader == nullptr)
		return mrb_nil_value();
	return mrbx_pushtype(mrb, shader);
}

// =========================================================================
// Canvas / render targets. new_canvas returns a render-target Texture (modern
// LÖVE merged Canvas into Texture); set_canvas / get_canvas swap the active
// render target(s). A single 2D target or an Array of them (MRT) is supported;
// the slice/mipmap/explicit-depthstencil-texture variants are not yet.
// =========================================================================

// new_canvas(width:, height:, format:, msaa:, readable:) -- width/height
// default to the screen size. Returns a render-target Love::Texture.
static mrb_value w_new_canvas(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[5];
	mrbx_get_kwargs(mrb, {"width", "height", "format", "msaa", "readable"}, 0, v);

	Texture::Settings s;
	s.renderTarget = true;
	s.width  = mrbx_optint(mrb, v[0], instance()->getWidth());
	s.height = mrbx_optint(mrb, v[1], instance()->getHeight());
	if (!mrb_undef_p(v[2]))
	{
		std::string fmt = mrbx_checkstring(mrb, v[2]);
		PixelFormat pf;
		if (!love::getConstant(fmt.c_str(), pf))
			mrb_raisef(mrb, E_ARGUMENT_ERROR, "Invalid pixel format: %s", fmt.c_str());
		s.format = pf;
	}
	if (!mrb_undef_p(v[3]))
		s.msaa = mrbx_checkint(mrb, v[3]);
	if (!mrb_undef_p(v[4]))
		s.readable.set(mrbx_checkboolean(mrb, v[4]));
	s.dpiScale = instance()->getScreenDPIScale();

	Texture *texture = nullptr;
	if (mrbx_catchexcept(mrb, [&]() { texture = instance()->newTexture(s); }))
		return mrb_nil_value();
	mrb_value res = mrbx_pushtype(mrb, texture);
	texture->release();
	return res;
}

// set_canvas(canvas:) -- a render-target Texture, an Array of them (MRT), or
// nil/omitted to reset to the backbuffer. stencil:/depth: request a temporary
// depth/stencil buffer (needed for stencil tests while drawing to a canvas).
static mrb_value w_set_canvas(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[3];
	mrbx_get_kwargs(mrb, {"canvas", "stencil", "depth"}, 0, v);

	if (mrb_undef_p(v[0]) || mrb_nil_p(v[0]))
	{
		instance()->setRenderTarget();
		return mrb_nil_value();
	}

	Graphics::RenderTargets targets;
	if (mrb_array_p(v[0]))
	{
		mrb_int n = RARRAY_LEN(v[0]);
		for (mrb_int i = 0; i < n; i++)
			targets.colors.emplace_back(mrbx_checktype<Texture>(mrb, mrb_ary_ref(mrb, v[0], i)), 0);
	}
	else
		targets.colors.emplace_back(mrbx_checktype<Texture>(mrb, v[0]), 0);

	if (mrbx_optboolean(mrb, v[1], false))
		targets.temporaryRTFlags |= Graphics::TEMPORARY_RT_STENCIL;
	if (mrbx_optboolean(mrb, v[2], false))
		targets.temporaryRTFlags |= Graphics::TEMPORARY_RT_DEPTH;

	mrbx_catchexcept(mrb, [&]() { instance()->setRenderTargets(targets); });
	return mrb_nil_value();
}

// get_canvas -> the active render-target Texture, an Array (for MRT), or nil
// when drawing to the backbuffer.
static mrb_value w_get_canvas(mrb_state *mrb, mrb_value self)
{
	(void) self;
	Graphics::RenderTargets targets = instance()->getRenderTargets();
	int n = (int) targets.colors.size();
	if (n == 0)
		return mrb_nil_value();
	if (n == 1)
		return mrbx_pushtype(mrb, targets.colors[0].texture);
	mrb_value arr = mrb_ary_new_capa(mrb, n);
	for (int i = 0; i < n; i++)
		mrb_ary_push(mrb, arr, mrbx_pushtype(mrb, targets.colors[i].texture));
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
	{ "push",                 w_push,                 MRB_ARGS_KEY(2, 0) },
	{ "pop",                  w_pop,                  MRB_ARGS_NONE() },
	{ "translate",            w_translate,            MRB_ARGS_KEY(2, 0) },
	{ "rotate",               w_rotate,               MRB_ARGS_KEY(1, 0) },
	{ "scale",                w_scale,                MRB_ARGS_KEY(2, 0) },
	{ "shear",                w_shear,                MRB_ARGS_KEY(2, 0) },
	{ "apply_transform",      w_apply_transform,      MRB_ARGS_KEY(1, 0) },
	{ "replace_transform",    w_replace_transform,    MRB_ARGS_KEY(1, 0) },
	{ "transform_point",      w_transform_point,      MRB_ARGS_KEY(2, 0) },
	{ "inverse_transform_point", w_inverse_transform_point, MRB_ARGS_KEY(2, 0) },
	{ "set_blend_mode",       w_set_blend_mode,       MRB_ARGS_KEY(2, 0) },
	{ "get_blend_mode",       w_get_blend_mode,       MRB_ARGS_NONE() },
	{ "set_scissor",          w_set_scissor,          MRB_ARGS_KEY(4, 0) },
	{ "intersect_scissor",    w_intersect_scissor,    MRB_ARGS_KEY(4, 0) },
	{ "get_scissor",          w_get_scissor,          MRB_ARGS_NONE() },
	{ "set_color_mask",       w_set_color_mask,       MRB_ARGS_KEY(4, 0) },
	{ "get_color_mask",       w_get_color_mask,       MRB_ARGS_NONE() },
	{ "set_line_width",       w_set_line_width,       MRB_ARGS_KEY(1, 0) },
	{ "get_line_width",       w_get_line_width,       MRB_ARGS_NONE() },
	{ "set_line_style",       w_set_line_style,       MRB_ARGS_KEY(1, 0) },
	{ "get_line_style",       w_get_line_style,       MRB_ARGS_NONE() },
	{ "set_line_join",        w_set_line_join,        MRB_ARGS_KEY(1, 0) },
	{ "get_line_join",        w_get_line_join,        MRB_ARGS_NONE() },
	{ "set_point_size",       w_set_point_size,       MRB_ARGS_KEY(1, 0) },
	{ "get_point_size",       w_get_point_size,       MRB_ARGS_NONE() },
	{ "set_wireframe",        w_set_wireframe,        MRB_ARGS_KEY(1, 0) },
	{ "wireframe?",           w_is_wireframe,         MRB_ARGS_NONE() },
	{ "present",              w_present,              MRB_ARGS_NONE() },
	{ "get_width",            w_get_width,            MRB_ARGS_NONE() },
	{ "get_height",           w_get_height,           MRB_ARGS_NONE() },
	{ "get_dimensions",       w_get_dimensions,       MRB_ARGS_NONE() },
	{ "new_image",            w_new_image,            MRB_ARGS_KEY(2, 0) },
	{ "new_quad",             w_new_quad,             MRB_ARGS_KEY(7, 0) },
	{ "draw",                 w_draw,                 MRB_ARGS_KEY(11, 0) },
	{ "new_font",             w_new_font,             MRB_ARGS_KEY(2, 0) },
	{ "set_font",             w_set_font,             MRB_ARGS_KEY(1, 0) },
	{ "get_font",             w_get_font,             MRB_ARGS_NONE() },
	{ "print",                w_print,                MRB_ARGS_KEY(11, 0) },
	{ "printf",               w_printf,               MRB_ARGS_KEY(13, 0) },
	{ "new_shader",           w_new_shader,           MRB_ARGS_KEY(4, 0) },
	{ "set_shader",           w_set_shader,           MRB_ARGS_KEY(1, 0) },
	{ "get_shader",           w_get_shader,           MRB_ARGS_NONE() },
	{ "new_canvas",           w_new_canvas,           MRB_ARGS_KEY(5, 0) },
	{ "set_canvas",           w_set_canvas,           MRB_ARGS_KEY(3, 0) },
	{ "get_canvas",           w_get_canvas,           MRB_ARGS_NONE() },
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
	mrbx_register_type(mrb, Font::type, fontFunctions);
	mrbx_register_type(mrb, Shader::type, shaderFunctions);
}

} // graphics
} // love
