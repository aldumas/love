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

// mruby port of wrap_Image.cpp + wrap_ImageData.cpp.
//
// Exposes Love::Image (the module) and the Love::ImageData object type. snake
// case names, keyword arguments. ImageData is-a Data, so it inherits the Data
// instance methods (get_string, get_size, ...) through the runtime's
// love::Type-mirrored class hierarchy — the data module must be initialised
// first.
//
// new_image_data has two forms, disambiguated by keyword:
//   Love::Image.new_image_data(width:, height:, format: "rgba8", pixels: <opt>)
//   Love::Image.new_image_data(file: <filename String or Data>)  # decode
// Pixel colors are passed/returned as `[r, g, b, a]` arrays.
//
// The CompressedImageData family (new_compressed_data / compressed?) is deferred
// (#img-compressed) — it needs the CompressedImageData object type wired up.

#include "common/config.h"
#include "common/mrb_runtime.h"
#include "common/Color.h"
#include "common/Data.h"
#include "common/pixelformat.h"

#include "Image.h"

#include "filesystem/Filesystem.h"
#include "filesystem/FileData.h"

namespace love
{
namespace image
{

#define instance() (Module::getInstance<Image>(Module::M_IMAGE))

// --- ImageData instance methods ------------------------------------------

static mrb_value id_clone(mrb_state *mrb, mrb_value self)
{
	ImageData *c = nullptr;
	ImageData *t = mrbx_checktype<ImageData>(mrb, self);
	mrbx_catchexcept(mrb, [&]() { c = t->clone(); });
	mrb_value r = mrbx_pushtype(mrb, c);
	if (c) c->release();
	return r;
}

static mrb_value id_getFormat(mrb_state *mrb, mrb_value self)
{
	ImageData *t = mrbx_checktype<ImageData>(mrb, self);
	const char *fstr = nullptr;
	if (!love::getConstant(t->getFormat(), fstr))
		mrb_raise(mrb, E_RUNTIME_ERROR, "Unknown pixel format.");
	return mrbx_string(mrb, fstr);
}

static mrb_value id_setLinear(mrb_state *mrb, mrb_value self)
{
	ImageData *t = mrbx_checktype<ImageData>(mrb, self);
	mrb_value v[1];
	mrbx_get_kwargs(mrb, {"value"}, 1, v);
	t->setLinear(mrbx_checkboolean(mrb, v[0]));
	return self;
}

static mrb_value id_isLinear(mrb_state *mrb, mrb_value self)
{
	return mrbx_boolean(mrb, mrbx_checktype<ImageData>(mrb, self)->isLinear());
}

static mrb_value id_getWidth(mrb_state *mrb, mrb_value self)
{
	return mrbx_integer(mrb, mrbx_checktype<ImageData>(mrb, self)->getWidth());
}

static mrb_value id_getHeight(mrb_state *mrb, mrb_value self)
{
	return mrbx_integer(mrb, mrbx_checktype<ImageData>(mrb, self)->getHeight());
}

static mrb_value id_getDimensions(mrb_state *mrb, mrb_value self)
{
	ImageData *t = mrbx_checktype<ImageData>(mrb, self);
	mrb_value arr = mrb_ary_new_capa(mrb, 2);
	mrb_ary_push(mrb, arr, mrbx_integer(mrb, t->getWidth()));
	mrb_ary_push(mrb, arr, mrbx_integer(mrb, t->getHeight()));
	return arr;
}

static mrb_value colorarray(mrb_state *mrb, const Colorf &c)
{
	mrb_value arr = mrb_ary_new_capa(mrb, 4);
	mrb_ary_push(mrb, arr, mrbx_number(mrb, c.r));
	mrb_ary_push(mrb, arr, mrbx_number(mrb, c.g));
	mrb_ary_push(mrb, arr, mrbx_number(mrb, c.b));
	mrb_ary_push(mrb, arr, mrbx_number(mrb, c.a));
	return arr;
}

// Reads a Colorf from a Ruby [r, g, b, a] array, honoring the format's
// component count (missing components default to 0, alpha to 1).
static Colorf readcolor(mrb_state *mrb, mrb_value v, int components)
{
	Colorf c(0, 0, 0, 1);
	if (!mrb_array_p(v))
		mrb_raise(mrb, E_ARGUMENT_ERROR, "expected a color array [r, g, b, a]");

	mrb_int n = RARRAY_LEN(v);
	if (n > 0)                  c.r = (float) mrbx_checknumber(mrb, mrb_ary_ref(mrb, v, 0));
	if (components > 1 && n > 1) c.g = (float) mrbx_checknumber(mrb, mrb_ary_ref(mrb, v, 1));
	if (components > 2 && n > 2) c.b = (float) mrbx_checknumber(mrb, mrb_ary_ref(mrb, v, 2));
	if (components > 3 && n > 3) c.a = (float) mrbx_checknumber(mrb, mrb_ary_ref(mrb, v, 3));
	return c;
}

static mrb_value id_getPixel(mrb_state *mrb, mrb_value self)
{
	ImageData *t = mrbx_checktype<ImageData>(mrb, self);
	mrb_value v[2];
	mrbx_get_kwargs(mrb, {"x", "y"}, 2, v);

	Colorf c;
	if (mrbx_catchexcept(mrb, [&]() { t->getPixel(mrbx_checkint(mrb, v[0]), mrbx_checkint(mrb, v[1]), c); }))
		return mrb_nil_value();
	return colorarray(mrb, c);
}

static mrb_value id_setPixel(mrb_state *mrb, mrb_value self)
{
	ImageData *t = mrbx_checktype<ImageData>(mrb, self);
	mrb_value v[3];
	mrbx_get_kwargs(mrb, {"x", "y", "color"}, 3, v);

	int x = mrbx_checkint(mrb, v[0]);
	int y = mrbx_checkint(mrb, v[1]);
	Colorf c = readcolor(mrb, v[2], love::getPixelFormatColorComponents(t->getFormat()));

	mrbx_catchexcept(mrb, [&]() { t->setPixel(x, y, c); });
	return self;
}

static mrb_value id_mapPixel(mrb_state *mrb, mrb_value self)
{
	ImageData *t = mrbx_checktype<ImageData>(mrb, self);

	// Fetch the block and keyword arguments together.
	mrb_value blk = mrb_nil_value();
	mrb_value kv[4];
	mrb_sym names[4] = {
		mrb_intern_lit(mrb, "sx"), mrb_intern_lit(mrb, "sy"),
		mrb_intern_lit(mrb, "width"), mrb_intern_lit(mrb, "height"),
	};
	const mrb_kwargs kw = { 4, 0, names, kv, nullptr };
	mrb_get_args(mrb, ":&", &kw, &blk);

	if (mrb_nil_p(blk))
		mrb_raise(mrb, E_ARGUMENT_ERROR, "map_pixel requires a block");

	int sx = mrbx_optint(mrb, kv[0], 0);
	int sy = mrbx_optint(mrb, kv[1], 0);
	int w  = mrbx_optint(mrb, kv[2], t->getWidth());
	int h  = mrbx_optint(mrb, kv[3], t->getHeight());

	if (!(t->inside(sx, sy) && t->inside(sx + w - 1, sy + h - 1)))
		mrb_raise(mrb, E_ARGUMENT_ERROR, "Invalid rectangle dimensions.");

	int components = love::getPixelFormatColorComponents(t->getFormat());

	for (int y = sy; y < sy + h; y++)
	{
		for (int x = sx; x < sx + w; x++)
		{
			Colorf c;
			t->getPixel(x, y, c);

			mrb_value args[6] = {
				mrbx_integer(mrb, x), mrbx_integer(mrb, y),
				mrbx_number(mrb, c.r), mrbx_number(mrb, c.g),
				mrbx_number(mrb, c.b), mrbx_number(mrb, c.a),
			};
			mrb_value res = mrb_yield_argv(mrb, blk, 6, args);

			c = readcolor(mrb, res, components);
			t->setPixel(x, y, c);
		}
	}

	return self;
}

static mrb_value id_paste(mrb_state *mrb, mrb_value self)
{
	ImageData *t = mrbx_checktype<ImageData>(mrb, self);
	mrb_value v[7];
	mrbx_get_kwargs(mrb, {"source", "dx", "dy", "sx", "sy", "width", "height"}, 3, v);

	ImageData *src = mrbx_checktype<ImageData>(mrb, v[0]);
	int dx = mrbx_checkint(mrb, v[1]);
	int dy = mrbx_checkint(mrb, v[2]);
	int sx = mrbx_optint(mrb, v[3], 0);
	int sy = mrbx_optint(mrb, v[4], 0);
	int sw = mrbx_optint(mrb, v[5], src->getWidth());
	int sh = mrbx_optint(mrb, v[6], src->getHeight());

	t->paste(src, dx, dy, sx, sy, sw, sh);
	return self;
}

static mrb_value id_encode(mrb_state *mrb, mrb_value self)
{
	ImageData *t = mrbx_checktype<ImageData>(mrb, self);
	mrb_value v[2];
	mrbx_get_kwargs(mrb, {"format", "filename"}, 1, v);

	std::string fmt = mrbx_checkstring(mrb, v[0]);
	FormatHandler::EncodedFormat format;
	if (!ImageData::getConstant(fmt.c_str(), format))
		mrb_raisef(mrb, E_ARGUMENT_ERROR, "invalid encoded image format: %s", fmt.c_str());

	bool hasfilename = !mrb_undef_p(v[1]);
	std::string filename = hasfilename ? mrbx_checkstring(mrb, v[1]) : ("Image." + fmt);

	love::filesystem::FileData *fd = nullptr;
	if (mrbx_catchexcept(mrb, [&]() { fd = t->encode(format, filename.c_str(), hasfilename); }))
		return mrb_nil_value();

	mrb_value r = mrbx_pushtype(mrb, fd);
	if (fd) fd->release();
	return r;
}

static const MrbReg imageDataFunctions[] =
{
	{ "clone",          id_clone,         MRB_ARGS_NONE() },
	{ "get_format",     id_getFormat,     MRB_ARGS_NONE() },
	{ "set_linear",     id_setLinear,     MRB_ARGS_KEY(1, 0) },
	{ "linear?",        id_isLinear,      MRB_ARGS_NONE() },
	{ "get_width",      id_getWidth,      MRB_ARGS_NONE() },
	{ "get_height",     id_getHeight,     MRB_ARGS_NONE() },
	{ "get_dimensions", id_getDimensions, MRB_ARGS_NONE() },
	{ "get_pixel",      id_getPixel,      MRB_ARGS_KEY(2, 0) },
	{ "set_pixel",      id_setPixel,      MRB_ARGS_KEY(3, 0) },
	{ "map_pixel",      id_mapPixel,      MRB_ARGS_KEY(4, 0) | MRB_ARGS_BLOCK() },
	{ "paste",          id_paste,         MRB_ARGS_KEY(7, 0) },
	{ "encode",         id_encode,        MRB_ARGS_KEY(2, 0) },
	{ nullptr, nullptr, 0 }
};

// --- Module-level functions ----------------------------------------------

static mrb_value w_newImageData(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[5]; // width, height, format, pixels, file
	mrbx_get_kwargs(mrb, {"width", "height", "format", "pixels", "file"}, 0, v);

	if (!mrb_undef_p(v[0])) // create form: width/height (+ optional raw pixels)
	{
		int w = mrbx_checkint(mrb, v[0]);
		int h = mrbx_checkint(mrb, v[1]);
		if (w <= 0 || h <= 0)
			mrb_raise(mrb, E_ARGUMENT_ERROR, "Invalid image size.");

		PixelFormat format = PIXELFORMAT_RGBA8_UNORM;
		if (!mrb_undef_p(v[2]))
		{
			std::string fstr = mrbx_checkstring(mrb, v[2]);
			if (!love::getConstant(fstr.c_str(), format))
				mrb_raisef(mrb, E_ARGUMENT_ERROR, "invalid pixel format: %s", fstr.c_str());
		}

		const char *bytes = nullptr;
		size_t numbytes = 0;
		mrb_value hold = mrb_nil_value();
		if (!mrb_undef_p(v[3]))
		{
			if (mrbx_istype<Data>(mrb, v[3]))
			{
				Data *d = mrbx_checktype<Data>(mrb, v[3]);
				bytes = (const char *) d->getData();
				numbytes = d->getSize();
			}
			else
			{
				hold = mrb_ensure_string_type(mrb, v[3]);
				bytes = RSTRING_PTR(hold);
				numbytes = RSTRING_LEN(hold);
			}
		}

		ImageData *t = nullptr;
		if (mrbx_catchexcept(mrb, [&]() { t = instance()->newImageData(w, h, format); }))
			return mrb_nil_value();

		if (bytes)
		{
			if (numbytes != t->getSize())
			{
				t->release();
				mrb_raise(mrb, E_ARGUMENT_ERROR, "The size of the raw pixel data must match the ImageData's size in bytes.");
			}
			memcpy(t->getData(), bytes, t->getSize());
		}

		mrb_value r = mrbx_pushtype(mrb, t);
		t->release();
		return r;
	}
	else if (!mrb_undef_p(v[4])) // decode form: from a filename or Data
	{
		Data *data = nullptr;
		if (mrbx_istype<Data>(mrb, v[4]))
		{
			data = mrbx_checktype<Data>(mrb, v[4]);
			data->retain();
		}
		else
		{
			std::string filename = mrbx_checkstring(mrb, v[4]);
			auto fs = Module::getInstance<love::filesystem::Filesystem>(Module::M_FILESYSTEM);
			if (fs == nullptr)
				mrb_raise(mrb, E_RUNTIME_ERROR, "The filesystem module must be loaded to decode an image from a filename.");
			if (mrbx_catchexcept(mrb, [&]() { data = fs->read(filename.c_str()); }))
				return mrb_nil_value();
		}

		ImageData *t = nullptr;
		bool err = mrbx_catchexcept(mrb, [&]() { t = instance()->newImageData(data); });
		data->release();
		if (err)
			return mrb_nil_value();

		mrb_value r = mrbx_pushtype(mrb, t);
		t->release();
		return r;
	}

	mrb_raise(mrb, E_ARGUMENT_ERROR, "expected `width:`/`height:` (create) or `file:` (decode)");
	return mrb_nil_value();
}

static mrb_value w_newCubeFaces(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[1];
	mrbx_get_kwargs(mrb, {"image_data"}, 1, v);

	ImageData *id = mrbx_checktype<ImageData>(mrb, v[0]);
	std::vector<StrongRef<ImageData>> faces;
	if (mrbx_catchexcept(mrb, [&]() { faces = instance()->newCubeFaces(id); }))
		return mrb_nil_value();

	mrb_value arr = mrb_ary_new_capa(mrb, (mrb_int) faces.size());
	for (auto &face : faces)
		mrb_ary_push(mrb, arr, mrbx_pushtype(mrb, face.get()));
	return arr;
}

static const MrbReg functions[] =
{
	{ "new_image_data", w_newImageData, MRB_ARGS_KEY(5, 0) },
	{ "new_cube_faces", w_newCubeFaces, MRB_ARGS_KEY(1, 0) },
	// TODO(mruby) #img-compressed: new_compressed_data / compressed? deferred —
	// need the CompressedImageData object type wired up. PORTING.md §A
	{ nullptr, nullptr, 0 }
};

// Equivalent of luaopen_love_image: creates the module instance, registers the
// Love::Image module and the Love::ImageData type. ImageData inherits the Data
// instance methods via the class hierarchy, so the data module must init first.
extern "C" void mrb_love_image_init(mrb_state *mrb)
{
	Image *inst = instance();
	if (inst == nullptr)
		inst = new love::image::Image();
	else
		inst->retain();

	WrappedModule w;
	w.module = inst;
	w.name = "Image";
	w.type = &Image::type;
	w.functions = functions;

	mrbx_register_module(mrb, w);
	mrbx_register_type(mrb, ImageData::type, imageDataFunctions);
}

} // image
} // love
