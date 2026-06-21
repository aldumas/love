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

// mruby port of wrap_Font.cpp + wrap_Rasterizer.cpp + wrap_GlyphData.cpp.
//
// Exposes Love::Font (the module) plus the Love::Rasterizer and Love::GlyphData
// object types, with snake_case names and keyword arguments. The backend is the
// real freetype::Font (links freetype + harfbuzz); the default font is the
// embedded gzip-compressed NotoSans, decompressed via the data module.
//
// GlyphData is-a Data, so it inherits the Data instance methods through the
// runtime's love::Type-mirrored class hierarchy -- the data module must init
// first. Rasterizers that read images (BMFont / image) and the image-decoding
// `file:`/`image_data:` forms need the image module, which must also be loaded.

#include "common/config.h"
#include "common/mrb_runtime.h"
#include "common/pixelformat.h"
#include "common/int.h"

#include "Font.h"
#include "freetype/Font.h"
#include "Rasterizer.h"
#include "TrueTypeRasterizer.h"
#include "GlyphData.h"

#include "filesystem/Filesystem.h"
#include "filesystem/FileData.h"
#include "image/Image.h"
#include "image/ImageData.h"
// The Ruby name "Font" is shared with the graphics Font *type* (see init).
#include "graphics/Font.h"

#include <string>
#include <vector>

namespace love
{
namespace font
{

#define instance() (Module::getInstance<Font>(Module::M_FONT))

// --- shared argument helpers ---------------------------------------------

// Resolve a kwarg into a retained Data: a Data object as-is, or a filename
// String read via the filesystem module. Returns nullptr with an mruby
// exception pending on failure.
static Data *getData(mrb_state *mrb, mrb_value v)
{
	if (mrbx_istype<Data>(mrb, v))
	{
		Data *d = mrbx_checktype<Data>(mrb, v);
		d->retain();
		return d;
	}
	std::string filename = mrbx_checkstring(mrb, v);
	auto fs = Module::getInstance<love::filesystem::Filesystem>(Module::M_FILESYSTEM);
	if (fs == nullptr)
		mrb_raise(mrb, E_RUNTIME_ERROR, "The filesystem module must be loaded to read a font by name.");
	Data *data = nullptr;
	mrbx_catchexcept(mrb, [&]() { data = fs->read(filename.c_str()); });
	return data;
}

// Like getData, but yields a FileData (needed by newRasterizer / BMFont, which
// inspect the filename). Accepts a FileData object or a filename String.
static love::filesystem::FileData *getFileData(mrb_state *mrb, mrb_value v)
{
	if (mrbx_istype<love::filesystem::FileData>(mrb, v))
	{
		auto *d = mrbx_checktype<love::filesystem::FileData>(mrb, v);
		d->retain();
		return d;
	}
	std::string filename = mrbx_checkstring(mrb, v);
	auto fs = Module::getInstance<love::filesystem::Filesystem>(Module::M_FILESYSTEM);
	if (fs == nullptr)
		mrb_raise(mrb, E_RUNTIME_ERROR, "The filesystem module must be loaded to read a font by name.");
	love::filesystem::FileData *data = nullptr;
	mrbx_catchexcept(mrb, [&]() { data = fs->read(filename.c_str()); });
	return data;
}

// Resolve a kwarg into a retained ImageData: an ImageData object as-is, or a
// filename String / Data decoded via the image module.
static love::image::ImageData *getImageData(mrb_state *mrb, mrb_value v)
{
	if (mrbx_istype<love::image::ImageData>(mrb, v))
	{
		auto *d = mrbx_checktype<love::image::ImageData>(mrb, v);
		d->retain();
		return d;
	}
	auto img = Module::getInstance<love::image::Image>(Module::M_IMAGE);
	if (img == nullptr)
		mrb_raise(mrb, E_RUNTIME_ERROR, "The image module must be loaded to decode font images.");
	Data *fd = getData(mrb, v);
	if (fd == nullptr)
		return nullptr;
	love::image::ImageData *id = nullptr;
	bool err = mrbx_catchexcept(mrb, [&]() { id = img->newImageData(fd); });
	fd->release();
	return err ? nullptr : id;
}

static TrueTypeRasterizer::Settings readSettings(mrb_state *mrb, mrb_value hinting, mrb_value dpiscale, mrb_value sdf)
{
	TrueTypeRasterizer::Settings s;
	if (!mrb_undef_p(hinting))
	{
		std::string h = mrbx_checkstring(mrb, hinting);
		if (!TrueTypeRasterizer::getConstant(h.c_str(), s.hinting))
			mrb_raisef(mrb, E_ARGUMENT_ERROR, "Invalid TrueType font hinting mode: %s", h.c_str());
	}
	if (!mrb_undef_p(dpiscale))
		s.dpiScale.set((float) mrbx_checknumber(mrb, dpiscale));
	if (!mrb_undef_p(sdf))
		s.sdf = mrbx_checkboolean(mrb, sdf);
	return s;
}

static mrb_value pushrasterizer(mrb_state *mrb, Rasterizer *t)
{
	mrb_value r = mrbx_pushtype(mrb, t);
	if (t) t->release();
	return r;
}

// --- module functions ----------------------------------------------------

// new_true_type_rasterizer(size:, file:, hinting:, dpiscale:, sdf:)
//   - with no file: the embedded default font (size default 13)
//   - with file: a TTF filename String or Data (size default 12)
static mrb_value w_newTrueTypeRasterizer(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[5];
	mrbx_get_kwargs(mrb, {"size", "file", "hinting", "dpiscale", "sdf"}, 0, v);

	TrueTypeRasterizer::Settings settings = readSettings(mrb, v[2], v[3], v[4]);
	Rasterizer *t = nullptr;

	if (mrb_undef_p(v[1]))
	{
		int size = mrbx_optint(mrb, v[0], 13);
		if (mrbx_catchexcept(mrb, [&]() { t = instance()->newTrueTypeRasterizer(size, settings); }))
			return mrb_nil_value();
	}
	else
	{
		int size = mrbx_optint(mrb, v[0], 12);
		Data *d = getData(mrb, v[1]);
		if (d == nullptr)
			return mrb_nil_value();
		bool err = mrbx_catchexcept(mrb, [&]() { t = instance()->newTrueTypeRasterizer(d, size, settings); });
		d->release();
		if (err)
			return mrb_nil_value();
	}

	return pushrasterizer(mrb, t);
}

// new_rasterizer(...) dispatches: no file (or a size given) -> TrueType; a file
// alone -> auto-detect the font format (TTF or BMFont) from the file content.
static mrb_value w_newRasterizer(mrb_state *mrb, mrb_value self)
{
	mrb_value v[5];
	mrbx_get_kwargs(mrb, {"size", "file", "hinting", "dpiscale", "sdf"}, 0, v);

	if (mrb_undef_p(v[1]) || !mrb_undef_p(v[0]))
		return w_newTrueTypeRasterizer(mrb, self);

	love::filesystem::FileData *d = getFileData(mrb, v[1]);
	if (d == nullptr)
		return mrb_nil_value();
	Rasterizer *t = nullptr;
	bool err = mrbx_catchexcept(mrb, [&]() { t = instance()->newRasterizer(d); });
	d->release();
	if (err)
		return mrb_nil_value();
	return pushrasterizer(mrb, t);
}

// new_bm_font_rasterizer(file:, images:, dpiscale:) -- file is the BMFont .fnt
// definition; images is an optional ImageData (or filename/Data) or Array of them.
static mrb_value w_newBMFontRasterizer(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[3];
	mrbx_get_kwargs(mrb, {"file", "images", "dpiscale"}, 1, v);

	love::filesystem::FileData *d = getFileData(mrb, v[0]);
	if (d == nullptr)
		return mrb_nil_value();

	std::vector<love::image::ImageData *> images;
	auto cleanup = [&]() { d->release(); for (auto *id : images) id->release(); };

	if (!mrb_undef_p(v[1]))
	{
		if (mrb_array_p(v[1]))
		{
			for (mrb_int i = 0; i < RARRAY_LEN(v[1]); i++)
			{
				love::image::ImageData *id = getImageData(mrb, mrb_ary_ref(mrb, v[1], i));
				if (id == nullptr) { cleanup(); return mrb_nil_value(); }
				images.push_back(id);
			}
		}
		else
		{
			love::image::ImageData *id = getImageData(mrb, v[1]);
			if (id == nullptr) { cleanup(); return mrb_nil_value(); }
			images.push_back(id);
		}
	}

	float dpiscale = (float) mrbx_optnumber(mrb, v[2], 1.0);
	Rasterizer *t = nullptr;
	bool err = mrbx_catchexcept(mrb, [&]() { t = instance()->newBMFontRasterizer(d, images, dpiscale); });
	cleanup();
	if (err)
		return mrb_nil_value();
	return pushrasterizer(mrb, t);
}

// new_image_rasterizer(image_data:, glyphs:, extra_spacing:, dpiscale:)
static mrb_value w_newImageRasterizer(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[4];
	mrbx_get_kwargs(mrb, {"image_data", "glyphs", "extra_spacing", "dpiscale"}, 2, v);

	love::image::ImageData *d = getImageData(mrb, v[0]);
	if (d == nullptr)
		return mrb_nil_value();
	std::string glyphs = mrbx_checkstring(mrb, v[1]);
	int extraspacing = mrbx_optint(mrb, v[2], 0);
	float dpiscale = (float) mrbx_optnumber(mrb, v[3], 1.0);

	Rasterizer *t = nullptr;
	bool err = mrbx_catchexcept(mrb, [&]() { t = instance()->newImageRasterizer(d, glyphs, extraspacing, dpiscale); });
	d->release();
	if (err)
		return mrb_nil_value();
	return pushrasterizer(mrb, t);
}

// new_glyph_data(rasterizer:, glyph:) -- glyph is a String character or a
// codepoint Integer.
static mrb_value w_newGlyphData(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[2];
	mrbx_get_kwargs(mrb, {"rasterizer", "glyph"}, 2, v);

	Rasterizer *r = mrbx_checktype<Rasterizer>(mrb, v[0]);
	GlyphData *t = nullptr;

	if (mrb_string_p(v[1]))
	{
		std::string glyph = mrbx_checkstring(mrb, v[1]);
		if (mrbx_catchexcept(mrb, [&]() { t = instance()->newGlyphData(r, glyph); }))
			return mrb_nil_value();
	}
	else
	{
		uint32 g = (uint32) mrbx_checkint(mrb, v[1]);
		t = instance()->newGlyphData(r, g);
	}

	mrb_value res = mrbx_pushtype(mrb, t);
	if (t) t->release();
	return res;
}

static const MrbReg functions[] =
{
	{ "new_rasterizer",            w_newRasterizer,          MRB_ARGS_KEY(5, 0) },
	{ "new_true_type_rasterizer",  w_newTrueTypeRasterizer,  MRB_ARGS_KEY(5, 0) },
	{ "new_bm_font_rasterizer",    w_newBMFontRasterizer,    MRB_ARGS_KEY(3, 0) },
	{ "new_image_rasterizer",      w_newImageRasterizer,     MRB_ARGS_KEY(4, 0) },
	{ "new_glyph_data",            w_newGlyphData,           MRB_ARGS_KEY(2, 0) },
	{ nullptr, nullptr, 0 }
};

// --- Rasterizer instance methods -----------------------------------------

static mrb_value r_getHeight    (mrb_state *mrb, mrb_value self) { return mrbx_integer(mrb, mrbx_checktype<Rasterizer>(mrb, self)->getHeight()); }
static mrb_value r_getAdvance   (mrb_state *mrb, mrb_value self) { return mrbx_integer(mrb, mrbx_checktype<Rasterizer>(mrb, self)->getAdvance()); }
static mrb_value r_getAscent    (mrb_state *mrb, mrb_value self) { return mrbx_integer(mrb, mrbx_checktype<Rasterizer>(mrb, self)->getAscent()); }
static mrb_value r_getDescent   (mrb_state *mrb, mrb_value self) { return mrbx_integer(mrb, mrbx_checktype<Rasterizer>(mrb, self)->getDescent()); }
static mrb_value r_getLineHeight(mrb_state *mrb, mrb_value self) { return mrbx_integer(mrb, mrbx_checktype<Rasterizer>(mrb, self)->getLineHeight()); }
static mrb_value r_getGlyphCount(mrb_state *mrb, mrb_value self) { return mrbx_integer(mrb, mrbx_checktype<Rasterizer>(mrb, self)->getGlyphCount()); }

static mrb_value r_getGlyphData(mrb_state *mrb, mrb_value self)
{
	Rasterizer *t = mrbx_checktype<Rasterizer>(mrb, self);
	mrb_value v[1];
	mrbx_get_kwargs(mrb, {"glyph"}, 1, v);

	GlyphData *g = nullptr;
	if (mrb_string_p(v[0]))
	{
		std::string glyph = mrbx_checkstring(mrb, v[0]);
		if (mrbx_catchexcept(mrb, [&]() { g = t->getGlyphData(glyph); }))
			return mrb_nil_value();
	}
	else
	{
		uint32 c = (uint32) mrbx_checkint(mrb, v[0]);
		if (mrbx_catchexcept(mrb, [&]() { g = t->getGlyphData(c); }))
			return mrb_nil_value();
	}

	mrb_value res = mrbx_pushtype(mrb, g);
	if (g) g->release();
	return res;
}

// has_glyphs?(glyphs:) -- glyphs is a String (all codepoints must exist) or an
// Array of String characters / codepoint Integers (all must exist).
static mrb_value r_hasGlyphs(mrb_state *mrb, mrb_value self)
{
	Rasterizer *t = mrbx_checktype<Rasterizer>(mrb, self);
	mrb_value v[1];
	mrbx_get_kwargs(mrb, {"glyphs"}, 1, v);
	mrb_value g = v[0];
	bool has = false;

	if (mrb_array_p(g))
	{
		has = RARRAY_LEN(g) > 0;
		for (mrb_int i = 0; i < RARRAY_LEN(g); i++)
		{
			mrb_value e = mrb_ary_ref(mrb, g, i);
			bool h = false;
			if (mrb_string_p(e))
			{
				std::string s = mrbx_checkstring(mrb, e);
				mrbx_catchexcept(mrb, [&]() { h = t->hasGlyphs(s); });
			}
			else
			{
				uint32 c = (uint32) mrbx_checkint(mrb, e);
				mrbx_catchexcept(mrb, [&]() { h = t->hasGlyph(c); });
			}
			if (!h) { has = false; break; }
		}
	}
	else if (mrb_string_p(g))
	{
		std::string s = mrbx_checkstring(mrb, g);
		mrbx_catchexcept(mrb, [&]() { has = t->hasGlyphs(s); });
	}
	else
	{
		uint32 c = (uint32) mrbx_checkint(mrb, g);
		mrbx_catchexcept(mrb, [&]() { has = t->hasGlyph(c); });
	}

	return mrbx_boolean(mrb, has);
}

static const MrbReg rasterizerFunctions[] =
{
	{ "get_height",      r_getHeight,     MRB_ARGS_NONE() },
	{ "get_advance",     r_getAdvance,    MRB_ARGS_NONE() },
	{ "get_ascent",      r_getAscent,     MRB_ARGS_NONE() },
	{ "get_descent",     r_getDescent,    MRB_ARGS_NONE() },
	{ "get_line_height", r_getLineHeight, MRB_ARGS_NONE() },
	{ "get_glyph_count", r_getGlyphCount, MRB_ARGS_NONE() },
	{ "get_glyph_data",  r_getGlyphData,  MRB_ARGS_KEY(1, 0) },
	{ "has_glyphs?",     r_hasGlyphs,     MRB_ARGS_KEY(1, 0) },
	{ nullptr, nullptr, 0 }
};

// --- GlyphData instance methods (is-a Data) ------------------------------

static mrb_value gd_clone(mrb_state *mrb, mrb_value self)
{
	GlyphData *t = mrbx_checktype<GlyphData>(mrb, self), *c = nullptr;
	mrbx_catchexcept(mrb, [&]() { c = t->clone(); });
	mrb_value r = mrbx_pushtype(mrb, c);
	if (c) c->release();
	return r;
}

static mrb_value gd_getWidth  (mrb_state *mrb, mrb_value self) { return mrbx_integer(mrb, mrbx_checktype<GlyphData>(mrb, self)->getWidth()); }
static mrb_value gd_getHeight (mrb_state *mrb, mrb_value self) { return mrbx_integer(mrb, mrbx_checktype<GlyphData>(mrb, self)->getHeight()); }
static mrb_value gd_getGlyph  (mrb_state *mrb, mrb_value self) { return mrbx_integer(mrb, (int) mrbx_checktype<GlyphData>(mrb, self)->getGlyph()); }
static mrb_value gd_getAdvance(mrb_state *mrb, mrb_value self) { return mrbx_integer(mrb, mrbx_checktype<GlyphData>(mrb, self)->getAdvance()); }

static mrb_value gd_getDimensions(mrb_state *mrb, mrb_value self)
{
	GlyphData *t = mrbx_checktype<GlyphData>(mrb, self);
	mrb_value arr = mrb_ary_new_capa(mrb, 2);
	mrb_ary_push(mrb, arr, mrbx_integer(mrb, t->getWidth()));
	mrb_ary_push(mrb, arr, mrbx_integer(mrb, t->getHeight()));
	return arr;
}

static mrb_value gd_getBearing(mrb_state *mrb, mrb_value self)
{
	GlyphData *t = mrbx_checktype<GlyphData>(mrb, self);
	mrb_value arr = mrb_ary_new_capa(mrb, 2);
	mrb_ary_push(mrb, arr, mrbx_integer(mrb, t->getBearingX()));
	mrb_ary_push(mrb, arr, mrbx_integer(mrb, t->getBearingY()));
	return arr;
}

static mrb_value gd_getBoundingBox(mrb_state *mrb, mrb_value self)
{
	GlyphData *t = mrbx_checktype<GlyphData>(mrb, self);
	int minX = t->getMinX(), minY = t->getMinY();
	int width = t->getMaxX() - minX, height = t->getMaxY() - minY;
	mrb_value out = mrb_hash_new(mrb);
	mrb_hash_set(mrb, out, mrb_symbol_value(mrb_intern_lit(mrb, "x")), mrbx_integer(mrb, minX));
	mrb_hash_set(mrb, out, mrb_symbol_value(mrb_intern_lit(mrb, "y")), mrbx_integer(mrb, minY));
	mrb_hash_set(mrb, out, mrb_symbol_value(mrb_intern_lit(mrb, "width")), mrbx_integer(mrb, width));
	mrb_hash_set(mrb, out, mrb_symbol_value(mrb_intern_lit(mrb, "height")), mrbx_integer(mrb, height));
	return out;
}

static mrb_value gd_getGlyphString(mrb_state *mrb, mrb_value self)
{
	GlyphData *t = mrbx_checktype<GlyphData>(mrb, self);
	std::string s;
	if (mrbx_catchexcept(mrb, [&]() { s = t->getGlyphString(); }))
		return mrb_nil_value();
	return mrbx_string(mrb, s);
}

static mrb_value gd_getFormat(mrb_state *mrb, mrb_value self)
{
	GlyphData *t = mrbx_checktype<GlyphData>(mrb, self);
	const char *str = nullptr;
	if (!love::getConstant(t->getFormat(), str))
		return mrbx_string(mrb, "unknown");
	return mrbx_string(mrb, str);
}

static const MrbReg glyphDataFunctions[] =
{
	{ "clone",            gd_clone,          MRB_ARGS_NONE() },
	{ "get_width",        gd_getWidth,       MRB_ARGS_NONE() },
	{ "get_height",       gd_getHeight,      MRB_ARGS_NONE() },
	{ "get_dimensions",   gd_getDimensions,  MRB_ARGS_NONE() },
	{ "get_glyph",        gd_getGlyph,       MRB_ARGS_NONE() },
	{ "get_glyph_string", gd_getGlyphString, MRB_ARGS_NONE() },
	{ "get_advance",      gd_getAdvance,     MRB_ARGS_NONE() },
	{ "get_bearing",      gd_getBearing,     MRB_ARGS_NONE() },
	{ "get_bounding_box", gd_getBoundingBox, MRB_ARGS_NONE() },
	{ "get_format",       gd_getFormat,      MRB_ARGS_NONE() },
	{ nullptr, nullptr, 0 }
};

// Equivalent of luaopen_love_font: creates the freetype backend, registers the
// Love::Font module and the Rasterizer / GlyphData types. GlyphData inherits the
// Data methods via the class hierarchy, so the data module must init first.
extern "C" void mrb_love_font_init(mrb_state *mrb)
{
	Font *inst = instance();
	if (inst == nullptr)
		inst = new love::font::freetype::Font();
	inst->retain(); // keep the module alive for the binding's lifetime

	// Name collision: the love.font module and the graphics Font *type* both map
	// to Love::Font. As with the data/thread/joystick module-name vs type-name
	// clashes, one Ruby class doubles as both -- the module functions become
	// CLASS methods on the graphics Font type's class, while graphics Font
	// instances (g.new_font) are objects of that same class with the instance
	// methods registered by the graphics wrapper. So Love::Font.new_*_rasterizer
	// and a font's get_height both work. (See PORTING.md, graphics §B.)
	struct RClass *fontClass = mrbx_gettypeclass(mrb, love::graphics::Font::type);
	for (const MrbReg *r = functions; r != nullptr && r->name != nullptr; r++)
		mrb_define_class_method(mrb, fontClass, r->name, r->func, r->aspec);

	mrbx_register_type(mrb, Rasterizer::type, rasterizerFunctions);
	mrbx_register_type(mrb, GlyphData::type, glyphDataFunctions);
}

} // font
} // love
