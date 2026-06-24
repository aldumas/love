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
//   (new_shader / set_shader / get_shader + the Shader type), canvas /
//   render targets (new_canvas / set_canvas / get_canvas), stencil/depth
//   render state (set_stencil_mode / set_depth_mode), and the SpriteBatch
//   TextBatch, ParticleSystem, Mesh, and Video object types (new_sprite_batch /
//   new_text_batch / new_particle_system / new_mesh / new_video)
//
// -- exercising the real batched-draw path (a rectangle goes through the default
// shader and the streaming vertex buffer). All of the graphics object types are
// now exposed on this real Graphics instance.

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
#include "SpriteBatch.h"
#include "TextBatch.h"
#include "ParticleSystem.h"
#include "Mesh.h"
#include "Buffer.h"
#include "GraphicsReadback.h"
#include "Video.h"
#include "vertex.h"
#include "data/ByteData.h"

#include <limits>
#include "video/VideoStream.h"
#include "video/Video.h"
#include "audio/Audio.h"
#include "audio/Source.h"
#include "sound/Sound.h"
#include "sound/Decoder.h"
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

// set_stencil_mode(mode:, value:) -- mode omitted resets to "off"; value
// defaults to 1. Drawing with a stencil mode reads/writes the stencil buffer
// (request one via set_canvas(stencil: true) for a canvas, or the window's
// stencil backbuffer).
static mrb_value w_set_stencil_mode(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[2];
	mrbx_get_kwargs(mrb, {"mode", "value"}, 0, v);

	if (mrb_undef_p(v[0]))
	{
		mrbx_catchexcept(mrb, [&]() { instance()->setStencilMode(); });
		return mrb_nil_value();
	}

	std::string modestr = mrbx_checkstring(mrb, v[0]);
	StencilMode mode;
	if (!getConstant(modestr.c_str(), mode))
		mrb_raisef(mrb, E_ARGUMENT_ERROR, "Invalid stencil mode: %s", modestr.c_str());
	int value = mrbx_optint(mrb, v[1], 1);
	mrbx_catchexcept(mrb, [&]() { instance()->setStencilMode(mode, value); });
	return mrb_nil_value();
}

// get_stencil_mode -> Hash {mode:, value:}.
static mrb_value w_get_stencil_mode(mrb_state *mrb, mrb_value self)
{
	(void) self;
	int value = 0;
	StencilMode mode = instance()->getStencilMode(value);
	const char *modestr = nullptr;
	getConstant(mode, modestr);
	mrb_value out = mrb_hash_new(mrb);
	hset(mrb, out, "mode", mrbx_string(mrb, modestr ? modestr : ""));
	hset(mrb, out, "value", mrbx_integer(mrb, value));
	return out;
}

// set_stencil_state(action:, compare:, value:, read_mask:, write_mask:) -- the
// low-level stencil API (set_stencil_mode is the convenience wrapper). No args
// resets to the default (keep/always). action is "keep"/"replace"/"increment"/
// "decrement"/"incrementwrap"/"decrementwrap"/"invert"; compare is a standard
// compare mode. value defaults 0; the masks default to all-ones.
static mrb_value w_set_stencil_state(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[5];
	mrbx_get_kwargs(mrb, {"action", "compare", "value", "read_mask", "write_mask"}, 0, v);

	if (mrb_undef_p(v[0]) || mrb_nil_p(v[0]))
	{
		mrbx_catchexcept(mrb, [&]() { instance()->setStencilState(); });
		return mrb_nil_value();
	}

	StencilState s;
	std::string actionstr = mrbx_checkstring(mrb, v[0]);
	if (!getConstant(actionstr.c_str(), s.action))
		mrb_raisef(mrb, E_ARGUMENT_ERROR, "Invalid stencil draw action: %s", actionstr.c_str());
	std::string comparestr = mrbx_checkstring(mrb, v[1]);
	if (!getConstant(comparestr.c_str(), s.compare))
		mrb_raisef(mrb, E_ARGUMENT_ERROR, "Invalid compare mode: %s", comparestr.c_str());
	s.value = mrbx_optint(mrb, v[2], 0);
	s.readMask = (uint32) mrbx_optnumber(mrb, v[3], (double) 0xFFFFFFFFu);
	s.writeMask = (uint32) mrbx_optnumber(mrb, v[4], (double) 0xFFFFFFFFu);

	mrbx_catchexcept(mrb, [&]() { instance()->setStencilState(s); });
	return mrb_nil_value();
}

// get_stencil_state -> Hash {action:, compare:, value:, read_mask:, write_mask:}.
static mrb_value w_get_stencil_state(mrb_state *mrb, mrb_value self)
{
	(void) self;
	const StencilState &s = instance()->getStencilState();
	const char *actionstr = nullptr;
	getConstant(s.action, actionstr);
	const char *comparestr = nullptr;
	getConstant(s.compare, comparestr);
	mrb_value out = mrb_hash_new(mrb);
	hset(mrb, out, "action", mrbx_string(mrb, actionstr ? actionstr : ""));
	hset(mrb, out, "compare", mrbx_string(mrb, comparestr ? comparestr : ""));
	hset(mrb, out, "value", mrbx_integer(mrb, s.value));
	hset(mrb, out, "read_mask", mrbx_number(mrb, (double) s.readMask));
	hset(mrb, out, "write_mask", mrbx_number(mrb, (double) s.writeMask));
	return out;
}

// set_depth_mode(compare:, write:) -- both omitted resets to always/no-write.
static mrb_value w_set_depth_mode(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[2];
	mrbx_get_kwargs(mrb, {"compare", "write"}, 0, v);

	if (mrb_undef_p(v[0]) && mrb_undef_p(v[1]))
	{
		mrbx_catchexcept(mrb, [&]() { instance()->setDepthMode(); });
		return mrb_nil_value();
	}

	std::string str = mrbx_checkstring(mrb, v[0]);
	CompareMode compare;
	if (!getConstant(str.c_str(), compare))
		mrb_raisef(mrb, E_ARGUMENT_ERROR, "Invalid compare mode: %s", str.c_str());
	bool write = mrbx_optboolean(mrb, v[1], false);
	mrbx_catchexcept(mrb, [&]() { instance()->setDepthMode(compare, write); });
	return mrb_nil_value();
}

// get_depth_mode -> Hash {compare:, write:}.
static mrb_value w_get_depth_mode(mrb_state *mrb, mrb_value self)
{
	(void) self;
	CompareMode compare = COMPARE_ALWAYS;
	bool write = false;
	instance()->getDepthMode(compare, write);
	const char *str = nullptr;
	getConstant(compare, str);
	mrb_value out = mrb_hash_new(mrb);
	hset(mrb, out, "compare", mrbx_string(mrb, str ? str : ""));
	hset(mrb, out, "write", mrbx_boolean(mrb, write));
	return out;
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

// new_array_image(layers:, linear:) -- build a 2D **array** Texture from an
// Array of ImageData (one per layer); the layer count is the Array length.
// Mirrors love.graphics.newArrayImage (the ImageData-slice form). An array
// texture is what a SpriteBatch needs for add_layer/set_layer.
static mrb_value w_new_array_image(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[2];
	mrbx_get_kwargs(mrb, {"layers", "linear"}, 1, v);
	if (!mrb_array_p(v[0]))
		mrb_raise(mrb, E_ARGUMENT_ERROR, "layers: must be an Array of ImageData (one per layer).");
	mrb_int n = RARRAY_LEN(v[0]);
	if (n < 1)
		mrb_raise(mrb, E_ARGUMENT_ERROR, "layers: needs at least one ImageData.");

	Texture::Settings settings;
	settings.type = TEXTURE_2D_ARRAY;
	settings.linear = mrbx_optboolean(mrb, v[1], false);

	Texture::Slices slices(TEXTURE_2D_ARRAY);
	for (mrb_int i = 0; i < n; i++)
		slices.set((int) i, 0, mrbx_checktype<image::ImageData>(mrb, mrb_ary_ref(mrb, v[0], i)));

	Texture *tex = nullptr;
	if (mrbx_catchexcept(mrb, [&]() { tex = instance()->newTexture(settings, &slices); }))
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

// Parses a `text:` argument into a list of coloured segments. Accepts either a
// plain String (one white segment -- the common case) or, mirroring
// love.graphics's coloured-string table, an Array alternating colour arrays
// ([r,g,b] or [r,g,b,a], components in 0..1) and Strings: a colour applies to
// the string segments that follow it, the default being opaque white. Used by
// print/printf, new_text_batch, and TextBatch set/setf/add/addf. Faithful to
// luax_checkcoloredstring in wrap_Font.cpp.
static std::vector<love::font::ColoredString> check_colored_string(mrb_state *mrb, mrb_value v)
{
	std::vector<love::font::ColoredString> text;

	if (mrb_array_p(v))
	{
		Colorf color(1, 1, 1, 1);
		mrb_int n = RARRAY_LEN(v);
		for (mrb_int i = 0; i < n; i++)
		{
			mrb_value e = mrb_ary_ref(mrb, v, i);
			if (mrb_array_p(e))
			{
				color.r = mrbx_checkfloat(mrb, mrb_ary_ref(mrb, e, 0));
				color.g = mrbx_checkfloat(mrb, mrb_ary_ref(mrb, e, 1));
				color.b = mrbx_checkfloat(mrb, mrb_ary_ref(mrb, e, 2));
				mrb_value a = mrb_ary_ref(mrb, e, 3);
				color.a = mrb_nil_p(a) ? 1.0f : mrbx_checkfloat(mrb, a);
			}
			else
				text.push_back({ mrbx_checkstring(mrb, e), color });
		}
		return text;
	}

	// Plain String (anything else, mrbx_checkstring rejects with a type error).
	text.push_back({ mrbx_checkstring(mrb, v), Colorf(1, 1, 1, 1) });
	return text;
}

// print(text:, x:, y:, r:, sx:, sy:, ox:, oy:, kx:, ky:, font:) -- draws text
// with the current (or given) font in the current color.
static mrb_value w_print(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[11];
	mrbx_get_kwargs(mrb, {"text", "x", "y", "r", "sx", "sy", "ox", "oy",
		"kx", "ky", "font"}, 1, v);

	std::vector<love::font::ColoredString> text = check_colored_string(mrb, v[0]);

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

	std::vector<love::font::ColoredString> text = check_colored_string(mrb, v[0]);

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
// Love::SpriteBatch  (batches many quads of a single texture into one draw
// call). Faithful to wrap_SpriteBatch.cpp, including the array-texture layer
// sprites (add_layer/set_layer, `layer` 1-based) and attach_attribute (binding
// a Buffer / another Mesh's vertex buffer as a per-sprite vertex attribute).
// =========================================================================

// Build a standard transform Matrix4 from v[base..base+8] = x,y,r,sx,sy,ox,oy,
// kx,ky (all optional; sy defaults to sx).
static Matrix4 standard_transform(mrb_state *mrb, mrb_value *v, int base)
{
	float x  = mrbx_optfloat(mrb, v[base + 0], 0.0f);
	float y  = mrbx_optfloat(mrb, v[base + 1], 0.0f);
	float r  = mrbx_optfloat(mrb, v[base + 2], 0.0f);
	float sx = mrbx_optfloat(mrb, v[base + 3], 1.0f);
	float sy = mrbx_optfloat(mrb, v[base + 4], sx);
	float ox = mrbx_optfloat(mrb, v[base + 5], 0.0f);
	float oy = mrbx_optfloat(mrb, v[base + 6], 0.0f);
	float kx = mrbx_optfloat(mrb, v[base + 7], 0.0f);
	float ky = mrbx_optfloat(mrb, v[base + 8], 0.0f);
	return Matrix4(x, y, r, sx, sy, ox, oy, kx, ky);
}

// add(quad:, x:, y:, r:, sx:, sy:, ox:, oy:, kx:, ky:) -- quad optional;
// returns the 1-based sprite index.
static mrb_value w_sb_add(mrb_state *mrb, mrb_value self)
{
	SpriteBatch *t = mrbx_checktype<SpriteBatch>(mrb, self);
	mrb_value v[10];
	mrbx_get_kwargs(mrb, {"quad", "x", "y", "r", "sx", "sy", "ox", "oy", "kx", "ky"}, 0, v);
	Matrix4 m = standard_transform(mrb, v, 1);
	int index = -1;
	mrbx_catchexcept(mrb, [&]() {
		if (!mrb_undef_p(v[0]) && !mrb_nil_p(v[0]))
			index = t->add(mrbx_checktype<Quad>(mrb, v[0]), m);
		else
			index = t->add(m);
	});
	return mrbx_integer(mrb, index + 1);
}

// set(index:, quad:, x:, ...) -- overwrite the sprite at the 1-based index.
static mrb_value w_sb_set(mrb_state *mrb, mrb_value self)
{
	SpriteBatch *t = mrbx_checktype<SpriteBatch>(mrb, self);
	mrb_value v[11];
	mrbx_get_kwargs(mrb, {"index", "quad", "x", "y", "r", "sx", "sy", "ox", "oy", "kx", "ky"}, 1, v);
	int index = mrbx_checkint(mrb, v[0]) - 1;
	Matrix4 m = standard_transform(mrb, v, 2);
	mrbx_catchexcept(mrb, [&]() {
		if (!mrb_undef_p(v[1]) && !mrb_nil_p(v[1]))
			t->add(mrbx_checktype<Quad>(mrb, v[1]), m, index);
		else
			t->add(m, index);
	});
	return mrb_nil_value();
}

// add_layer(layer:, quad:, x:, y:, r:, sx:, sy:, ox:, oy:, kx:, ky:) -- add a
// sprite sampling array-texture layer `layer` (1-based). quad optional. Returns
// the 1-based sprite index.
static mrb_value w_sb_add_layer(mrb_state *mrb, mrb_value self)
{
	SpriteBatch *t = mrbx_checktype<SpriteBatch>(mrb, self);
	mrb_value v[11];
	mrbx_get_kwargs(mrb, {"layer", "quad", "x", "y", "r", "sx", "sy", "ox", "oy", "kx", "ky"}, 1, v);
	int layer = mrbx_checkint(mrb, v[0]) - 1;
	Matrix4 m = standard_transform(mrb, v, 2);
	int index = -1;
	mrbx_catchexcept(mrb, [&]() {
		if (!mrb_undef_p(v[1]) && !mrb_nil_p(v[1]))
			index = t->addLayer(layer, mrbx_checktype<Quad>(mrb, v[1]), m);
		else
			index = t->addLayer(layer, m);
	});
	return mrbx_integer(mrb, index + 1);
}

// set_layer(index:, layer:, quad:, x:, ...) -- overwrite the sprite at the
// 1-based index with an array-texture-layer sprite (`layer` 1-based).
static mrb_value w_sb_set_layer(mrb_state *mrb, mrb_value self)
{
	SpriteBatch *t = mrbx_checktype<SpriteBatch>(mrb, self);
	mrb_value v[12];
	mrbx_get_kwargs(mrb, {"index", "layer", "quad", "x", "y", "r", "sx", "sy", "ox", "oy", "kx", "ky"}, 2, v);
	int index = mrbx_checkint(mrb, v[0]) - 1;
	int layer = mrbx_checkint(mrb, v[1]) - 1;
	Matrix4 m = standard_transform(mrb, v, 3);
	mrbx_catchexcept(mrb, [&]() {
		if (!mrb_undef_p(v[2]) && !mrb_nil_p(v[2]))
			t->addLayer(layer, mrbx_checktype<Quad>(mrb, v[2]), m, index);
		else
			t->addLayer(layer, m, index);
	});
	return mrb_nil_value();
}

// attach_attribute(name:, buffer:/mesh:) -- bind a per-sprite vertex attribute
// sourced from a Buffer (or another Mesh's vertex buffer).
static mrb_value w_sb_attach_attribute(mrb_state *mrb, mrb_value self)
{
	SpriteBatch *t = mrbx_checktype<SpriteBatch>(mrb, self);
	mrb_value v[3];
	mrbx_get_kwargs(mrb, {"name", "buffer", "mesh"}, 1, v);
	std::string name = mrbx_checkstring(mrb, v[0]);

	Buffer *buffer = nullptr;
	Mesh *mesh = nullptr;
	if (!mrb_undef_p(v[1]) && !mrb_nil_p(v[1]))
		buffer = mrbx_checktype<Buffer>(mrb, v[1]);
	else if (!mrb_undef_p(v[2]) && !mrb_nil_p(v[2]))
	{
		mesh = mrbx_checktype<Mesh>(mrb, v[2]);
		buffer = mesh->getVertexBuffer();
		if (buffer == nullptr)
			mrb_raise(mrb, E_ARGUMENT_ERROR, "Mesh does not have its own vertex buffer.");
	}
	else
		mrb_raise(mrb, E_ARGUMENT_ERROR, "attach_attribute needs buffer: or mesh:.");

	mrbx_catchexcept(mrb, [&]() { t->attachAttribute(name, buffer, mesh); });
	return mrb_nil_value();
}

static mrb_value w_sb_clear(mrb_state *mrb, mrb_value self)
{
	mrbx_checktype<SpriteBatch>(mrb, self)->clear();
	return mrb_nil_value();
}

static mrb_value w_sb_flush(mrb_state *mrb, mrb_value self)
{
	mrbx_checktype<SpriteBatch>(mrb, self)->flush();
	return mrb_nil_value();
}

static mrb_value w_sb_set_texture(mrb_state *mrb, mrb_value self)
{
	SpriteBatch *t = mrbx_checktype<SpriteBatch>(mrb, self);
	mrb_value v[1];
	mrbx_get_kwargs(mrb, {"texture"}, 1, v);
	Texture *tex = mrbx_checktype<Texture>(mrb, v[0]);
	mrbx_catchexcept(mrb, [&]() { t->setTexture(tex); });
	return mrb_nil_value();
}

static mrb_value w_sb_get_texture(mrb_state *mrb, mrb_value self)
{
	return mrbx_pushtype(mrb, mrbx_checktype<SpriteBatch>(mrb, self)->getTexture());
}

static mrb_value w_sb_set_color(mrb_state *mrb, mrb_value self)
{
	SpriteBatch *t = mrbx_checktype<SpriteBatch>(mrb, self);
	mrb_value v[4];
	mrbx_get_kwargs(mrb, {"r", "g", "b", "a"}, 3, v);
	Colorf c(mrbx_checkfloat(mrb, v[0]), mrbx_checkfloat(mrb, v[1]),
		mrbx_checkfloat(mrb, v[2]), mrbx_optfloat(mrb, v[3], 1.0f));
	t->setColor(c);
	return mrb_nil_value();
}

static mrb_value w_sb_get_color(mrb_state *mrb, mrb_value self)
{
	return colorhash(mrb, mrbx_checktype<SpriteBatch>(mrb, self)->getColor());
}

static mrb_value w_sb_get_count(mrb_state *mrb, mrb_value self)
{
	return mrbx_integer(mrb, mrbx_checktype<SpriteBatch>(mrb, self)->getCount());
}

static mrb_value w_sb_get_buffer_size(mrb_state *mrb, mrb_value self)
{
	return mrbx_integer(mrb, mrbx_checktype<SpriteBatch>(mrb, self)->getBufferSize());
}

// set_draw_range(start:, count:) -- omit both to reset to the full batch.
static mrb_value w_sb_set_draw_range(mrb_state *mrb, mrb_value self)
{
	SpriteBatch *t = mrbx_checktype<SpriteBatch>(mrb, self);
	mrb_value v[2];
	mrbx_get_kwargs(mrb, {"start", "count"}, 0, v);
	if (mrb_undef_p(v[0]) && mrb_undef_p(v[1]))
		t->setDrawRange();
	else
	{
		int start = mrbx_checkint(mrb, v[0]) - 1;
		int count = mrbx_checkint(mrb, v[1]);
		mrbx_catchexcept(mrb, [&]() { t->setDrawRange(start, count); });
	}
	return mrb_nil_value();
}

// get_draw_range -> Hash {start:, count:} (1-based start), or nil if unset.
static mrb_value w_sb_get_draw_range(mrb_state *mrb, mrb_value self)
{
	SpriteBatch *t = mrbx_checktype<SpriteBatch>(mrb, self);
	int start = 0, count = 1;
	if (!t->getDrawRange(start, count))
		return mrb_nil_value();
	mrb_value out = mrb_hash_new(mrb);
	hset(mrb, out, "start", mrbx_integer(mrb, start + 1));
	hset(mrb, out, "count", mrbx_integer(mrb, count));
	return out;
}

static const MrbReg spriteBatchFunctions[] =
{
	{ "add",             w_sb_add,             MRB_ARGS_KEY(10, 0) },
	{ "set",             w_sb_set,             MRB_ARGS_KEY(11, 0) },
	{ "add_layer",       w_sb_add_layer,       MRB_ARGS_KEY(11, 0) },
	{ "set_layer",       w_sb_set_layer,       MRB_ARGS_KEY(12, 0) },
	{ "attach_attribute", w_sb_attach_attribute, MRB_ARGS_KEY(3, 0) },
	{ "clear",           w_sb_clear,           MRB_ARGS_NONE() },
	{ "flush",           w_sb_flush,           MRB_ARGS_NONE() },
	{ "set_texture",     w_sb_set_texture,     MRB_ARGS_KEY(1, 0) },
	{ "get_texture",     w_sb_get_texture,     MRB_ARGS_NONE() },
	{ "set_color",       w_sb_set_color,       MRB_ARGS_KEY(4, 0) },
	{ "get_color",       w_sb_get_color,       MRB_ARGS_NONE() },
	{ "get_count",       w_sb_get_count,       MRB_ARGS_NONE() },
	{ "get_buffer_size", w_sb_get_buffer_size, MRB_ARGS_NONE() },
	{ "set_draw_range",  w_sb_set_draw_range,  MRB_ARGS_KEY(2, 0) },
	{ "get_draw_range",  w_sb_get_draw_range,  MRB_ARGS_NONE() },
	{ nullptr, nullptr, 0 }
};

// new_sprite_batch(texture:, size:, usage:) -- size defaults to 1000, usage to
// "dynamic" ("static"/"stream" also accepted).
static mrb_value w_new_sprite_batch(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[3];
	mrbx_get_kwargs(mrb, {"texture", "size", "usage"}, 1, v);
	Texture *tex = mrbx_checktype<Texture>(mrb, v[0]);
	int size = mrbx_optint(mrb, v[1], 1000);
	BufferDataUsage usage = BUFFERDATAUSAGE_DYNAMIC;
	if (!mrb_undef_p(v[2]))
	{
		std::string str = mrbx_checkstring(mrb, v[2]);
		if (!getConstant(str.c_str(), usage))
			mrb_raisef(mrb, E_ARGUMENT_ERROR, "Invalid usage hint: %s", str.c_str());
	}

	SpriteBatch *t = nullptr;
	if (mrbx_catchexcept(mrb, [&]() { t = instance()->newSpriteBatch(tex, size, usage); }))
		return mrb_nil_value();
	mrb_value res = mrbx_pushtype(mrb, t);
	t->release();
	return res;
}

// =========================================================================
// Love::TextBatch  (pre-laid-out text drawn through a font's glyph atlas).
// `text:` is a plain String or the colored-string-segments Array form (see
// check_colored_string); align names come from the Font enum, as in print/printf.
// =========================================================================

static Font::AlignMode check_align(mrb_state *mrb, mrb_value v)
{
	Font::AlignMode align = Font::ALIGN_LEFT;
	std::string str = mrbx_checkstring(mrb, v);
	if (!Font::getConstant(str.c_str(), align))
		mrb_raisef(mrb, E_ARGUMENT_ERROR, "Invalid alignment: %s", str.c_str());
	return align;
}

static mrb_value w_tb_set(mrb_state *mrb, mrb_value self)
{
	TextBatch *t = mrbx_checktype<TextBatch>(mrb, self);
	mrb_value v[1];
	mrbx_get_kwargs(mrb, {"text"}, 1, v);
	auto text = check_colored_string(mrb, v[0]);
	mrbx_catchexcept(mrb, [&]() { t->set(text); });
	return mrb_nil_value();
}

static mrb_value w_tb_setf(mrb_state *mrb, mrb_value self)
{
	TextBatch *t = mrbx_checktype<TextBatch>(mrb, self);
	mrb_value v[3];
	mrbx_get_kwargs(mrb, {"text", "wrap", "align"}, 3, v);
	auto text = check_colored_string(mrb, v[0]);
	float wrap = mrbx_checkfloat(mrb, v[1]);
	Font::AlignMode align = check_align(mrb, v[2]);
	mrbx_catchexcept(mrb, [&]() { t->set(text, wrap, align); });
	return mrb_nil_value();
}

// add(text:, x:, y:, r:, sx:, sy:, ox:, oy:, kx:, ky:) -> 1-based index.
static mrb_value w_tb_add(mrb_state *mrb, mrb_value self)
{
	TextBatch *t = mrbx_checktype<TextBatch>(mrb, self);
	mrb_value v[10];
	mrbx_get_kwargs(mrb, {"text", "x", "y", "r", "sx", "sy", "ox", "oy", "kx", "ky"}, 1, v);
	auto text = check_colored_string(mrb, v[0]);
	Matrix4 m = standard_transform(mrb, v, 1);
	int index = 0;
	mrbx_catchexcept(mrb, [&]() { index = t->add(text, m); });
	return mrbx_integer(mrb, index + 1);
}

// addf(text:, wrap:, align:, x:, ...) -> 1-based index.
static mrb_value w_tb_addf(mrb_state *mrb, mrb_value self)
{
	TextBatch *t = mrbx_checktype<TextBatch>(mrb, self);
	mrb_value v[12];
	mrbx_get_kwargs(mrb, {"text", "wrap", "align", "x", "y", "r", "sx", "sy", "ox", "oy", "kx", "ky"}, 3, v);
	auto text = check_colored_string(mrb, v[0]);
	float wrap = mrbx_checkfloat(mrb, v[1]);
	Font::AlignMode align = check_align(mrb, v[2]);
	Matrix4 m = standard_transform(mrb, v, 3);
	int index = 0;
	mrbx_catchexcept(mrb, [&]() { index = t->addf(text, wrap, align, m); });
	return mrbx_integer(mrb, index + 1);
}

static mrb_value w_tb_clear(mrb_state *mrb, mrb_value self)
{
	TextBatch *t = mrbx_checktype<TextBatch>(mrb, self);
	mrbx_catchexcept(mrb, [&]() { t->clear(); });
	return mrb_nil_value();
}

static mrb_value w_tb_set_font(mrb_state *mrb, mrb_value self)
{
	TextBatch *t = mrbx_checktype<TextBatch>(mrb, self);
	mrb_value v[1];
	mrbx_get_kwargs(mrb, {"font"}, 1, v);
	Font *f = mrbx_checktype<Font>(mrb, v[0]);
	mrbx_catchexcept(mrb, [&]() { t->setFont(f); });
	return mrb_nil_value();
}

static mrb_value w_tb_get_font(mrb_state *mrb, mrb_value self)
{
	return mrbx_pushtype(mrb, mrbx_checktype<TextBatch>(mrb, self)->getFont());
}

// get_width(index:) / get_height(index:) -- index optional (1-based); omitted
// measures the whole batch.
static mrb_value w_tb_get_width(mrb_state *mrb, mrb_value self)
{
	TextBatch *t = mrbx_checktype<TextBatch>(mrb, self);
	mrb_value v[1];
	mrbx_get_kwargs(mrb, {"index"}, 0, v);
	int index = mrbx_optint(mrb, v[0], 0) - 1;
	return mrbx_integer(mrb, t->getWidth(index < 0 ? 0 : index));
}

static mrb_value w_tb_get_height(mrb_state *mrb, mrb_value self)
{
	TextBatch *t = mrbx_checktype<TextBatch>(mrb, self);
	mrb_value v[1];
	mrbx_get_kwargs(mrb, {"index"}, 0, v);
	int index = mrbx_optint(mrb, v[0], 0) - 1;
	return mrbx_integer(mrb, t->getHeight(index < 0 ? 0 : index));
}

static mrb_value w_tb_get_dimensions(mrb_state *mrb, mrb_value self)
{
	TextBatch *t = mrbx_checktype<TextBatch>(mrb, self);
	mrb_value v[1];
	mrbx_get_kwargs(mrb, {"index"}, 0, v);
	int index = mrbx_optint(mrb, v[0], 0) - 1;
	if (index < 0) index = 0;
	mrb_value arr = mrb_ary_new_capa(mrb, 2);
	mrb_ary_push(mrb, arr, mrbx_integer(mrb, t->getWidth(index)));
	mrb_ary_push(mrb, arr, mrbx_integer(mrb, t->getHeight(index)));
	return arr;
}

static const MrbReg textBatchFunctions[] =
{
	{ "set",            w_tb_set,            MRB_ARGS_KEY(1, 0) },
	{ "setf",           w_tb_setf,           MRB_ARGS_KEY(3, 0) },
	{ "add",            w_tb_add,            MRB_ARGS_KEY(10, 0) },
	{ "addf",           w_tb_addf,           MRB_ARGS_KEY(12, 0) },
	{ "clear",          w_tb_clear,          MRB_ARGS_NONE() },
	{ "set_font",       w_tb_set_font,       MRB_ARGS_KEY(1, 0) },
	{ "get_font",       w_tb_get_font,       MRB_ARGS_NONE() },
	{ "get_width",      w_tb_get_width,      MRB_ARGS_KEY(1, 0) },
	{ "get_height",     w_tb_get_height,     MRB_ARGS_KEY(1, 0) },
	{ "get_dimensions", w_tb_get_dimensions, MRB_ARGS_KEY(1, 0) },
	{ nullptr, nullptr, 0 }
};

// new_text_batch(font:, text:) -- font required; text optional (a plain String
// or the colored-string-segments Array form, see check_colored_string).
static mrb_value w_new_text_batch(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[2];
	mrbx_get_kwargs(mrb, {"font", "text"}, 1, v);
	Font *font = mrbx_checktype<Font>(mrb, v[0]);

	TextBatch *t = nullptr;
	if (mrb_undef_p(v[1]) || mrb_nil_p(v[1]))
	{
		if (mrbx_catchexcept(mrb, [&]() { t = instance()->newTextBatch(font); }))
			return mrb_nil_value();
	}
	else
	{
		auto text = check_colored_string(mrb, v[1]);
		if (mrbx_catchexcept(mrb, [&]() { t = instance()->newTextBatch(font, text); }))
			return mrb_nil_value();
	}
	mrb_value res = mrbx_pushtype(mrb, t);
	t->release();
	return res;
}

// =========================================================================
// Love::ParticleSystem  (a CPU particle emitter). Faithful to
// wrap_ParticleSystem.cpp; multi-value setters/getters use min/max (or
// component) keyword args and Hash returns.
// =========================================================================

#define PS (mrbx_checktype<ParticleSystem>(mrb, self))

// clone -- an identical copy of the emitter's configuration and emitting
// state (the copy ctor carries over `active`), but with no live particles
// (activeParticles starts at 0). Faithful to wrap_ParticleSystem.cpp.
static mrb_value w_ps_clone(mrb_state *mrb, mrb_value self)
{
	ParticleSystem *clone = nullptr;
	if (mrbx_catchexcept(mrb, [&]() { clone = PS->clone(); }))
		return mrb_nil_value();
	mrb_value res = mrbx_pushtype(mrb, clone);
	clone->release();
	return res;
}

static mrb_value w_ps_set_texture(mrb_state *mrb, mrb_value self)
{
	mrb_value v[1];
	mrbx_get_kwargs(mrb, {"texture"}, 1, v);
	PS->setTexture(mrbx_checktype<Texture>(mrb, v[0]));
	return mrb_nil_value();
}

static mrb_value w_ps_get_texture(mrb_state *mrb, mrb_value self)
{
	return mrbx_pushtype(mrb, PS->getTexture());
}

static mrb_value w_ps_set_buffer_size(mrb_state *mrb, mrb_value self)
{
	mrb_value v[1];
	mrbx_get_kwargs(mrb, {"size"}, 1, v);
	mrbx_catchexcept(mrb, [&]() { PS->setBufferSize((uint32) mrbx_checkint(mrb, v[0])); });
	return mrb_nil_value();
}

static mrb_value w_ps_get_buffer_size(mrb_state *mrb, mrb_value self)
{
	return mrbx_integer(mrb, (int) PS->getBufferSize());
}

static mrb_value w_ps_set_insert_mode(mrb_state *mrb, mrb_value self)
{
	mrb_value v[1];
	mrbx_get_kwargs(mrb, {"mode"}, 1, v);
	std::string str = mrbx_checkstring(mrb, v[0]);
	ParticleSystem::InsertMode mode;
	if (!ParticleSystem::getConstant(str.c_str(), mode))
		mrb_raisef(mrb, E_ARGUMENT_ERROR, "Invalid insert mode: %s", str.c_str());
	PS->setInsertMode(mode);
	return mrb_nil_value();
}

static mrb_value w_ps_get_insert_mode(mrb_state *mrb, mrb_value self)
{
	const char *str = nullptr;
	ParticleSystem::getConstant(PS->getInsertMode(), str);
	return mrbx_string(mrb, str ? str : "");
}

static mrb_value w_ps_set_emission_rate(mrb_state *mrb, mrb_value self)
{
	mrb_value v[1];
	mrbx_get_kwargs(mrb, {"rate"}, 1, v);
	mrbx_catchexcept(mrb, [&]() { PS->setEmissionRate(mrbx_checkfloat(mrb, v[0])); });
	return mrb_nil_value();
}

static mrb_value w_ps_get_emission_rate(mrb_state *mrb, mrb_value self)
{
	return mrbx_number(mrb, PS->getEmissionRate());
}

static mrb_value w_ps_set_emitter_lifetime(mrb_state *mrb, mrb_value self)
{
	mrb_value v[1];
	mrbx_get_kwargs(mrb, {"life"}, 1, v);
	PS->setEmitterLifetime(mrbx_checkfloat(mrb, v[0]));
	return mrb_nil_value();
}

static mrb_value w_ps_get_emitter_lifetime(mrb_state *mrb, mrb_value self)
{
	return mrbx_number(mrb, PS->getEmitterLifetime());
}

// set_particle_lifetime(min:, max:) -- max defaults to min.
static mrb_value w_ps_set_particle_lifetime(mrb_state *mrb, mrb_value self)
{
	mrb_value v[2];
	mrbx_get_kwargs(mrb, {"min", "max"}, 1, v);
	float min = mrbx_checkfloat(mrb, v[0]);
	PS->setParticleLifetime(min, mrbx_optfloat(mrb, v[1], min));
	return mrb_nil_value();
}

static mrb_value w_ps_get_particle_lifetime(mrb_state *mrb, mrb_value self)
{
	float min = 0, max = 0;
	PS->getParticleLifetime(min, max);
	mrb_value out = mrb_hash_new(mrb);
	hset(mrb, out, "min", mrbx_number(mrb, min));
	hset(mrb, out, "max", mrbx_number(mrb, max));
	return out;
}

static mrb_value w_ps_set_position(mrb_state *mrb, mrb_value self)
{
	mrb_value v[2];
	mrbx_get_kwargs(mrb, {"x", "y"}, 2, v);
	PS->setPosition(mrbx_checkfloat(mrb, v[0]), mrbx_checkfloat(mrb, v[1]));
	return mrb_nil_value();
}

static mrb_value w_ps_get_position(mrb_state *mrb, mrb_value self)
{
	const Vector2 &p = PS->getPosition();
	mrb_value out = mrb_hash_new(mrb);
	hset(mrb, out, "x", mrbx_number(mrb, p.x));
	hset(mrb, out, "y", mrbx_number(mrb, p.y));
	return out;
}

static mrb_value w_ps_move_to(mrb_state *mrb, mrb_value self)
{
	mrb_value v[2];
	mrbx_get_kwargs(mrb, {"x", "y"}, 2, v);
	PS->moveTo(mrbx_checkfloat(mrb, v[0]), mrbx_checkfloat(mrb, v[1]));
	return mrb_nil_value();
}

// set_emission_area(distribution:, x:, y:, angle:, direction_relative:)
static mrb_value w_ps_set_emission_area(mrb_state *mrb, mrb_value self)
{
	mrb_value v[5];
	mrbx_get_kwargs(mrb, {"distribution", "x", "y", "angle", "direction_relative"}, 1, v);
	std::string str = mrbx_checkstring(mrb, v[0]);
	ParticleSystem::AreaSpreadDistribution dist;
	if (!ParticleSystem::getConstant(str.c_str(), dist))
		mrb_raisef(mrb, E_ARGUMENT_ERROR, "Invalid particle distribution: %s", str.c_str());

	float x = 0, y = 0, angle = 0;
	bool dirrel = false;
	if (dist != ParticleSystem::DISTRIBUTION_NONE)
	{
		x = mrbx_checkfloat(mrb, v[1]);
		y = mrbx_checkfloat(mrb, v[2]);
		angle = mrbx_optfloat(mrb, v[3], 0.0f);
		dirrel = mrbx_optboolean(mrb, v[4], false);
	}
	PS->setEmissionArea(dist, x, y, angle, dirrel);
	return mrb_nil_value();
}

static mrb_value w_ps_get_emission_area(mrb_state *mrb, mrb_value self)
{
	Vector2 params;
	float angle = 0;
	bool dirrel = false;
	ParticleSystem::AreaSpreadDistribution dist = PS->getEmissionArea(params, angle, dirrel);
	const char *str = nullptr;
	ParticleSystem::getConstant(dist, str);
	mrb_value out = mrb_hash_new(mrb);
	hset(mrb, out, "distribution", mrbx_string(mrb, str ? str : ""));
	hset(mrb, out, "x", mrbx_number(mrb, params.x));
	hset(mrb, out, "y", mrbx_number(mrb, params.y));
	hset(mrb, out, "angle", mrbx_number(mrb, angle));
	hset(mrb, out, "direction_relative", mrbx_boolean(mrb, dirrel));
	return out;
}

static mrb_value w_ps_set_direction(mrb_state *mrb, mrb_value self)
{
	mrb_value v[1];
	mrbx_get_kwargs(mrb, {"direction"}, 1, v);
	PS->setDirection(mrbx_checkfloat(mrb, v[0]));
	return mrb_nil_value();
}

static mrb_value w_ps_get_direction(mrb_state *mrb, mrb_value self)
{
	return mrbx_number(mrb, PS->getDirection());
}

static mrb_value w_ps_set_spread(mrb_state *mrb, mrb_value self)
{
	mrb_value v[1];
	mrbx_get_kwargs(mrb, {"spread"}, 1, v);
	PS->setSpread(mrbx_checkfloat(mrb, v[0]));
	return mrb_nil_value();
}

static mrb_value w_ps_get_spread(mrb_state *mrb, mrb_value self)
{
	return mrbx_number(mrb, PS->getSpread());
}

// set_speed(min:, max:) -- max defaults to min.
static mrb_value w_ps_set_speed(mrb_state *mrb, mrb_value self)
{
	mrb_value v[2];
	mrbx_get_kwargs(mrb, {"min", "max"}, 1, v);
	float min = mrbx_checkfloat(mrb, v[0]);
	PS->setSpeed(min, mrbx_optfloat(mrb, v[1], min));
	return mrb_nil_value();
}

static mrb_value minmax_hash(mrb_state *mrb, float min, float max)
{
	mrb_value out = mrb_hash_new(mrb);
	hset(mrb, out, "min", mrbx_number(mrb, min));
	hset(mrb, out, "max", mrbx_number(mrb, max));
	return out;
}

static mrb_value w_ps_get_speed(mrb_state *mrb, mrb_value self)
{
	float min = 0, max = 0;
	PS->getSpeed(min, max);
	return minmax_hash(mrb, min, max);
}

// set_linear_acceleration(xmin:, ymin:, xmax:, ymax:) -- max defaults to min.
static mrb_value w_ps_set_linear_acceleration(mrb_state *mrb, mrb_value self)
{
	mrb_value v[4];
	mrbx_get_kwargs(mrb, {"xmin", "ymin", "xmax", "ymax"}, 2, v);
	float xmin = mrbx_checkfloat(mrb, v[0]);
	float ymin = mrbx_checkfloat(mrb, v[1]);
	PS->setLinearAcceleration(xmin, ymin, mrbx_optfloat(mrb, v[2], xmin), mrbx_optfloat(mrb, v[3], ymin));
	return mrb_nil_value();
}

static mrb_value w_ps_get_linear_acceleration(mrb_state *mrb, mrb_value self)
{
	Vector2 min, max;
	PS->getLinearAcceleration(min, max);
	mrb_value out = mrb_hash_new(mrb);
	hset(mrb, out, "xmin", mrbx_number(mrb, min.x));
	hset(mrb, out, "ymin", mrbx_number(mrb, min.y));
	hset(mrb, out, "xmax", mrbx_number(mrb, max.x));
	hset(mrb, out, "ymax", mrbx_number(mrb, max.y));
	return out;
}

static mrb_value w_ps_set_radial_acceleration(mrb_state *mrb, mrb_value self)
{
	mrb_value v[2];
	mrbx_get_kwargs(mrb, {"min", "max"}, 1, v);
	float min = mrbx_checkfloat(mrb, v[0]);
	PS->setRadialAcceleration(min, mrbx_optfloat(mrb, v[1], min));
	return mrb_nil_value();
}

static mrb_value w_ps_get_radial_acceleration(mrb_state *mrb, mrb_value self)
{
	float min = 0, max = 0;
	PS->getRadialAcceleration(min, max);
	return minmax_hash(mrb, min, max);
}

static mrb_value w_ps_set_tangential_acceleration(mrb_state *mrb, mrb_value self)
{
	mrb_value v[2];
	mrbx_get_kwargs(mrb, {"min", "max"}, 1, v);
	float min = mrbx_checkfloat(mrb, v[0]);
	PS->setTangentialAcceleration(min, mrbx_optfloat(mrb, v[1], min));
	return mrb_nil_value();
}

static mrb_value w_ps_get_tangential_acceleration(mrb_state *mrb, mrb_value self)
{
	float min = 0, max = 0;
	PS->getTangentialAcceleration(min, max);
	return minmax_hash(mrb, min, max);
}

static mrb_value w_ps_set_linear_damping(mrb_state *mrb, mrb_value self)
{
	mrb_value v[2];
	mrbx_get_kwargs(mrb, {"min", "max"}, 1, v);
	float min = mrbx_checkfloat(mrb, v[0]);
	PS->setLinearDamping(min, mrbx_optfloat(mrb, v[1], min));
	return mrb_nil_value();
}

static mrb_value w_ps_get_linear_damping(mrb_state *mrb, mrb_value self)
{
	float min = 0, max = 0;
	PS->getLinearDamping(min, max);
	return minmax_hash(mrb, min, max);
}

// set_sizes(sizes:) -- an Array of up to 8 floats (or a single number).
static mrb_value w_ps_set_sizes(mrb_state *mrb, mrb_value self)
{
	ParticleSystem *t = PS;
	mrb_value v[1];
	mrbx_get_kwargs(mrb, {"sizes"}, 1, v);
	if (mrb_array_p(v[0]))
	{
		std::vector<float> sizes;
		mrb_int n = RARRAY_LEN(v[0]);
		for (mrb_int i = 0; i < n; i++)
			sizes.push_back(mrbx_checkfloat(mrb, mrb_ary_ref(mrb, v[0], i)));
		if (sizes.size() == 1)
			t->setSize(sizes[0]);
		else
			t->setSizes(sizes);
	}
	else
		t->setSize(mrbx_checkfloat(mrb, v[0]));
	return mrb_nil_value();
}

static mrb_value w_ps_get_sizes(mrb_state *mrb, mrb_value self)
{
	const std::vector<float> &sizes = PS->getSizes();
	mrb_value arr = mrb_ary_new_capa(mrb, (mrb_int) sizes.size());
	for (float s : sizes)
		mrb_ary_push(mrb, arr, mrbx_number(mrb, s));
	return arr;
}

static mrb_value w_ps_set_size_variation(mrb_state *mrb, mrb_value self)
{
	mrb_value v[1];
	mrbx_get_kwargs(mrb, {"variation"}, 1, v);
	float var = mrbx_checkfloat(mrb, v[0]);
	if (var < 0.0f || var > 1.0f)
		mrb_raise(mrb, E_ARGUMENT_ERROR, "Size variation must be between 0 and 1.");
	PS->setSizeVariation(var);
	return mrb_nil_value();
}

static mrb_value w_ps_get_size_variation(mrb_state *mrb, mrb_value self)
{
	return mrbx_number(mrb, PS->getSizeVariation());
}

// set_rotation(min:, max:) -- max defaults to min.
static mrb_value w_ps_set_rotation(mrb_state *mrb, mrb_value self)
{
	mrb_value v[2];
	mrbx_get_kwargs(mrb, {"min", "max"}, 1, v);
	float min = mrbx_checkfloat(mrb, v[0]);
	PS->setRotation(min, mrbx_optfloat(mrb, v[1], min));
	return mrb_nil_value();
}

static mrb_value w_ps_get_rotation(mrb_state *mrb, mrb_value self)
{
	float min = 0, max = 0;
	PS->getRotation(min, max);
	return minmax_hash(mrb, min, max);
}

// set_spin(start:, end:) -- end defaults to start.
static mrb_value w_ps_set_spin(mrb_state *mrb, mrb_value self)
{
	mrb_value v[2];
	mrbx_get_kwargs(mrb, {"start", "end"}, 1, v);
	float start = mrbx_checkfloat(mrb, v[0]);
	PS->setSpin(start, mrbx_optfloat(mrb, v[1], start));
	return mrb_nil_value();
}

static mrb_value w_ps_get_spin(mrb_state *mrb, mrb_value self)
{
	float start = 0, end = 0;
	PS->getSpin(start, end);
	mrb_value out = mrb_hash_new(mrb);
	hset(mrb, out, "start", mrbx_number(mrb, start));
	hset(mrb, out, "end", mrbx_number(mrb, end));
	return out;
}

static mrb_value w_ps_set_spin_variation(mrb_state *mrb, mrb_value self)
{
	mrb_value v[1];
	mrbx_get_kwargs(mrb, {"variation"}, 1, v);
	PS->setSpinVariation(mrbx_checkfloat(mrb, v[0]));
	return mrb_nil_value();
}

static mrb_value w_ps_get_spin_variation(mrb_state *mrb, mrb_value self)
{
	return mrbx_number(mrb, PS->getSpinVariation());
}

static mrb_value w_ps_set_offset(mrb_state *mrb, mrb_value self)
{
	mrb_value v[2];
	mrbx_get_kwargs(mrb, {"x", "y"}, 2, v);
	PS->setOffset(mrbx_checkfloat(mrb, v[0]), mrbx_checkfloat(mrb, v[1]));
	return mrb_nil_value();
}

static mrb_value w_ps_get_offset(mrb_state *mrb, mrb_value self)
{
	Vector2 o = PS->getOffset();
	mrb_value out = mrb_hash_new(mrb);
	hset(mrb, out, "x", mrbx_number(mrb, o.x));
	hset(mrb, out, "y", mrbx_number(mrb, o.y));
	return out;
}

// set_colors(colors:) -- an Array of [r,g,b,a] arrays (up to 8).
static mrb_value w_ps_set_colors(mrb_state *mrb, mrb_value self)
{
	ParticleSystem *t = PS;
	mrb_value v[1];
	mrbx_get_kwargs(mrb, {"colors"}, 1, v);
	if (!mrb_array_p(v[0]))
		mrb_raise(mrb, E_ARGUMENT_ERROR, "colors: must be an Array of [r,g,b,a] arrays.");
	mrb_int n = RARRAY_LEN(v[0]);
	std::vector<Colorf> colors;
	for (mrb_int i = 0; i < n; i++)
	{
		mrb_value c = mrb_ary_ref(mrb, v[0], i);
		colors.emplace_back(
			mrbx_checkfloat(mrb, mrb_ary_ref(mrb, c, 0)),
			mrbx_checkfloat(mrb, mrb_ary_ref(mrb, c, 1)),
			mrbx_checkfloat(mrb, mrb_ary_ref(mrb, c, 2)),
			mrb_undef_p(mrb_ary_ref(mrb, c, 3)) ? 1.0f : mrbx_checkfloat(mrb, mrb_ary_ref(mrb, c, 3)));
	}
	t->setColor(colors);
	return mrb_nil_value();
}

static mrb_value w_ps_get_colors(mrb_state *mrb, mrb_value self)
{
	const std::vector<Colorf> &colors = PS->getColor();
	mrb_value arr = mrb_ary_new_capa(mrb, (mrb_int) colors.size());
	for (const Colorf &c : colors)
	{
		mrb_value one = mrb_ary_new_capa(mrb, 4);
		mrb_ary_push(mrb, one, mrbx_number(mrb, c.r));
		mrb_ary_push(mrb, one, mrbx_number(mrb, c.g));
		mrb_ary_push(mrb, one, mrbx_number(mrb, c.b));
		mrb_ary_push(mrb, one, mrbx_number(mrb, c.a));
		mrb_ary_push(mrb, arr, one);
	}
	return arr;
}

// set_quads(quads:) -- an Array of Love::Quad (empty/omitted clears).
static mrb_value w_ps_set_quads(mrb_state *mrb, mrb_value self)
{
	ParticleSystem *t = PS;
	mrb_value v[1];
	mrbx_get_kwargs(mrb, {"quads"}, 0, v);
	std::vector<Quad *> quads;
	if (!mrb_undef_p(v[0]) && mrb_array_p(v[0]))
	{
		mrb_int n = RARRAY_LEN(v[0]);
		for (mrb_int i = 0; i < n; i++)
			quads.push_back(mrbx_checktype<Quad>(mrb, mrb_ary_ref(mrb, v[0], i)));
	}
	t->setQuads(quads);
	return mrb_nil_value();
}

static mrb_value w_ps_get_quads(mrb_state *mrb, mrb_value self)
{
	const std::vector<Quad *> &quads = PS->getQuads();
	mrb_value arr = mrb_ary_new_capa(mrb, (mrb_int) quads.size());
	for (Quad *q : quads)
		mrb_ary_push(mrb, arr, mrbx_pushtype(mrb, q));
	return arr;
}

static mrb_value w_ps_set_relative_rotation(mrb_state *mrb, mrb_value self)
{
	mrb_value v[1];
	mrbx_get_kwargs(mrb, {"enable"}, 1, v);
	PS->setRelativeRotation(mrbx_checkboolean(mrb, v[0]));
	return mrb_nil_value();
}

static mrb_value w_ps_has_relative_rotation(mrb_state *mrb, mrb_value self)
{
	return mrbx_boolean(mrb, PS->hasRelativeRotation());
}

static mrb_value w_ps_get_count(mrb_state *mrb, mrb_value self)
{
	return mrbx_integer(mrb, PS->getCount());
}

static mrb_value w_ps_start(mrb_state *mrb, mrb_value self)  { PS->start(); return mrb_nil_value(); }
static mrb_value w_ps_stop(mrb_state *mrb, mrb_value self)   { PS->stop();  return mrb_nil_value(); }
static mrb_value w_ps_pause(mrb_state *mrb, mrb_value self)  { PS->pause(); return mrb_nil_value(); }
static mrb_value w_ps_reset(mrb_state *mrb, mrb_value self)  { PS->reset(); return mrb_nil_value(); }

static mrb_value w_ps_emit(mrb_state *mrb, mrb_value self)
{
	mrb_value v[1];
	mrbx_get_kwargs(mrb, {"count"}, 1, v);
	PS->emit(mrbx_checkint(mrb, v[0]));
	return mrb_nil_value();
}

static mrb_value w_ps_update(mrb_state *mrb, mrb_value self)
{
	mrb_value v[1];
	mrbx_get_kwargs(mrb, {"dt"}, 1, v);
	PS->update(mrbx_checkfloat(mrb, v[0]));
	return mrb_nil_value();
}

static mrb_value w_ps_is_active(mrb_state *mrb, mrb_value self)  { return mrbx_boolean(mrb, PS->isActive()); }
static mrb_value w_ps_is_paused(mrb_state *mrb, mrb_value self)  { return mrbx_boolean(mrb, PS->isPaused()); }
static mrb_value w_ps_is_stopped(mrb_state *mrb, mrb_value self) { return mrbx_boolean(mrb, PS->isStopped()); }
static mrb_value w_ps_is_empty(mrb_state *mrb, mrb_value self)   { return mrbx_boolean(mrb, PS->isEmpty()); }
static mrb_value w_ps_is_full(mrb_state *mrb, mrb_value self)    { return mrbx_boolean(mrb, PS->isFull()); }

#undef PS

static const MrbReg particleSystemFunctions[] =
{
	{ "set_texture",                 w_ps_set_texture,                 MRB_ARGS_KEY(1, 0) },
	{ "get_texture",                 w_ps_get_texture,                 MRB_ARGS_NONE() },
	{ "set_buffer_size",             w_ps_set_buffer_size,             MRB_ARGS_KEY(1, 0) },
	{ "get_buffer_size",             w_ps_get_buffer_size,             MRB_ARGS_NONE() },
	{ "set_insert_mode",             w_ps_set_insert_mode,             MRB_ARGS_KEY(1, 0) },
	{ "get_insert_mode",             w_ps_get_insert_mode,             MRB_ARGS_NONE() },
	{ "set_emission_rate",           w_ps_set_emission_rate,           MRB_ARGS_KEY(1, 0) },
	{ "get_emission_rate",           w_ps_get_emission_rate,           MRB_ARGS_NONE() },
	{ "set_emitter_lifetime",        w_ps_set_emitter_lifetime,        MRB_ARGS_KEY(1, 0) },
	{ "get_emitter_lifetime",        w_ps_get_emitter_lifetime,        MRB_ARGS_NONE() },
	{ "set_particle_lifetime",       w_ps_set_particle_lifetime,       MRB_ARGS_KEY(2, 0) },
	{ "get_particle_lifetime",       w_ps_get_particle_lifetime,       MRB_ARGS_NONE() },
	{ "set_position",                w_ps_set_position,                MRB_ARGS_KEY(2, 0) },
	{ "get_position",                w_ps_get_position,                MRB_ARGS_NONE() },
	{ "move_to",                     w_ps_move_to,                     MRB_ARGS_KEY(2, 0) },
	{ "set_emission_area",           w_ps_set_emission_area,           MRB_ARGS_KEY(5, 0) },
	{ "get_emission_area",           w_ps_get_emission_area,           MRB_ARGS_NONE() },
	{ "set_direction",               w_ps_set_direction,               MRB_ARGS_KEY(1, 0) },
	{ "get_direction",               w_ps_get_direction,               MRB_ARGS_NONE() },
	{ "set_spread",                  w_ps_set_spread,                  MRB_ARGS_KEY(1, 0) },
	{ "get_spread",                  w_ps_get_spread,                  MRB_ARGS_NONE() },
	{ "set_speed",                   w_ps_set_speed,                   MRB_ARGS_KEY(2, 0) },
	{ "get_speed",                   w_ps_get_speed,                   MRB_ARGS_NONE() },
	{ "set_linear_acceleration",     w_ps_set_linear_acceleration,     MRB_ARGS_KEY(4, 0) },
	{ "get_linear_acceleration",     w_ps_get_linear_acceleration,     MRB_ARGS_NONE() },
	{ "set_radial_acceleration",     w_ps_set_radial_acceleration,     MRB_ARGS_KEY(2, 0) },
	{ "get_radial_acceleration",     w_ps_get_radial_acceleration,     MRB_ARGS_NONE() },
	{ "set_tangential_acceleration", w_ps_set_tangential_acceleration, MRB_ARGS_KEY(2, 0) },
	{ "get_tangential_acceleration", w_ps_get_tangential_acceleration, MRB_ARGS_NONE() },
	{ "set_linear_damping",          w_ps_set_linear_damping,          MRB_ARGS_KEY(2, 0) },
	{ "get_linear_damping",          w_ps_get_linear_damping,          MRB_ARGS_NONE() },
	{ "set_sizes",                   w_ps_set_sizes,                   MRB_ARGS_KEY(1, 0) },
	{ "get_sizes",                   w_ps_get_sizes,                   MRB_ARGS_NONE() },
	{ "set_size_variation",          w_ps_set_size_variation,          MRB_ARGS_KEY(1, 0) },
	{ "get_size_variation",          w_ps_get_size_variation,          MRB_ARGS_NONE() },
	{ "set_rotation",                w_ps_set_rotation,                MRB_ARGS_KEY(2, 0) },
	{ "get_rotation",                w_ps_get_rotation,                MRB_ARGS_NONE() },
	{ "set_spin",                    w_ps_set_spin,                    MRB_ARGS_KEY(2, 0) },
	{ "get_spin",                    w_ps_get_spin,                    MRB_ARGS_NONE() },
	{ "set_spin_variation",          w_ps_set_spin_variation,          MRB_ARGS_KEY(1, 0) },
	{ "get_spin_variation",          w_ps_get_spin_variation,          MRB_ARGS_NONE() },
	{ "set_offset",                  w_ps_set_offset,                  MRB_ARGS_KEY(2, 0) },
	{ "get_offset",                  w_ps_get_offset,                  MRB_ARGS_NONE() },
	{ "set_colors",                  w_ps_set_colors,                  MRB_ARGS_KEY(1, 0) },
	{ "get_colors",                  w_ps_get_colors,                  MRB_ARGS_NONE() },
	{ "set_quads",                   w_ps_set_quads,                   MRB_ARGS_KEY(1, 0) },
	{ "get_quads",                   w_ps_get_quads,                   MRB_ARGS_NONE() },
	{ "set_relative_rotation",       w_ps_set_relative_rotation,       MRB_ARGS_KEY(1, 0) },
	{ "relative_rotation?",          w_ps_has_relative_rotation,       MRB_ARGS_NONE() },
	{ "get_count",                   w_ps_get_count,                   MRB_ARGS_NONE() },
	{ "start",                       w_ps_start,                       MRB_ARGS_NONE() },
	{ "stop",                        w_ps_stop,                        MRB_ARGS_NONE() },
	{ "pause",                       w_ps_pause,                       MRB_ARGS_NONE() },
	{ "reset",                       w_ps_reset,                       MRB_ARGS_NONE() },
	{ "emit",                        w_ps_emit,                        MRB_ARGS_KEY(1, 0) },
	{ "update",                      w_ps_update,                      MRB_ARGS_KEY(1, 0) },
	{ "active?",                     w_ps_is_active,                   MRB_ARGS_NONE() },
	{ "paused?",                     w_ps_is_paused,                   MRB_ARGS_NONE() },
	{ "stopped?",                    w_ps_is_stopped,                  MRB_ARGS_NONE() },
	{ "empty?",                      w_ps_is_empty,                    MRB_ARGS_NONE() },
	{ "full?",                       w_ps_is_full,                     MRB_ARGS_NONE() },
	{ "clone",                       w_ps_clone,                       MRB_ARGS_NONE() },
	{ nullptr, nullptr, 0 }
};

// new_particle_system(texture:, size:) -- size defaults to 1000 (max particles).
static mrb_value w_new_particle_system(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[2];
	mrbx_get_kwargs(mrb, {"texture", "size"}, 1, v);
	Texture *tex = mrbx_checktype<Texture>(mrb, v[0]);
	int size = mrbx_optint(mrb, v[1], 1000);
	if (size < 1 || size > (int) ParticleSystem::MAX_PARTICLES)
		mrb_raise(mrb, E_ARGUMENT_ERROR, "Invalid ParticleSystem size.");

	ParticleSystem *t = nullptr;
	if (mrbx_catchexcept(mrb, [&]() { t = instance()->newParticleSystem(tex, size); }))
		return mrb_nil_value();
	mrb_value res = mrbx_pushtype(mrb, t);
	t->release();
	return res;
}

// =========================================================================
// Love::Mesh  (a drawable vertex set). Both the standard format (a vertex is
// [x, y, u, v, r, g, b, a] — position vec2, texcoord vec2, color unorm8 vec4)
// and **custom vertex formats** are supported: vertex read/write is driven by
// the mesh's actual format via the shared Buffer data helpers (the standard
// format is just one such format). Also exposes per-attribute access, attribute
// enable/disable, attached attributes (binding a Buffer as a custom vertex
// attribute), an explicit index buffer, and the from-buffers constructor.
// =========================================================================

// Defined further down with the Buffer type; used here to drive format-aware
// vertex read/write (a Mesh vertex format is a Buffer DataMember list).
static int buf_ncomponents(const std::vector<Buffer::DataMember> &members);
static void buf_write_element(mrb_state *mrb, const std::vector<Buffer::DataMember> &members,
	const mrb_value *comps, int ncomponents, char *dst);
static void buf_writebufferdata(mrb_state *mrb, const mrb_value *vals, int navail, DataFormat format, char *data);
static void buf_readbufferdata(mrb_state *mrb, DataFormat format, const char *data, mrb_value out);
static Buffer::DataDeclaration buf_check_declaration(mrb_state *mrb, mrb_value h);

static PrimitiveType check_mesh_mode(mrb_state *mrb, mrb_value v, PrimitiveType def)
{
	if (mrb_undef_p(v))
		return def;
	std::string str = mrbx_checkstring(mrb, v);
	PrimitiveType mode;
	if (!getConstant(str.c_str(), mode))
		mrb_raisef(mrb, E_ARGUMENT_ERROR, "Invalid mesh draw mode: %s", str.c_str());
	return mode;
}

// Write a vertex array element (an Array of component values) into the mesh at
// `index` (0-based), driven by the mesh's vertex format. Short arrays fall back
// to each format's per-component defaults. One path for standard and custom
// formats alike (the standard format is pos vec2 + texcoord vec2 + color
// unorm8 vec4, so [x,y,u,v,r,g,b,a] still works).
static void mesh_write_vertex(mrb_state *mrb, Mesh *t, size_t index, mrb_value el)
{
	if (!mrb_array_p(el))
		mrb_raise(mrb, E_ARGUMENT_ERROR, "Each vertex must be an array of component values.");
	const std::vector<Buffer::DataMember> &fmt = t->getVertexFormat();
	int ncomponents = buf_ncomponents(fmt);
	std::vector<mrb_value> comps(ncomponents);
	for (int j = 0; j < ncomponents; j++)
		comps[j] = mrb_ary_ref(mrb, el, j);

	char *data = nullptr;
	size_t offset = 0;
	if (mrbx_catchexcept(mrb, [&]() { data = (char *) t->checkVertexDataOffset(index, &offset); }))
		return;
	buf_write_element(mrb, fmt, comps.data(), ncomponents, data);
	t->setVertexDataModified(offset, t->getVertexStride());
}

static mrb_value w_mesh_set_vertex(mrb_state *mrb, mrb_value self)
{
	Mesh *t = mrbx_checktype<Mesh>(mrb, self);
	mrb_value v[2];
	mrbx_get_kwargs(mrb, {"index", "vertex"}, 2, v);
	mesh_write_vertex(mrb, t, (size_t) mrbx_checkint(mrb, v[0]) - 1, v[1]);
	return mrb_nil_value();
}

// get_vertex(index:) -> a flat Array of the vertex's component values, in
// format order (e.g. [x,y,u,v,r,g,b,a] for the standard format).
static mrb_value w_mesh_get_vertex(mrb_state *mrb, mrb_value self)
{
	Mesh *t = mrbx_checktype<Mesh>(mrb, self);
	mrb_value v[1];
	mrbx_get_kwargs(mrb, {"index"}, 1, v);
	const char *data = nullptr;
	if (mrbx_catchexcept(mrb, [&]() { data = (const char *) t->checkVertexDataOffset((size_t) mrbx_checkint(mrb, v[0]) - 1, nullptr); }))
		return mrb_nil_value();
	const std::vector<Buffer::DataMember> &fmt = t->getVertexFormat();
	mrb_value arr = mrb_ary_new(mrb);
	for (const Buffer::DataMember &member : fmt)
		buf_readbufferdata(mrb, member.decl.format, data + member.offset, arr);
	return arr;
}

// set_vertices(vertices:) -- overwrite from vertex 1 (1-based). Each element is
// an Array of component values for the mesh's vertex format.
static mrb_value w_mesh_set_vertices(mrb_state *mrb, mrb_value self)
{
	Mesh *t = mrbx_checktype<Mesh>(mrb, self);
	mrb_value v[1];
	mrbx_get_kwargs(mrb, {"vertices"}, 1, v);
	if (!mrb_array_p(v[0]))
		mrb_raise(mrb, E_ARGUMENT_ERROR, "vertices: must be an Array of vertex arrays.");
	mrb_int n = RARRAY_LEN(v[0]);
	for (mrb_int i = 0; i < n; i++)
		mesh_write_vertex(mrb, t, (size_t) i, mrb_ary_ref(mrb, v[0], i));
	return mrb_nil_value();
}

// set_vertex_attribute(index:, attribute:, value:) -- write one attribute of one
// vertex. `attribute` is a 1-based attribute index into the vertex format;
// `value` is an Array of that attribute's component values.
static mrb_value w_mesh_set_vertex_attribute(mrb_state *mrb, mrb_value self)
{
	Mesh *t = mrbx_checktype<Mesh>(mrb, self);
	mrb_value v[3];
	mrbx_get_kwargs(mrb, {"index", "attribute", "value"}, 3, v);
	size_t vertindex = (size_t) mrbx_checkint(mrb, v[0]) - 1;
	int attribindex = mrbx_checkint(mrb, v[1]) - 1;
	const std::vector<Buffer::DataMember> &fmt = t->getVertexFormat();
	if (attribindex < 0 || attribindex >= (int) fmt.size())
		mrb_raisef(mrb, E_ARGUMENT_ERROR, "Invalid vertex attribute index: %d", attribindex + 1);
	const Buffer::DataMember &member = fmt[attribindex];
	if (!mrb_array_p(v[2]))
		mrb_raise(mrb, E_ARGUMENT_ERROR, "value: must be an array of component values.");
	int nc = member.info.components;
	std::vector<mrb_value> comps(nc);
	for (int j = 0; j < nc; j++)
		comps[j] = mrb_ary_ref(mrb, v[2], j);

	char *data = nullptr;
	size_t offset = 0;
	if (mrbx_catchexcept(mrb, [&]() { data = (char *) t->checkVertexDataOffset(vertindex, &offset); }))
		return mrb_nil_value();
	buf_writebufferdata(mrb, comps.data(), nc, member.decl.format, data + member.offset);
	t->setVertexDataModified(offset + member.offset, member.size);
	return mrb_nil_value();
}

// get_vertex_attribute(index:, attribute:) -> Array of the attribute's component
// values (`attribute` 1-based).
static mrb_value w_mesh_get_vertex_attribute(mrb_state *mrb, mrb_value self)
{
	Mesh *t = mrbx_checktype<Mesh>(mrb, self);
	mrb_value v[2];
	mrbx_get_kwargs(mrb, {"index", "attribute"}, 2, v);
	size_t vertindex = (size_t) mrbx_checkint(mrb, v[0]) - 1;
	int attribindex = mrbx_checkint(mrb, v[1]) - 1;
	const std::vector<Buffer::DataMember> &fmt = t->getVertexFormat();
	if (attribindex < 0 || attribindex >= (int) fmt.size())
		mrb_raisef(mrb, E_ARGUMENT_ERROR, "Invalid vertex attribute index: %d", attribindex + 1);
	const Buffer::DataMember &member = fmt[attribindex];
	const char *data = nullptr;
	if (mrbx_catchexcept(mrb, [&]() { data = (const char *) t->checkVertexDataOffset(vertindex, nullptr); }))
		return mrb_nil_value();
	mrb_value arr = mrb_ary_new(mrb);
	buf_readbufferdata(mrb, member.decl.format, data + member.offset, arr);
	return arr;
}

// get_vertex_format -> Array of member Hashes {name:, location:, format:,
// array_length:, offset:}. Mirrors Mesh:getVertexFormat.
static mrb_value w_mesh_get_vertex_format(mrb_state *mrb, mrb_value self)
{
	Mesh *t = mrbx_checktype<Mesh>(mrb, self);
	const std::vector<Buffer::DataMember> &fmt = t->getVertexFormat();
	mrb_value arr = mrb_ary_new_capa(mrb, (mrb_int) fmt.size());
	for (const Buffer::DataMember &member : fmt)
	{
		mrb_value h = mrb_hash_new(mrb);
		hset(mrb, h, "name", mrb_str_new_cstr(mrb, member.decl.name.c_str()));
		hset(mrb, h, "location", mrbx_integer(mrb, member.decl.bindingLocation));
		const char *formatstr = "unknown";
		getConstant(member.decl.format, formatstr);
		hset(mrb, h, "format", mrb_str_new_cstr(mrb, formatstr));
		hset(mrb, h, "array_length", mrbx_integer(mrb, member.decl.arrayLength));
		hset(mrb, h, "offset", mrbx_integer(mrb, (int) member.offset));
		mrb_ary_push(mrb, arr, h);
	}
	return arr;
}

static mrb_value w_mesh_get_vertex_count(mrb_state *mrb, mrb_value self)
{
	return mrbx_integer(mrb, (int) mrbx_checktype<Mesh>(mrb, self)->getVertexCount());
}

static mrb_value w_mesh_set_texture(mrb_state *mrb, mrb_value self)
{
	Mesh *t = mrbx_checktype<Mesh>(mrb, self);
	mrb_value v[1];
	mrbx_get_kwargs(mrb, {"texture"}, 0, v);
	if (mrb_undef_p(v[0]) || mrb_nil_p(v[0]))
		t->setTexture();
	else
		t->setTexture(mrbx_checktype<Texture>(mrb, v[0]));
	return mrb_nil_value();
}

static mrb_value w_mesh_get_texture(mrb_state *mrb, mrb_value self)
{
	Texture *tex = mrbx_checktype<Mesh>(mrb, self)->getTexture();
	return tex ? mrbx_pushtype(mrb, tex) : mrb_nil_value();
}

static mrb_value w_mesh_set_draw_mode(mrb_state *mrb, mrb_value self)
{
	Mesh *t = mrbx_checktype<Mesh>(mrb, self);
	mrb_value v[1];
	mrbx_get_kwargs(mrb, {"mode"}, 1, v);
	t->setDrawMode(check_mesh_mode(mrb, v[0], PRIMITIVE_TRIANGLE_FAN));
	return mrb_nil_value();
}

static mrb_value w_mesh_get_draw_mode(mrb_state *mrb, mrb_value self)
{
	const char *str = nullptr;
	getConstant(mrbx_checktype<Mesh>(mrb, self)->getDrawMode(), str);
	return mrbx_string(mrb, str ? str : "");
}

static mrb_value w_mesh_set_draw_range(mrb_state *mrb, mrb_value self)
{
	Mesh *t = mrbx_checktype<Mesh>(mrb, self);
	mrb_value v[2];
	mrbx_get_kwargs(mrb, {"start", "count"}, 0, v);
	if (mrb_undef_p(v[0]) && mrb_undef_p(v[1]))
		t->setDrawRange();
	else
	{
		int start = mrbx_checkint(mrb, v[0]) - 1;
		int count = mrbx_checkint(mrb, v[1]);
		mrbx_catchexcept(mrb, [&]() { t->setDrawRange(start, count); });
	}
	return mrb_nil_value();
}

static mrb_value w_mesh_get_draw_range(mrb_state *mrb, mrb_value self)
{
	Mesh *t = mrbx_checktype<Mesh>(mrb, self);
	int start = 0, count = 0;
	if (!t->getDrawRange(start, count))
		return mrb_nil_value();
	mrb_value out = mrb_hash_new(mrb);
	hset(mrb, out, "start", mrbx_integer(mrb, start + 1));
	hset(mrb, out, "count", mrbx_integer(mrb, count));
	return out;
}

// set_vertex_map(map:, index_type:, count:) -- `map:` is an Array of 1-based
// vertex indices, or a Data of raw index bytes (then `index_type:` "uint16"/
// "uint32" is required, `count:` optional); omitted/nil clears the index map.
static mrb_value w_mesh_set_vertex_map(mrb_state *mrb, mrb_value self)
{
	Mesh *t = mrbx_checktype<Mesh>(mrb, self);
	mrb_value v[3];
	mrbx_get_kwargs(mrb, {"map", "index_type", "count"}, 0, v);
	if (mrb_undef_p(v[0]) || mrb_nil_p(v[0]))
	{
		mrbx_catchexcept(mrb, [&]() { t->setVertexMap(); });
		return mrb_nil_value();
	}

	// Raw index data from a Data object.
	if (mrbx_istype<love::Data>(mrb, v[0]))
	{
		love::Data *d = mrbx_checktype<love::Data>(mrb, v[0]);
		if (mrb_undef_p(v[1]) || mrb_nil_p(v[1]))
			mrb_raise(mrb, E_ARGUMENT_ERROR, "index_type: (\"uint16\"/\"uint32\") is required when map: is a Data.");
		std::string str = mrbx_checkstring(mrb, v[1]);
		IndexDataType indextype;
		if (!getConstant(str.c_str(), indextype))
			mrb_raisef(mrb, E_ARGUMENT_ERROR, "Invalid index data type: %s", str.c_str());
		size_t typesize = getIndexDataSize(indextype);
		int indexcount = mrbx_optint(mrb, v[2], (int) (d->getSize() / typesize));
		if (indexcount < 1 || (size_t) indexcount * typesize > d->getSize())
			mrb_raisef(mrb, E_ARGUMENT_ERROR, "Invalid index count: %d", indexcount);
		mrbx_catchexcept(mrb, [&]() { t->setVertexMap(indextype, d->getData(), indexcount * typesize); });
		return mrb_nil_value();
	}

	if (!mrb_array_p(v[0]))
		mrb_raise(mrb, E_ARGUMENT_ERROR, "map: must be an Array of 1-based indices or a Data.");
	std::vector<uint32> map;
	mrb_int n = RARRAY_LEN(v[0]);
	for (mrb_int i = 0; i < n; i++)
		map.push_back((uint32) (mrbx_checkint(mrb, mrb_ary_ref(mrb, v[0], i)) - 1));
	mrbx_catchexcept(mrb, [&]() { t->setVertexMap(map); });
	return mrb_nil_value();
}

static mrb_value w_mesh_get_vertex_map(mrb_state *mrb, mrb_value self)
{
	Mesh *t = mrbx_checktype<Mesh>(mrb, self);
	std::vector<uint32> map;
	if (!t->getVertexMap(map))
		return mrb_nil_value();
	mrb_value arr = mrb_ary_new_capa(mrb, (mrb_int) map.size());
	for (uint32 idx : map)
		mrb_ary_push(mrb, arr, mrbx_integer(mrb, (int) idx + 1));
	return arr;
}

// set_attribute_enabled(name:/location:, enable:) -- toggle whether an attribute
// participates in drawing. Identify the attribute by `name:` (String) or
// `location:` (Integer binding location).
static mrb_value w_mesh_set_attribute_enabled(mrb_state *mrb, mrb_value self)
{
	Mesh *t = mrbx_checktype<Mesh>(mrb, self);
	mrb_value v[3];
	mrbx_get_kwargs(mrb, {"name", "location", "enable"}, 0, v);
	if (mrb_undef_p(v[2]) || mrb_nil_p(v[2]))
		mrb_raise(mrb, E_ARGUMENT_ERROR, "enable: is required.");
	bool enable = mrb_test(v[2]);
	if (!mrb_undef_p(v[0]) && !mrb_nil_p(v[0]))
	{
		std::string name = mrbx_checkstring(mrb, v[0]);
		mrbx_catchexcept(mrb, [&]() { t->setAttributeEnabled(name, enable); });
	}
	else if (!mrb_undef_p(v[1]) && !mrb_nil_p(v[1]))
	{
		int location = mrbx_checkint(mrb, v[1]);
		mrbx_catchexcept(mrb, [&]() { t->setAttributeEnabled(location, enable); });
	}
	else
		mrb_raise(mrb, E_ARGUMENT_ERROR, "set_attribute_enabled needs name: or location:.");
	return mrb_nil_value();
}

// attribute_enabled?(name:/location:) -> bool.
static mrb_value w_mesh_is_attribute_enabled(mrb_state *mrb, mrb_value self)
{
	Mesh *t = mrbx_checktype<Mesh>(mrb, self);
	mrb_value v[2];
	mrbx_get_kwargs(mrb, {"name", "location"}, 0, v);
	bool enabled = false;
	if (!mrb_undef_p(v[0]) && !mrb_nil_p(v[0]))
	{
		std::string name = mrbx_checkstring(mrb, v[0]);
		mrbx_catchexcept(mrb, [&]() { enabled = t->isAttributeEnabled(name); });
	}
	else if (!mrb_undef_p(v[1]) && !mrb_nil_p(v[1]))
	{
		int location = mrbx_checkint(mrb, v[1]);
		mrbx_catchexcept(mrb, [&]() { enabled = t->isAttributeEnabled(location); });
	}
	else
		mrb_raise(mrb, E_ARGUMENT_ERROR, "attribute_enabled? needs name: or location:.");
	return mrbx_boolean(mrb, enabled);
}

static AttributeStep check_attribute_step(mrb_state *mrb, mrb_value v, AttributeStep def)
{
	if (mrb_undef_p(v) || mrb_nil_p(v))
		return def;
	std::string str = mrbx_checkstring(mrb, v);
	AttributeStep step;
	if (!getConstant(str.c_str(), step))
		mrb_raisef(mrb, E_ARGUMENT_ERROR, "Invalid vertex attribute step: %s", str.c_str());
	return step;
}

// attach_attribute(name:/location:, buffer:/mesh:, step:, attach_name:/
// attach_location:, start_index:) -- bind a vertex attribute sourced from
// another Buffer (or another Mesh's vertex buffer). Identify the local attribute
// by name: or location:; the source attribute defaults to the same name/location
// unless attach_name:/attach_location: override it. step: is "pervertex"
// (default) or "perinstance"; start_index: is 1-based (default 1).
static mrb_value w_mesh_attach_attribute(mrb_state *mrb, mrb_value self)
{
	Mesh *t = mrbx_checktype<Mesh>(mrb, self);
	mrb_value v[8];
	mrbx_get_kwargs(mrb, {"name", "location", "buffer", "mesh", "step", "attach_name", "attach_location", "start_index"}, 0, v);

	bool byname = !mrb_undef_p(v[0]) && !mrb_nil_p(v[0]);
	bool byloc = !mrb_undef_p(v[1]) && !mrb_nil_p(v[1]);
	if (!byname && !byloc)
		mrb_raise(mrb, E_ARGUMENT_ERROR, "attach_attribute needs name: or location:.");

	Buffer *buffer = nullptr;
	if (!mrb_undef_p(v[2]) && !mrb_nil_p(v[2]))
		buffer = mrbx_checktype<Buffer>(mrb, v[2]);
	else if (!mrb_undef_p(v[3]) && !mrb_nil_p(v[3]))
	{
		Mesh *src = mrbx_checktype<Mesh>(mrb, v[3]);
		buffer = src->getVertexBuffer();
		if (buffer == nullptr)
			mrb_raise(mrb, E_ARGUMENT_ERROR, "Mesh does not have its own vertex buffer.");
	}
	else
		mrb_raise(mrb, E_ARGUMENT_ERROR, "attach_attribute needs buffer: or mesh:.");

	AttributeStep step = check_attribute_step(mrb, v[4], STEP_PER_VERTEX);
	int startindex = mrbx_optint(mrb, v[7], 1) - 1;

	if (byname)
	{
		std::string name = mrbx_checkstring(mrb, v[0]);
		std::string attachname = (!mrb_undef_p(v[5]) && !mrb_nil_p(v[5])) ? mrbx_checkstring(mrb, v[5]) : name;
		mrbx_catchexcept(mrb, [&]() { t->attachAttribute(name, buffer, nullptr, attachname, startindex, step); });
	}
	else
	{
		int location = mrbx_checkint(mrb, v[1]);
		int attachloc = mrbx_optint(mrb, v[6], location);
		mrbx_catchexcept(mrb, [&]() { t->attachAttribute(location, buffer, nullptr, attachloc, startindex, step); });
	}
	return mrb_nil_value();
}

// detach_attribute(name:) -> true if an attribute was detached.
static mrb_value w_mesh_detach_attribute(mrb_state *mrb, mrb_value self)
{
	Mesh *t = mrbx_checktype<Mesh>(mrb, self);
	mrb_value v[1];
	mrbx_get_kwargs(mrb, {"name"}, 1, v);
	std::string name = mrbx_checkstring(mrb, v[0]);
	bool success = false;
	mrbx_catchexcept(mrb, [&]() { success = t->detachAttribute(name); });
	return mrbx_boolean(mrb, success);
}

// get_attached_attributes -> Array of Hashes {name:, location:, buffer:, step:,
// name_in_buffer:, location_in_buffer:, start_index:}.
static mrb_value w_mesh_get_attached_attributes(mrb_state *mrb, mrb_value self)
{
	Mesh *t = mrbx_checktype<Mesh>(mrb, self);
	const std::vector<Mesh::BufferAttribute> &attributes = t->getAttachedAttributes();
	mrb_value arr = mrb_ary_new_capa(mrb, (mrb_int) attributes.size());
	for (const Mesh::BufferAttribute &attrib : attributes)
	{
		mrb_value h = mrb_hash_new(mrb);
		hset(mrb, h, "name", mrb_str_new_cstr(mrb, attrib.name.c_str()));
		hset(mrb, h, "location", mrbx_integer(mrb, attrib.bindingLocation));
		hset(mrb, h, "buffer", mrbx_pushtype(mrb, attrib.buffer.get()));
		const char *stepstr = nullptr;
		getConstant(attrib.step, stepstr);
		hset(mrb, h, "step", mrb_str_new_cstr(mrb, stepstr ? stepstr : ""));
		const Buffer::DataMember &member = attrib.buffer->getDataMember(attrib.indexInBuffer);
		hset(mrb, h, "name_in_buffer", mrb_str_new_cstr(mrb, member.decl.name.c_str()));
		hset(mrb, h, "location_in_buffer", mrbx_integer(mrb, member.decl.bindingLocation));
		hset(mrb, h, "start_index", mrbx_integer(mrb, attrib.startArrayIndex + 1));
		mrb_ary_push(mrb, arr, h);
	}
	return arr;
}

static mrb_value w_mesh_get_vertex_buffer(mrb_state *mrb, mrb_value self)
{
	Buffer *b = mrbx_checktype<Mesh>(mrb, self)->getVertexBuffer();
	return b ? mrbx_pushtype(mrb, b) : mrb_nil_value();
}

// set_index_buffer(buffer:) -- use a Buffer as the explicit index buffer
// (nil/omitted clears it).
static mrb_value w_mesh_set_index_buffer(mrb_state *mrb, mrb_value self)
{
	Mesh *t = mrbx_checktype<Mesh>(mrb, self);
	mrb_value v[1];
	mrbx_get_kwargs(mrb, {"buffer"}, 0, v);
	Buffer *b = nullptr;
	if (!mrb_undef_p(v[0]) && !mrb_nil_p(v[0]))
		b = mrbx_checktype<Buffer>(mrb, v[0]);
	mrbx_catchexcept(mrb, [&]() { t->setIndexBuffer(b); });
	return mrb_nil_value();
}

static mrb_value w_mesh_get_index_buffer(mrb_state *mrb, mrb_value self)
{
	Buffer *b = mrbx_checktype<Mesh>(mrb, self)->getIndexBuffer();
	return b ? mrbx_pushtype(mrb, b) : mrb_nil_value();
}

static mrb_value w_mesh_flush(mrb_state *mrb, mrb_value self)
{
	mrbx_checktype<Mesh>(mrb, self)->flush();
	return mrb_nil_value();
}

static const MrbReg meshFunctions[] =
{
	{ "set_vertex",            w_mesh_set_vertex,            MRB_ARGS_KEY(2, 0) },
	{ "get_vertex",            w_mesh_get_vertex,            MRB_ARGS_KEY(1, 0) },
	{ "set_vertices",          w_mesh_set_vertices,          MRB_ARGS_KEY(1, 0) },
	{ "set_vertex_attribute",  w_mesh_set_vertex_attribute,  MRB_ARGS_KEY(3, 0) },
	{ "get_vertex_attribute",  w_mesh_get_vertex_attribute,  MRB_ARGS_KEY(2, 0) },
	{ "get_vertex_count",      w_mesh_get_vertex_count,      MRB_ARGS_NONE() },
	{ "get_vertex_format",     w_mesh_get_vertex_format,     MRB_ARGS_NONE() },
	{ "set_attribute_enabled", w_mesh_set_attribute_enabled, MRB_ARGS_KEY(3, 0) },
	{ "attribute_enabled?",    w_mesh_is_attribute_enabled,  MRB_ARGS_KEY(2, 0) },
	{ "attach_attribute",      w_mesh_attach_attribute,      MRB_ARGS_KEY(8, 0) },
	{ "detach_attribute",      w_mesh_detach_attribute,      MRB_ARGS_KEY(1, 0) },
	{ "get_attached_attributes", w_mesh_get_attached_attributes, MRB_ARGS_NONE() },
	{ "get_vertex_buffer",     w_mesh_get_vertex_buffer,     MRB_ARGS_NONE() },
	{ "set_texture",           w_mesh_set_texture,           MRB_ARGS_KEY(1, 0) },
	{ "get_texture",           w_mesh_get_texture,           MRB_ARGS_NONE() },
	{ "set_draw_mode",         w_mesh_set_draw_mode,         MRB_ARGS_KEY(1, 0) },
	{ "get_draw_mode",         w_mesh_get_draw_mode,         MRB_ARGS_NONE() },
	{ "set_draw_range",        w_mesh_set_draw_range,        MRB_ARGS_KEY(2, 0) },
	{ "get_draw_range",        w_mesh_get_draw_range,        MRB_ARGS_NONE() },
	{ "set_vertex_map",        w_mesh_set_vertex_map,        MRB_ARGS_KEY(3, 0) },
	{ "get_vertex_map",        w_mesh_get_vertex_map,        MRB_ARGS_NONE() },
	{ "set_index_buffer",      w_mesh_set_index_buffer,      MRB_ARGS_KEY(1, 0) },
	{ "get_index_buffer",      w_mesh_get_index_buffer,      MRB_ARGS_NONE() },
	{ "flush",                 w_mesh_flush,                 MRB_ARGS_NONE() },
	{ nullptr, nullptr, 0 }
};

// Parse one Mesh::BufferAttribute from a Ruby Hash {buffer:, location:, name:,
// step:, location_in_buffer:, name_in_buffer:, start_index:}. Mirrors
// luax_checkbufferattributetable.
static Mesh::BufferAttribute mesh_check_buffer_attribute(mrb_state *mrb, mrb_value h)
{
	if (!mrb_hash_p(h))
		mrb_raise(mrb, E_ARGUMENT_ERROR, "Each Mesh buffer attribute must be a Hash.");
	auto field = [&](const char *k) {
		return mrb_hash_get(mrb, h, mrb_symbol_value(mrb_intern_cstr(mrb, k)));
	};

	Mesh::BufferAttribute attrib;
	attrib.step = STEP_PER_VERTEX;
	attrib.enabled = true;

	mrb_value buf = field("buffer");
	if (mrb_nil_p(buf))
		mrb_raise(mrb, E_ARGUMENT_ERROR, "Mesh buffer attribute needs a buffer:.");
	attrib.buffer = mrbx_checktype<Buffer>(mrb, buf);

	mrb_value loc = field("location");
	if (mrb_nil_p(loc))
		mrb_raise(mrb, E_ARGUMENT_ERROR, "Mesh buffer attribute needs a location:.");
	attrib.bindingLocation = mrbx_checkint(mrb, loc);

	mrb_value name = field("name");
	if (!mrb_nil_p(name))
		attrib.name = mrbx_checkstring(mrb, name);

	attrib.step = check_attribute_step(mrb, field("step"), STEP_PER_VERTEX);

	mrb_value locinbuf = field("location_in_buffer");
	attrib.bindingLocationInBuffer = mrb_nil_p(locinbuf) ? attrib.bindingLocation : mrbx_checkint(mrb, locinbuf);

	mrb_value nameinbuf = field("name_in_buffer");
	attrib.nameInBuffer = mrb_nil_p(nameinbuf) ? attrib.name : mrbx_checkstring(mrb, nameinbuf);

	mrb_value startidx = field("start_index");
	attrib.startArrayIndex = (mrb_nil_p(startidx) ? 1 : mrbx_checkint(mrb, startidx)) - 1;

	return attrib;
}

// new_mesh(vertices:, count:, format:, data:, buffers:, mode:, usage:).
// Three constructors, mirroring love.graphics.newMesh:
//  - buffers: an Array of attribute Hashes -> a Mesh sourced from existing GPU
//    Buffers (see mesh_check_buffer_attribute); mode: only.
//  - otherwise a vertex format: format: an Array of declaration Hashes for a
//    custom format, else the default [position, texcoord, color] standard
//    format. Supply vertices: (an Array of per-vertex component arrays), data:
//    (a Data of packed vertex bytes), or count: (an empty mesh of N vertices).
// mode: defaults to "fan", usage: to "dynamic".
static mrb_value w_new_mesh(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[7];
	mrbx_get_kwargs(mrb, {"vertices", "count", "format", "data", "buffers", "mode", "usage"}, 0, v);

	PrimitiveType drawmode = check_mesh_mode(mrb, v[5], PRIMITIVE_TRIANGLE_FAN);
	BufferDataUsage usage = BUFFERDATAUSAGE_DYNAMIC;
	if (!mrb_undef_p(v[6]) && !mrb_nil_p(v[6]))
	{
		std::string str = mrbx_checkstring(mrb, v[6]);
		if (!getConstant(str.c_str(), usage))
			mrb_raisef(mrb, E_ARGUMENT_ERROR, "Invalid usage hint: %s", str.c_str());
	}

	Mesh *t = nullptr;

	// From-buffers constructor.
	if (!mrb_undef_p(v[4]) && !mrb_nil_p(v[4]))
	{
		if (!mrb_array_p(v[4]))
			mrb_raise(mrb, E_ARGUMENT_ERROR, "buffers: must be an Array of attribute Hashes.");
		std::vector<Mesh::BufferAttribute> attributes;
		mrb_int n = RARRAY_LEN(v[4]);
		for (mrb_int i = 0; i < n; i++)
			attributes.push_back(mesh_check_buffer_attribute(mrb, mrb_ary_ref(mrb, v[4], i)));
		if (mrbx_catchexcept(mrb, [&]() { t = instance()->newMesh(attributes, drawmode); }))
			return mrb_nil_value();
		mrb_value resb = mrbx_pushtype(mrb, t);
		t->release();
		return resb;
	}

	// Vertex format: custom (format:) or the default standard format.
	std::vector<Buffer::DataDeclaration> format;
	if (!mrb_undef_p(v[2]) && !mrb_nil_p(v[2]))
	{
		if (!mrb_array_p(v[2]))
			mrb_raise(mrb, E_ARGUMENT_ERROR, "format: must be an Array of declaration Hashes.");
		mrb_int n = RARRAY_LEN(v[2]);
		for (mrb_int i = 0; i < n; i++)
			format.push_back(buf_check_declaration(mrb, mrb_ary_ref(mrb, v[2], i)));
	}
	else
		format = Mesh::getDefaultVertexFormat();

	if (!mrb_undef_p(v[3]) && mrbx_istype<love::Data>(mrb, v[3]))
	{
		// Packed vertex bytes straight from a Data object.
		love::Data *d = mrbx_checktype<love::Data>(mrb, v[3]);
		if (mrbx_catchexcept(mrb, [&]() { t = instance()->newMesh(format, d->getData(), d->getSize(), drawmode, usage); }))
			return mrb_nil_value();
	}
	else if (!mrb_undef_p(v[0]) && mrb_array_p(v[0]))
	{
		// Empty mesh of N vertices, then fill format-aware from the Array.
		mrb_int n = RARRAY_LEN(v[0]);
		if (mrbx_catchexcept(mrb, [&]() { t = instance()->newMesh(format, (int) n, drawmode, usage); }))
			return mrb_nil_value();
		for (mrb_int i = 0; i < n; i++)
			mesh_write_vertex(mrb, t, (size_t) i, mrb_ary_ref(mrb, v[0], i));
		t->flush();
	}
	else if (!mrb_undef_p(v[1]) && !mrb_nil_p(v[1]))
	{
		int count = mrbx_checkint(mrb, v[1]);
		if (mrbx_catchexcept(mrb, [&]() { t = instance()->newMesh(format, count, drawmode, usage); }))
			return mrb_nil_value();
	}
	else
		mrb_raise(mrb, E_ARGUMENT_ERROR, "new_mesh needs vertices:, data:, count:, or buffers:.");

	mrb_value res = mrbx_pushtype(mrb, t);
	t->release();
	return res;
}

// =========================================================================
// Love::Video  (a theora video as a Drawable). Playback control delegates to
// the underlying VideoStream. Drawing the video advances its frames (Video::
// draw calls update internally). The audio track is not wired here -- a video
// plays timer-driven via the stream's default DeltaSync.
// =========================================================================

static mrb_value w_video_get_stream(mrb_state *mrb, mrb_value self)
{
	return mrbx_pushtype(mrb, mrbx_checktype<Video>(mrb, self)->getStream());
}

static mrb_value w_video_get_source(mrb_state *mrb, mrb_value self)
{
	love::audio::Source *src = mrbx_checktype<Video>(mrb, self)->getSource();
	return src ? mrbx_pushtype(mrb, src) : mrb_nil_value();
}

static mrb_value w_video_play(mrb_state *mrb, mrb_value self)
{
	mrbx_checktype<Video>(mrb, self)->getStream()->play();
	return mrb_nil_value();
}

static mrb_value w_video_pause(mrb_state *mrb, mrb_value self)
{
	mrbx_checktype<Video>(mrb, self)->getStream()->pause();
	return mrb_nil_value();
}

static mrb_value w_video_seek(mrb_state *mrb, mrb_value self)
{
	mrb_value v[1];
	mrbx_get_kwargs(mrb, {"offset"}, 1, v);
	mrbx_checktype<Video>(mrb, self)->getStream()->seek(mrbx_checknumber(mrb, v[0]));
	return mrb_nil_value();
}

static mrb_value w_video_rewind(mrb_state *mrb, mrb_value self)
{
	mrbx_checktype<Video>(mrb, self)->getStream()->seek(0.0);
	return mrb_nil_value();
}

static mrb_value w_video_tell(mrb_state *mrb, mrb_value self)
{
	return mrbx_number(mrb, mrbx_checktype<Video>(mrb, self)->getStream()->tell());
}

static mrb_value w_video_is_playing(mrb_state *mrb, mrb_value self)
{
	return mrbx_boolean(mrb, mrbx_checktype<Video>(mrb, self)->getStream()->isPlaying());
}

static mrb_value w_video_get_width(mrb_state *mrb, mrb_value self)
{
	return mrbx_integer(mrb, mrbx_checktype<Video>(mrb, self)->getWidth());
}

static mrb_value w_video_get_height(mrb_state *mrb, mrb_value self)
{
	return mrbx_integer(mrb, mrbx_checktype<Video>(mrb, self)->getHeight());
}

static mrb_value w_video_get_dimensions(mrb_state *mrb, mrb_value self)
{
	Video *vid = mrbx_checktype<Video>(mrb, self);
	mrb_value arr = mrb_ary_new_capa(mrb, 2);
	mrb_ary_push(mrb, arr, mrbx_integer(mrb, vid->getWidth()));
	mrb_ary_push(mrb, arr, mrbx_integer(mrb, vid->getHeight()));
	return arr;
}

static mrb_value w_video_set_filter(mrb_state *mrb, mrb_value self)
{
	Video *vid = mrbx_checktype<Video>(mrb, self);
	mrb_value v[2];
	mrbx_get_kwargs(mrb, {"min", "mag"}, 1, v);
	SamplerState s = vid->getSamplerState();
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
	mrbx_catchexcept(mrb, [&]() { vid->setSamplerState(s); });
	return mrb_nil_value();
}

static mrb_value w_video_get_filter(mrb_state *mrb, mrb_value self)
{
	const SamplerState &s = mrbx_checktype<Video>(mrb, self)->getSamplerState();
	const char *mins = nullptr, *mags = nullptr;
	SamplerState::getConstant(s.minFilter, mins);
	SamplerState::getConstant(s.magFilter, mags);
	mrb_value out = mrb_hash_new(mrb);
	hset(mrb, out, "min", mrbx_string(mrb, mins ? mins : ""));
	hset(mrb, out, "mag", mrbx_string(mrb, mags ? mags : ""));
	return out;
}

static const MrbReg videoFunctions[] =
{
	{ "get_stream",     w_video_get_stream,     MRB_ARGS_NONE() },
	{ "get_source",     w_video_get_source,     MRB_ARGS_NONE() },
	{ "play",           w_video_play,           MRB_ARGS_NONE() },
	{ "pause",          w_video_pause,          MRB_ARGS_NONE() },
	{ "seek",           w_video_seek,           MRB_ARGS_KEY(1, 0) },
	{ "rewind",         w_video_rewind,         MRB_ARGS_NONE() },
	{ "tell",           w_video_tell,           MRB_ARGS_NONE() },
	{ "playing?",       w_video_is_playing,     MRB_ARGS_NONE() },
	{ "get_width",      w_video_get_width,      MRB_ARGS_NONE() },
	{ "get_height",     w_video_get_height,     MRB_ARGS_NONE() },
	{ "get_dimensions", w_video_get_dimensions, MRB_ARGS_NONE() },
	{ "set_filter",     w_video_set_filter,     MRB_ARGS_KEY(2, 0) },
	{ "get_filter",     w_video_get_filter,     MRB_ARGS_NONE() },
	{ nullptr, nullptr, 0 }
};

// new_video(file:, dpi_scale:) -- file: an .ogv filename (theora). Returns a
// Love::Video. The audio track is not wired up; the video plays timer-driven.
static mrb_value w_new_video(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[2];
	mrbx_get_kwargs(mrb, {"file", "dpi_scale"}, 1, v);
	std::string filename = mrbx_checkstring(mrb, v[0]);
	float dpiscale = mrbx_optfloat(mrb, v[1], 1.0f);

	auto videomod = Module::getInstance<love::video::Video>(Module::M_VIDEO);
	if (videomod == nullptr)
		mrb_raise(mrb, E_RUNTIME_ERROR, "The love.video module is not available.");
	auto fs = Module::getInstance<filesystem::Filesystem>(Module::M_FILESYSTEM);
	if (fs == nullptr)
		mrb_raise(mrb, E_RUNTIME_ERROR, "Cannot load a video without the love.filesystem module.");

	love::filesystem::File *file = nullptr;
	if (mrbx_catchexcept(mrb, [&]() { file = fs->openFile(filename.c_str(), love::filesystem::File::MODE_READ); }))
		return mrb_nil_value();

	love::video::VideoStream *stream = nullptr;
	bool err = mrbx_catchexcept(mrb, [&]() { stream = videomod->newVideoStream(file); });
	file->release();
	if (err)
		return mrb_nil_value();

	Video *video = nullptr;
	bool err2 = mrbx_catchexcept(mrb, [&]() { video = instance()->newVideo(stream, dpiscale); });
	stream->release();
	if (err2)
		return mrb_nil_value();

	// Best-effort audio: if the video carries an audio track and the sound +
	// audio modules are present, build a streaming Source from the same file,
	// attach it, and sync the video frames to it (a SourceSync) -- mirroring the
	// Lua newVideo wrapper. Any failure (no audio track, missing modules) is
	// non-fatal: the video keeps its default timer-driven DeltaSync and plays
	// silently.
	auto snd = Module::getInstance<love::sound::Sound>(Module::M_SOUND);
	auto audiomod = Module::getInstance<love::audio::Audio>(Module::M_AUDIO);
	if (snd != nullptr && audiomod != nullptr)
	{
		love::audio::Source *source = nullptr;
		try
		{
			love::filesystem::File *afile = fs->openFile(filename.c_str(), love::filesystem::File::MODE_READ);
			love::sound::Decoder *dec = snd->newDecoder(afile, love::sound::Decoder::DEFAULT_BUFFER_SIZE);
			afile->release();
			source = audiomod->newSource(dec);
			dec->release();
		}
		catch (love::Exception &)
		{
			source = nullptr;
		}

		if (source != nullptr)
		{
			video->setSource(source);
			auto sync = new love::video::VideoStream::SourceSync(source);
			video->getStream()->setSync(sync);
			sync->release();
			source->release();
		}
	}

	mrb_value res = mrbx_pushtype(mrb, video);
	video->release();
	return res;
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

// =========================================================================
// Love::Buffer  (a block of GPU-owned memory). Faithful to wrap_Buffer.cpp +
// the newBuffer half of wrap_Graphics.cpp, under the keyword-argument
// convention. A buffer is created from a vertex-style format declaration plus
// either initial element data (a Data or an Array) or an element count.
// =========================================================================

static const double bufDefaultComponents[] = {0.0, 0.0, 0.0, 1.0};

// One component value out of the gathered per-element array, or a default when
// it is missing/nil (mirrors luaL_optnumber in wrap_Buffer.cpp).
static double buf_comp(mrb_state *mrb, const mrb_value *vals, int navail, int i, double def)
{
	if (i >= navail)
		return def;
	mrb_value c = vals[i];
	if (mrb_undef_p(c) || mrb_nil_p(c))
		return def;
	return (double) mrbx_checkfloat(mrb, c);
}

// A matrix component is required (no default), like luaL_checknumber.
static double buf_reqcomp(mrb_state *mrb, const mrb_value *vals, int navail, int i)
{
	if (i >= navail || mrb_undef_p(vals[i]) || mrb_nil_p(vals[i]))
		mrb_raise(mrb, E_ARGUMENT_ERROR, "Not enough components supplied for a matrix buffer format.");
	return (double) mrbx_checkfloat(mrb, vals[i]);
}

template <typename T>
static void buf_writeData(mrb_state *mrb, const mrb_value *vals, int navail, int components, char *data)
{
	auto cd = (T *) data;
	for (int i = 0; i < components; i++)
		cd[i] = (T) buf_comp(mrb, vals, navail, i, bufDefaultComponents[i]);
}

template <typename T>
static void buf_writeRequired(mrb_state *mrb, const mrb_value *vals, int navail, int components, char *data)
{
	auto cd = (T *) data;
	for (int i = 0; i < components; i++)
		cd[i] = (T) buf_reqcomp(mrb, vals, navail, i);
}

template <typename T>
static void buf_writeSNorm(mrb_state *mrb, const mrb_value *vals, int navail, int components, char *data)
{
	auto cd = (T *) data;
	constexpr auto maxval = std::numeric_limits<T>::max();
	for (int i = 0; i < components; i++)
	{
		double d = buf_comp(mrb, vals, navail, i, bufDefaultComponents[i]);
		d = d < -1.0 ? -1.0 : (d > 1.0 ? 1.0 : d);
		cd[i] = (T) (d * maxval);
	}
}

template <typename T>
static void buf_writeUNorm(mrb_state *mrb, const mrb_value *vals, int navail, int components, char *data)
{
	auto cd = (T *) data;
	constexpr auto maxval = std::numeric_limits<T>::max();
	for (int i = 0; i < components; i++)
	{
		double d = buf_comp(mrb, vals, navail, i, 1.0);
		d = d < 0.0 ? 0.0 : (d > 1.0 ? 1.0 : d);
		cd[i] = (T) (d * maxval);
	}
}

// mruby analog of luax_writebufferdata: write one member's components (read from
// the gathered value array `vals`, of which `navail` are usable) into `data`.
static void buf_writebufferdata(mrb_state *mrb, const mrb_value *vals, int navail, DataFormat format, char *data)
{
	switch (format)
	{
		case DATAFORMAT_FLOAT:      buf_writeData<float>(mrb, vals, navail, 1, data); break;
		case DATAFORMAT_FLOAT_VEC2: buf_writeData<float>(mrb, vals, navail, 2, data); break;
		case DATAFORMAT_FLOAT_VEC3: buf_writeData<float>(mrb, vals, navail, 3, data); break;
		case DATAFORMAT_FLOAT_VEC4: buf_writeData<float>(mrb, vals, navail, 4, data); break;

		case DATAFORMAT_FLOAT_MAT2X2: buf_writeRequired<float>(mrb, vals, navail, 4, data); break;
		case DATAFORMAT_FLOAT_MAT2X3: buf_writeRequired<float>(mrb, vals, navail, 6, data); break;
		case DATAFORMAT_FLOAT_MAT2X4: buf_writeRequired<float>(mrb, vals, navail, 8, data); break;

		case DATAFORMAT_FLOAT_MAT3X2: buf_writeRequired<float>(mrb, vals, navail, 6, data); break;
		case DATAFORMAT_FLOAT_MAT3X3: buf_writeRequired<float>(mrb, vals, navail, 9, data); break;
		case DATAFORMAT_FLOAT_MAT3X4: buf_writeRequired<float>(mrb, vals, navail, 12, data); break;

		case DATAFORMAT_FLOAT_MAT4X2: buf_writeRequired<float>(mrb, vals, navail, 8, data); break;
		case DATAFORMAT_FLOAT_MAT4X3: buf_writeRequired<float>(mrb, vals, navail, 12, data); break;
		case DATAFORMAT_FLOAT_MAT4X4: buf_writeRequired<float>(mrb, vals, navail, 16, data); break;

		case DATAFORMAT_INT32:      buf_writeData<int32>(mrb, vals, navail, 1, data); break;
		case DATAFORMAT_INT32_VEC2: buf_writeData<int32>(mrb, vals, navail, 2, data); break;
		case DATAFORMAT_INT32_VEC3: buf_writeData<int32>(mrb, vals, navail, 3, data); break;
		case DATAFORMAT_INT32_VEC4: buf_writeData<int32>(mrb, vals, navail, 4, data); break;

		case DATAFORMAT_UINT32:      buf_writeData<uint32>(mrb, vals, navail, 1, data); break;
		case DATAFORMAT_UINT32_VEC2: buf_writeData<uint32>(mrb, vals, navail, 2, data); break;
		case DATAFORMAT_UINT32_VEC3: buf_writeData<uint32>(mrb, vals, navail, 3, data); break;
		case DATAFORMAT_UINT32_VEC4: buf_writeData<uint32>(mrb, vals, navail, 4, data); break;

		case DATAFORMAT_SNORM8_VEC4: buf_writeSNorm<int8>(mrb, vals, navail, 4, data); break;
		case DATAFORMAT_UNORM8_VEC4: buf_writeUNorm<uint8>(mrb, vals, navail, 4, data); break;
		case DATAFORMAT_INT8_VEC4:   buf_writeData<int8>(mrb, vals, navail, 4, data); break;
		case DATAFORMAT_UINT8_VEC4:  buf_writeData<uint8>(mrb, vals, navail, 4, data); break;

		case DATAFORMAT_SNORM16_VEC2: buf_writeSNorm<int16>(mrb, vals, navail, 2, data); break;
		case DATAFORMAT_SNORM16_VEC4: buf_writeSNorm<int16>(mrb, vals, navail, 4, data); break;

		case DATAFORMAT_UNORM16_VEC2: buf_writeUNorm<uint16>(mrb, vals, navail, 2, data); break;
		case DATAFORMAT_UNORM16_VEC4: buf_writeUNorm<uint16>(mrb, vals, navail, 4, data); break;

		case DATAFORMAT_INT16_VEC2: buf_writeData<int16>(mrb, vals, navail, 2, data); break;
		case DATAFORMAT_INT16_VEC4: buf_writeData<int16>(mrb, vals, navail, 4, data); break;

		case DATAFORMAT_UINT16:      buf_writeData<uint16>(mrb, vals, navail, 1, data); break;
		case DATAFORMAT_UINT16_VEC2: buf_writeData<uint16>(mrb, vals, navail, 2, data); break;
		case DATAFORMAT_UINT16_VEC4: buf_writeData<uint16>(mrb, vals, navail, 4, data); break;

		default: break;
	}
}

// Read templates: append `components` values of type T from `data` to the Ruby
// Array `out`. Mirror readData/readSNormData/readUNormData in wrap_Buffer.cpp.
template <typename T>
static void buf_readData(mrb_state *mrb, int components, const char *data, mrb_value out)
{
	auto cd = (const T *) data;
	for (int i = 0; i < components; i++)
		mrb_ary_push(mrb, out, mrbx_number(mrb, (double) cd[i]));
}

template <typename T>
static void buf_readSNorm(mrb_state *mrb, int components, const char *data, mrb_value out)
{
	auto cd = (const T *) data;
	constexpr auto maxval = std::numeric_limits<T>::max();
	for (int i = 0; i < components; i++)
	{
		double d = (double) cd[i] / (double) maxval;
		mrb_ary_push(mrb, out, mrbx_number(mrb, d < -1.0 ? -1.0 : d));
	}
}

template <typename T>
static void buf_readUNorm(mrb_state *mrb, int components, const char *data, mrb_value out)
{
	auto cd = (const T *) data;
	constexpr auto maxval = std::numeric_limits<T>::max();
	for (int i = 0; i < components; i++)
		mrb_ary_push(mrb, out, mrbx_number(mrb, (double) cd[i] / (double) maxval));
}

// mruby analog of luax_readbufferdata: append one member's components (as
// Numbers) to `out`. Faithful to wrap_Buffer.cpp's read path (no matrix
// formats, matching the Lua reader).
static void buf_readbufferdata(mrb_state *mrb, DataFormat format, const char *data, mrb_value out)
{
	switch (format)
	{
		case DATAFORMAT_FLOAT:      buf_readData<float>(mrb, 1, data, out); break;
		case DATAFORMAT_FLOAT_VEC2: buf_readData<float>(mrb, 2, data, out); break;
		case DATAFORMAT_FLOAT_VEC3: buf_readData<float>(mrb, 3, data, out); break;
		case DATAFORMAT_FLOAT_VEC4: buf_readData<float>(mrb, 4, data, out); break;

		case DATAFORMAT_INT32:      buf_readData<int32>(mrb, 1, data, out); break;
		case DATAFORMAT_INT32_VEC2: buf_readData<int32>(mrb, 2, data, out); break;
		case DATAFORMAT_INT32_VEC3: buf_readData<int32>(mrb, 3, data, out); break;
		case DATAFORMAT_INT32_VEC4: buf_readData<int32>(mrb, 4, data, out); break;

		case DATAFORMAT_UINT32:      buf_readData<uint32>(mrb, 1, data, out); break;
		case DATAFORMAT_UINT32_VEC2: buf_readData<uint32>(mrb, 2, data, out); break;
		case DATAFORMAT_UINT32_VEC3: buf_readData<uint32>(mrb, 3, data, out); break;
		case DATAFORMAT_UINT32_VEC4: buf_readData<uint32>(mrb, 4, data, out); break;

		case DATAFORMAT_SNORM8_VEC4: buf_readSNorm<int8>(mrb, 4, data, out); break;
		case DATAFORMAT_UNORM8_VEC4: buf_readUNorm<uint8>(mrb, 4, data, out); break;
		case DATAFORMAT_INT8_VEC4:   buf_readData<int8>(mrb, 4, data, out); break;
		case DATAFORMAT_UINT8_VEC4:  buf_readData<uint8>(mrb, 4, data, out); break;

		case DATAFORMAT_SNORM16_VEC2: buf_readSNorm<int16>(mrb, 2, data, out); break;
		case DATAFORMAT_SNORM16_VEC4: buf_readSNorm<int16>(mrb, 4, data, out); break;

		case DATAFORMAT_UNORM16_VEC2: buf_readUNorm<uint16>(mrb, 2, data, out); break;
		case DATAFORMAT_UNORM16_VEC4: buf_readUNorm<uint16>(mrb, 4, data, out); break;

		case DATAFORMAT_INT16_VEC2: buf_readData<int16>(mrb, 2, data, out); break;
		case DATAFORMAT_INT16_VEC4: buf_readData<int16>(mrb, 4, data, out); break;

		case DATAFORMAT_UINT16:      buf_readData<uint16>(mrb, 1, data, out); break;
		case DATAFORMAT_UINT16_VEC2: buf_readData<uint16>(mrb, 2, data, out); break;
		case DATAFORMAT_UINT16_VEC4: buf_readData<uint16>(mrb, 4, data, out); break;

		default: break;
	}
}

Buffer *mrbx_checkbuffer(mrb_state *mrb, mrb_value v)
{
	return mrbx_checktype<Buffer>(mrb, v);
}

// Total number of scalar components across all members of the buffer format.
static int buf_ncomponents(const std::vector<Buffer::DataMember> &members)
{
	int n = 0;
	for (const Buffer::DataMember &m : members)
		n += m.info.components;
	return n;
}

// Write one element's `ncomponents` gathered values into `dst` per the members.
static void buf_write_element(mrb_state *mrb, const std::vector<Buffer::DataMember> &members,
	const mrb_value *comps, int ncomponents, char *dst)
{
	int idx = 0;
	for (const Buffer::DataMember &member : members)
	{
		int nc = member.info.components;
		int avail = ncomponents - idx;
		if (avail < 0) avail = 0;
		buf_writebufferdata(mrb, comps + idx, avail, member.decl.format, dst + member.offset);
		idx += nc;
	}
}

// Stage `count` elements (read from Ruby `arr`, an array-of-component-arrays
// when `tableoftables`, else a flat array) and upload them at element
// `destindex`. Values are gathered into a host buffer first so a malformed
// element raises before any GPU state is touched.
static void buf_fill_from_array(mrb_state *mrb, Buffer *b, mrb_value arr, bool tableoftables,
	int sourceindex, int destindex, int count, int ncomponents)
{
	const std::vector<Buffer::DataMember> &members = b->getDataMembers();
	size_t stride = b->getArrayStride();
	std::vector<char> staging((size_t) count * stride, 0);
	std::vector<mrb_value> comps(ncomponents);
	char *data = staging.data();

	for (int i = 0; i < count; i++)
	{
		if (tableoftables)
		{
			mrb_value el = mrb_ary_ref(mrb, arr, sourceindex + i);
			if (!mrb_array_p(el))
				mrb_raise(mrb, E_ARGUMENT_ERROR, "Buffer array elements must each be an array of component values.");
			for (int j = 0; j < ncomponents; j++)
				comps[j] = mrb_ary_ref(mrb, el, j);
		}
		else
		{
			for (int j = 0; j < ncomponents; j++)
				comps[j] = mrb_ary_ref(mrb, arr, (sourceindex + i) * ncomponents + j);
		}
		buf_write_element(mrb, members, comps.data(), ncomponents, data);
		data += stride;
	}

	mrbx_catchexcept(mrb, [&]() { b->fill((size_t) destindex * stride, (size_t) count * stride, staging.data()); });
}

// set_array_data(data:, source_index:, dest_index:, count:) -- overwrite buffer
// elements from a Data or an Array (array-of-component-arrays or flat). Indices
// are 1-based (as in Lua). Faithful to w_Buffer_setArrayData.
static mrb_value w_buffer_set_array_data(mrb_state *mrb, mrb_value self)
{
	Buffer *t = mrbx_checkbuffer(mrb, self);
	mrb_value v[4];
	mrbx_get_kwargs(mrb, {"data", "source_index", "dest_index", "count"}, 1, v);

	int sourceindex = mrbx_optint(mrb, v[1], 1) - 1;
	int destindex = mrbx_optint(mrb, v[2], 1) - 1;
	if (sourceindex < 0)
		mrb_raise(mrb, E_ARGUMENT_ERROR, "Source start index must be at least 1.");

	int count = -1;
	if (!mrb_undef_p(v[3]) && !mrb_nil_p(v[3]))
	{
		count = mrbx_checkint(mrb, v[3]);
		if (count <= 0)
			mrb_raise(mrb, E_ARGUMENT_ERROR, "Element count must be greater than 0.");
	}

	size_t stride = t->getArrayStride();
	int arraylength = (int) t->getArrayLength();
	if (destindex < 0 || destindex >= arraylength)
		mrb_raisef(mrb, E_ARGUMENT_ERROR, "Invalid buffer start index (must be between 1 and %d).", arraylength);

	if (mrbx_istype<Data>(mrb, v[0]))
	{
		Data *d = mrbx_checktype<Data>(mrb, v[0]);
		int dataarraylength = (int) (d->getSize() / stride);
		if (sourceindex >= dataarraylength)
			mrb_raisef(mrb, E_ARGUMENT_ERROR, "Invalid data start index (must be between 1 and %d).", dataarraylength);

		int maxcount = (dataarraylength - sourceindex < arraylength - destindex)
			? dataarraylength - sourceindex : arraylength - destindex;
		if (count < 0)
			count = maxcount;
		if (count > maxcount)
			mrb_raisef(mrb, E_ARGUMENT_ERROR, "Too many array elements (expected at most %d, got %d).", maxcount, count);

		size_t dataoffset = (size_t) sourceindex * stride;
		size_t datasize = d->getSize() - dataoffset;
		if (datasize > (size_t) count * stride)
			datasize = (size_t) count * stride;
		const void *sourcedata = (const uint8 *) d->getData() + dataoffset;
		mrbx_catchexcept(mrb, [&]() { t->fill((size_t) destindex * stride, datasize, sourcedata); });
		return mrb_nil_value();
	}

	if (!mrb_array_p(v[0]))
		mrb_raise(mrb, E_ARGUMENT_ERROR, "data: must be a Data or an Array.");

	int ncomponents = buf_ncomponents(t->getDataMembers());
	int tablelen = (int) RARRAY_LEN(v[0]);
	bool tableoftables = tablelen > 0 && mrb_array_p(mrb_ary_ref(mrb, v[0], 0));

	if (!tableoftables)
	{
		if (ncomponents == 0 || tablelen % ncomponents != 0)
			mrb_raisef(mrb, E_ARGUMENT_ERROR, "Array length in flat-array set_array_data must be a multiple of the total number of components (%d).", ncomponents);
		tablelen /= ncomponents;
	}

	if (sourceindex >= tablelen)
		mrb_raisef(mrb, E_ARGUMENT_ERROR, "Invalid data start index (must be between 1 and %d).", tablelen);

	count = count >= 0 ? (count < tablelen - sourceindex ? count : tablelen - sourceindex)
		: tablelen - sourceindex;
	if (destindex + count > arraylength)
		mrb_raisef(mrb, E_ARGUMENT_ERROR, "Too many array elements (expected at most %d, got %d).", arraylength - destindex, count);

	buf_fill_from_array(mrb, t, v[0], tableoftables, sourceindex, destindex, count, ncomponents);
	return mrb_nil_value();
}

// clear(offset:, size:) -- reset a byte range to zero (whole buffer if omitted).
static mrb_value w_buffer_clear(mrb_state *mrb, mrb_value self)
{
	Buffer *t = mrbx_checkbuffer(mrb, self);
	mrb_value v[2];
	mrbx_get_kwargs(mrb, {"offset", "size"}, 0, v);

	size_t offset = 0;
	size_t size = t->getSize();
	if (!mrb_undef_p(v[0]) && !mrb_nil_p(v[0]))
	{
		double offsetp = mrbx_optnumber(mrb, v[0], 0);
		double sizep = mrbx_optnumber(mrb, v[1], (double) t->getSize());
		if (offsetp < 0 || sizep < 0)
			mrb_raise(mrb, E_ARGUMENT_ERROR, "Offset and size parameters cannot be negative.");
		offset = (size_t) offsetp;
		size = (size_t) sizep;
	}
	mrbx_catchexcept(mrb, [&]() { t->clear(offset, size); });
	return mrb_nil_value();
}

static mrb_value w_buffer_get_element_count(mrb_state *mrb, mrb_value self)
{
	return mrbx_integer(mrb, (int) mrbx_checkbuffer(mrb, self)->getArrayLength());
}

static mrb_value w_buffer_get_element_stride(mrb_state *mrb, mrb_value self)
{
	return mrbx_integer(mrb, (int) mrbx_checkbuffer(mrb, self)->getArrayStride());
}

static mrb_value w_buffer_get_size(mrb_state *mrb, mrb_value self)
{
	return mrbx_integer(mrb, (int) mrbx_checkbuffer(mrb, self)->getSize());
}

// get_format -> Array of member Hashes {name:, format:, array_length:,
// location:, offset:, size:}.
static mrb_value w_buffer_get_format(mrb_state *mrb, mrb_value self)
{
	Buffer *t = mrbx_checkbuffer(mrb, self);
	const auto &members = t->getDataMembers();
	mrb_value arr = mrb_ary_new_capa(mrb, (mrb_int) members.size());
	for (const Buffer::DataMember &member : members)
	{
		mrb_value h = mrb_hash_new(mrb);
		hset(mrb, h, "name", mrb_str_new_cstr(mrb, member.decl.name.c_str()));
		const char *formatstr = "unknown";
		getConstant(member.decl.format, formatstr);
		hset(mrb, h, "format", mrb_str_new_cstr(mrb, formatstr));
		hset(mrb, h, "array_length", mrbx_integer(mrb, member.decl.arrayLength));
		hset(mrb, h, "location", mrbx_integer(mrb, member.decl.bindingLocation));
		hset(mrb, h, "offset", mrbx_integer(mrb, (int) member.offset));
		hset(mrb, h, "size", mrbx_integer(mrb, (int) member.size));
		mrb_ary_push(mrb, arr, h);
	}
	return arr;
}

// buffer_type?(type:) -- is the buffer usable as the given usage
// ("vertex"/"index"/"texel"/"shaderstorage"/"indirectarguments")?
static mrb_value w_buffer_is_buffer_type(mrb_state *mrb, mrb_value self)
{
	Buffer *t = mrbx_checkbuffer(mrb, self);
	mrb_value v[1];
	mrbx_get_kwargs(mrb, {"type"}, 1, v);
	std::string str = mrbx_checkstring(mrb, v[0]);
	BufferUsage usage;
	if (!getConstant(str.c_str(), usage))
		mrb_raisef(mrb, E_ARGUMENT_ERROR, "Invalid buffer type: %s", str.c_str());
	return mrbx_boolean(mrb, (t->getUsageFlags() & (1 << usage)) != 0);
}

static mrb_value w_buffer_get_debug_name(mrb_state *mrb, mrb_value self)
{
	const std::string &name = mrbx_checkbuffer(mrb, self)->getDebugName();
	return name.empty() ? mrb_nil_value() : mrb_str_new_cstr(mrb, name.c_str());
}

static const MrbReg bufferFunctions[] =
{
	{ "set_array_data",     w_buffer_set_array_data,    MRB_ARGS_KEY(4, 0) },
	{ "clear",              w_buffer_clear,             MRB_ARGS_KEY(2, 0) },
	{ "get_element_count",  w_buffer_get_element_count, MRB_ARGS_NONE() },
	{ "get_element_stride", w_buffer_get_element_stride, MRB_ARGS_NONE() },
	{ "get_size",           w_buffer_get_size,          MRB_ARGS_NONE() },
	{ "get_format",         w_buffer_get_format,        MRB_ARGS_NONE() },
	{ "buffer_type?",       w_buffer_is_buffer_type,    MRB_ARGS_KEY(1, 0) },
	{ "get_debug_name",     w_buffer_get_debug_name,    MRB_ARGS_NONE() },
	{ nullptr, nullptr, 0 }
};

// Read one buffer data-format declaration from a Ruby Hash {name:, format:,
// array_length:, location:}. format: is required.
static Buffer::DataDeclaration buf_check_declaration(mrb_state *mrb, mrb_value h)
{
	Buffer::DataDeclaration decl("", DATAFORMAT_MAX_ENUM);
	if (!mrb_hash_p(h))
		mrb_raise(mrb, E_ARGUMENT_ERROR, "Each buffer format declaration must be a Hash.");

	mrb_value name = mrb_hash_get(mrb, h, mrb_symbol_value(mrb_intern_cstr(mrb, "name")));
	if (!mrb_nil_p(name))
		decl.name = mrbx_checkstring(mrb, name);

	mrb_value format = mrb_hash_get(mrb, h, mrb_symbol_value(mrb_intern_cstr(mrb, "format")));
	if (mrb_nil_p(format))
		mrb_raise(mrb, E_ARGUMENT_ERROR, "Each buffer format declaration needs a format: field.");
	std::string formatstr = mrbx_checkstring(mrb, format);
	if (!getConstant(formatstr.c_str(), decl.format))
		mrb_raisef(mrb, E_ARGUMENT_ERROR, "Invalid data format: %s", formatstr.c_str());

	mrb_value arrlen = mrb_hash_get(mrb, h, mrb_symbol_value(mrb_intern_cstr(mrb, "array_length")));
	if (!mrb_nil_p(arrlen))
		decl.arrayLength = mrbx_checkint(mrb, arrlen);

	mrb_value loc = mrb_hash_get(mrb, h, mrb_symbol_value(mrb_intern_cstr(mrb, "location")));
	if (!mrb_nil_p(loc))
		decl.bindingLocation = mrbx_checkint(mrb, loc);

	return decl;
}

// new_buffer(format:, data:, count:, usage_flags:, usage:, debug_name:).
// format: is a single format String or an Array of declaration Hashes. Supply
// data: (a Data, or an Array of component arrays / a flat Array) or count: (an
// empty, zero-initialized buffer of N elements). usage_flags: is an Array of
// "vertex"/"index"/"texel"/"shaderstorage"/"indirectarguments"; usage: is the
// data-usage hint ("dynamic" default / "static" / "stream" / "readback").
static mrb_value w_new_buffer(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[6];
	mrbx_get_kwargs(mrb, {"format", "data", "count", "usage_flags", "usage", "debug_name"}, 1, v);

	Buffer::Settings settings(0, BUFFERDATAUSAGE_DYNAMIC);

	if (!mrb_undef_p(v[3]) && mrb_array_p(v[3]))
	{
		mrb_int n = RARRAY_LEN(v[3]);
		for (mrb_int i = 0; i < n; i++)
		{
			std::string str = mrbx_checkstring(mrb, mrb_ary_ref(mrb, v[3], i));
			BufferUsage usage;
			if (!getConstant(str.c_str(), usage))
				mrb_raisef(mrb, E_ARGUMENT_ERROR, "Invalid buffer usage flag: %s", str.c_str());
			settings.usageFlags = (BufferUsageFlags) (settings.usageFlags | (1u << usage));
		}
	}

	if (!mrb_undef_p(v[4]))
	{
		std::string str = mrbx_checkstring(mrb, v[4]);
		if (!getConstant(str.c_str(), settings.dataUsage))
			mrb_raisef(mrb, E_ARGUMENT_ERROR, "Invalid usage hint: %s", str.c_str());
	}

	if (!mrb_undef_p(v[5]))
		settings.debugName = mrbx_checkstring(mrb, v[5]);

	// Format: a single format string, or an array of declaration hashes.
	std::vector<Buffer::DataDeclaration> format;
	if (mrb_string_p(v[0]))
	{
		Buffer::DataDeclaration decl("", DATAFORMAT_MAX_ENUM);
		std::string str = mrbx_checkstring(mrb, v[0]);
		if (!getConstant(str.c_str(), decl.format))
			mrb_raisef(mrb, E_ARGUMENT_ERROR, "Invalid data format: %s", str.c_str());
		format.push_back(decl);
	}
	else if (mrb_array_p(v[0]))
	{
		mrb_int n = RARRAY_LEN(v[0]);
		for (mrb_int i = 0; i < n; i++)
			format.push_back(buf_check_declaration(mrb, mrb_ary_ref(mrb, v[0], i)));
	}
	else
		mrb_raise(mrb, E_ARGUMENT_ERROR, "format: must be a format String or an Array of declaration Hashes.");

	if (format.empty())
		mrb_raise(mrb, E_ARGUMENT_ERROR, "Buffer format must have at least one member.");

	int ncomponents = 0;
	for (const Buffer::DataDeclaration &decl : format)
		ncomponents += getDataFormatInfo(decl.format).components;

	// Resolve the initial data source.
	Data *data = nullptr;
	const void *initialdata = nullptr;
	size_t bytesize = 0;
	size_t arraylength = 0;
	bool tableoftables = false;
	bool fromarray = false;

	if (!mrb_undef_p(v[1]) && mrbx_istype<Data>(mrb, v[1]))
	{
		data = mrbx_checktype<Data>(mrb, v[1]);
		initialdata = data->getData();
		bytesize = data->getSize();
	}
	else if (!mrb_undef_p(v[1]) && mrb_array_p(v[1]))
	{
		fromarray = true;
		mrb_int len = RARRAY_LEN(v[1]);
		tableoftables = len > 0 && mrb_array_p(mrb_ary_ref(mrb, v[1], 0));
		if (tableoftables)
			arraylength = (size_t) len;
		else
		{
			if (ncomponents == 0 || len % ncomponents != 0)
				mrb_raisef(mrb, E_ARGUMENT_ERROR, "Array length in flat-array new_buffer must be a multiple of the total number of components (%d).", ncomponents);
			arraylength = (size_t) (len / ncomponents);
		}
	}
	else if (!mrb_undef_p(v[2]))
	{
		int len = mrbx_checkint(mrb, v[2]);
		if (len <= 0)
			mrb_raise(mrb, E_ARGUMENT_ERROR, "Number of elements must be greater than 0.");
		arraylength = (size_t) len;
		settings.zeroInitialize = true;
	}
	else
		mrb_raise(mrb, E_ARGUMENT_ERROR, "new_buffer needs data: or count:.");

	Buffer *b = nullptr;
	if (mrbx_catchexcept(mrb, [&]() { b = instance()->newBuffer(settings, format, initialdata, bytesize, arraylength); }))
		return mrb_nil_value();

	if (fromarray)
		buf_fill_from_array(mrb, b, v[1], tableoftables, 0, 0, (int) arraylength, ncomponents);

	mrb_value res = mrbx_pushtype(mrb, b);
	b->release();
	return res;
}

// =========================================================================
// Love::GraphicsReadback  (the result of an async readback of a Buffer or
// Texture). Faithful to wrap_GraphicsReadback.cpp + the readback half of
// wrap_Graphics.cpp.
// =========================================================================

static mrb_value w_readback_is_complete(mrb_state *mrb, mrb_value self)
{
	return mrbx_boolean(mrb, mrbx_checktype<GraphicsReadback>(mrb, self)->isComplete());
}

static mrb_value w_readback_has_error(mrb_state *mrb, mrb_value self)
{
	return mrbx_boolean(mrb, mrbx_checktype<GraphicsReadback>(mrb, self)->hasError());
}

static mrb_value w_readback_wait(mrb_state *mrb, mrb_value self)
{
	mrbx_checktype<GraphicsReadback>(mrb, self)->wait();
	return mrb_nil_value();
}

static mrb_value w_readback_update(mrb_state *mrb, mrb_value self)
{
	GraphicsReadback *t = mrbx_checktype<GraphicsReadback>(mrb, self);
	mrbx_catchexcept(mrb, [&]() { t->update(); });
	return mrb_nil_value();
}

static mrb_value w_readback_get_buffer_data(mrb_state *mrb, mrb_value self)
{
	love::data::ByteData *d = mrbx_checktype<GraphicsReadback>(mrb, self)->getBufferData();
	return d ? mrbx_pushtype(mrb, d) : mrb_nil_value();
}

static mrb_value w_readback_get_image_data(mrb_state *mrb, mrb_value self)
{
	love::image::ImageData *d = mrbx_checktype<GraphicsReadback>(mrb, self)->getImageData();
	return d ? mrbx_pushtype(mrb, d) : mrb_nil_value();
}

static const MrbReg graphicsReadbackFunctions[] =
{
	{ "complete?",       w_readback_is_complete,    MRB_ARGS_NONE() },
	{ "error?",          w_readback_has_error,      MRB_ARGS_NONE() },
	{ "wait",            w_readback_wait,           MRB_ARGS_NONE() },
	{ "update",          w_readback_update,         MRB_ARGS_NONE() },
	{ "get_buffer_data", w_readback_get_buffer_data, MRB_ARGS_NONE() },
	{ "get_image_data",  w_readback_get_image_data, MRB_ARGS_NONE() },
	{ nullptr, nullptr, 0 }
};

// Shared parse for the two buffer-readback module functions: buffer + byte
// range, with an optional ByteData dest + dest_offset.
static void readback_parse_buffer_args(mrb_state *mrb, mrb_value *v, Buffer *&b, size_t &offset,
	size_t &size, love::data::ByteData *&dest, size_t &destoffset)
{
	b = mrbx_checkbuffer(mrb, v[0]);
	offset = (size_t) mrbx_optint(mrb, v[1], 0);
	size = mrb_undef_p(v[2]) || mrb_nil_p(v[2]) ? b->getSize() - offset : (size_t) mrbx_checkint(mrb, v[2]);
	dest = nullptr;
	destoffset = 0;
	if (!mrb_undef_p(v[3]) && !mrb_nil_p(v[3]))
	{
		dest = mrbx_checktype<love::data::ByteData>(mrb, v[3]);
		destoffset = (size_t) mrbx_optint(mrb, v[4], 0);
	}
}

// readback_buffer(buffer:, offset:, size:, dest:, dest_offset:) -> ByteData
// (synchronous; stalls the GPU). dest is an optional ByteData to write into.
static mrb_value w_readback_buffer(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[5];
	mrbx_get_kwargs(mrb, {"buffer", "offset", "size", "dest", "dest_offset"}, 1, v);

	Buffer *b; size_t offset, size, destoffset; love::data::ByteData *dest;
	readback_parse_buffer_args(mrb, v, b, offset, size, dest, destoffset);

	love::data::ByteData *out = nullptr;
	if (mrbx_catchexcept(mrb, [&]() { out = instance()->readbackBuffer(b, offset, size, dest, destoffset); }))
		return mrb_nil_value();
	mrb_value res = mrbx_pushtype(mrb, out);
	out->release();
	return res;
}

// readback_buffer_async(...) -> GraphicsReadback (poll complete?/get_buffer_data).
static mrb_value w_readback_buffer_async(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[5];
	mrbx_get_kwargs(mrb, {"buffer", "offset", "size", "dest", "dest_offset"}, 1, v);

	Buffer *b; size_t offset, size, destoffset; love::data::ByteData *dest;
	readback_parse_buffer_args(mrb, v, b, offset, size, dest, destoffset);

	GraphicsReadback *r = nullptr;
	if (mrbx_catchexcept(mrb, [&]() { r = instance()->readbackBufferAsync(b, offset, size, dest, destoffset); }))
		return mrb_nil_value();
	mrb_value res = mrbx_pushtype(mrb, r);
	r->release();
	return res;
}

// Shared parse for the two texture-readback module functions.
static void readback_parse_texture_args(mrb_state *mrb, mrb_value *v, Texture *&t, int &slice,
	int &mipmap, Rect &rect, love::image::ImageData *&dest, int &destx, int &desty)
{
	t = mrbx_checktype<Texture>(mrb, v[0]);

	slice = 0;
	if (t->getTextureType() != TEXTURE_2D)
		slice = mrbx_checkint(mrb, v[1]) - 1;

	mipmap = mrbx_optint(mrb, v[2], 1) - 1;

	rect.x = 0;
	rect.y = 0;
	rect.w = t->getPixelWidth(mipmap);
	rect.h = t->getPixelHeight(mipmap);
	if (!mrb_undef_p(v[3]) && !mrb_nil_p(v[3]))
	{
		rect.x = mrbx_checkint(mrb, v[3]);
		rect.y = mrbx_checkint(mrb, v[4]);
		rect.w = mrbx_checkint(mrb, v[5]);
		rect.h = mrbx_checkint(mrb, v[6]);
	}

	dest = nullptr;
	destx = 0;
	desty = 0;
	if (!mrb_undef_p(v[7]) && !mrb_nil_p(v[7]))
	{
		dest = mrbx_checktype<love::image::ImageData>(mrb, v[7]);
		destx = mrbx_optint(mrb, v[8], 0);
		desty = mrbx_optint(mrb, v[9], 0);
	}
}

// readback_texture(texture:, slice:, mipmap:, x:, y:, width:, height:, dest:,
// dest_x:, dest_y:) -> ImageData (synchronous). slice: is 1-based and required
// for non-2D textures; x/y/width/height default to the whole mip level.
static mrb_value w_readback_texture(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[10];
	mrbx_get_kwargs(mrb, {"texture", "slice", "mipmap", "x", "y", "width", "height", "dest", "dest_x", "dest_y"}, 1, v);

	Texture *t; int slice, mipmap, destx, desty; Rect rect; love::image::ImageData *dest;
	readback_parse_texture_args(mrb, v, t, slice, mipmap, rect, dest, destx, desty);

	love::image::ImageData *out = nullptr;
	if (mrbx_catchexcept(mrb, [&]() { out = instance()->readbackTexture(t, slice, mipmap, rect, dest, destx, desty); }))
		return mrb_nil_value();
	mrb_value res = mrbx_pushtype(mrb, out);
	out->release();
	return res;
}

// readback_texture_async(...) -> GraphicsReadback (poll complete?/get_image_data).
static mrb_value w_readback_texture_async(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[10];
	mrbx_get_kwargs(mrb, {"texture", "slice", "mipmap", "x", "y", "width", "height", "dest", "dest_x", "dest_y"}, 1, v);

	Texture *t; int slice, mipmap, destx, desty; Rect rect; love::image::ImageData *dest;
	readback_parse_texture_args(mrb, v, t, slice, mipmap, rect, dest, destx, desty);

	GraphicsReadback *r = nullptr;
	if (mrbx_catchexcept(mrb, [&]() { r = instance()->readbackTextureAsync(t, slice, mipmap, rect, dest, destx, desty); }))
		return mrb_nil_value();
	mrb_value res = mrbx_pushtype(mrb, r);
	r->release();
	return res;
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
	{ "set_stencil_mode",     w_set_stencil_mode,     MRB_ARGS_KEY(2, 0) },
	{ "get_stencil_mode",     w_get_stencil_mode,     MRB_ARGS_NONE() },
	{ "set_stencil_state",    w_set_stencil_state,    MRB_ARGS_KEY(5, 0) },
	{ "get_stencil_state",    w_get_stencil_state,    MRB_ARGS_NONE() },
	{ "set_depth_mode",       w_set_depth_mode,       MRB_ARGS_KEY(2, 0) },
	{ "get_depth_mode",       w_get_depth_mode,       MRB_ARGS_NONE() },
	{ "present",              w_present,              MRB_ARGS_NONE() },
	{ "get_width",            w_get_width,            MRB_ARGS_NONE() },
	{ "get_height",           w_get_height,           MRB_ARGS_NONE() },
	{ "get_dimensions",       w_get_dimensions,       MRB_ARGS_NONE() },
	{ "new_image",            w_new_image,            MRB_ARGS_KEY(2, 0) },
	{ "new_array_image",      w_new_array_image,      MRB_ARGS_KEY(2, 0) },
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
	{ "new_sprite_batch",     w_new_sprite_batch,     MRB_ARGS_KEY(3, 0) },
	{ "new_text_batch",       w_new_text_batch,       MRB_ARGS_KEY(2, 0) },
	{ "new_particle_system",  w_new_particle_system,  MRB_ARGS_KEY(2, 0) },
	{ "new_mesh",             w_new_mesh,             MRB_ARGS_KEY(7, 0) },
	{ "new_buffer",           w_new_buffer,           MRB_ARGS_KEY(6, 0) },
	{ "readback_buffer",      w_readback_buffer,      MRB_ARGS_KEY(5, 0) },
	{ "readback_buffer_async", w_readback_buffer_async, MRB_ARGS_KEY(5, 0) },
	{ "readback_texture",     w_readback_texture,     MRB_ARGS_KEY(10, 0) },
	{ "readback_texture_async", w_readback_texture_async, MRB_ARGS_KEY(10, 0) },
	{ "new_video",            w_new_video,            MRB_ARGS_KEY(2, 0) },
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
	mrbx_register_type(mrb, SpriteBatch::type, spriteBatchFunctions);
	mrbx_register_type(mrb, TextBatch::type, textBatchFunctions);
	mrbx_register_type(mrb, ParticleSystem::type, particleSystemFunctions);
	mrbx_register_type(mrb, Mesh::type, meshFunctions);
	mrbx_register_type(mrb, Buffer::type, bufferFunctions);
	mrbx_register_type(mrb, GraphicsReadback::type, graphicsReadbackFunctions);
	mrbx_register_type(mrb, Video::type, videoFunctions);
}

} // graphics
} // love
