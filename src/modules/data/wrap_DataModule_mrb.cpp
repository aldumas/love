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

// mruby port of wrap_DataModule.cpp + wrap_Data.cpp + wrap_ByteData.cpp +
// wrap_DataView.cpp + wrap_CompressedData.cpp.
//
// Naming: the Lua `love.data` module's name ("Data") collides with the `Data`
// object type — a Ruby module and class can't share a name under `Love::`. We
// resolve it the idiomatic Ruby way: the module-level functions are CLASS
// methods on the `Love::Data` class, and the Data base instance methods are
// instance methods on the same class. Call syntax is unchanged from every other
// module, e.g. `Love::Data.compress(...)`, `Love::Data.new_byte_data(size: 16)`.
//
// Subclasses (ByteData, DataView, CompressedData — and later ImageData,
// SoundData) inherit the Data instance methods through the Ruby class hierarchy
// the runtime now mirrors from love::Type, so the base methods are registered
// once here. Keyword args throughout; multi-value reads return an Array.
//
// Container type: methods that can yield either a Ruby String or a Data object
// take `container: "string"` (default) or `container: "data"`, matching the Lua
// API's leading container-type argument.

#include "common/config.h"
#include "common/mrb_runtime.h"
#include "common/Data.h"
#include "DataModule.h"

#include <type_traits>

namespace love
{
namespace data
{

#define instance() (Module::getInstance<DataModule>(Module::M_DATA))

// --- shared helpers ------------------------------------------------------

// Non-raising "is this Ruby value a wrapped love::Object of (a subtype of) the
// given type?" — for the overloaded inputs that accept Data-or-something-else.
static bool mrbx_is(mrb_state *mrb, mrb_value v, const love::Type &type)
{
	if (mrb_data_check_get_ptr(mrb, v, &mrbx_object_data_type) == nullptr)
		return false;
	return mrb_obj_is_kind_of(mrb, v, mrbx_gettypeclass(mrb, type));
}

static ContainerType getcontainer(mrb_state *mrb, mrb_value v)
{
	std::string s = mrb_undef_p(v) ? std::string("string") : mrbx_checkstring(mrb, v);
	ContainerType ctype = CONTAINER_STRING;
	if (!getConstant(s.c_str(), ctype))
		mrb_raisef(mrb, E_ARGUMENT_ERROR, "invalid container type: %s", s.c_str());
	return ctype;
}

// Reads raw bytes from either `data:` (a Data object) or `string:` (a String).
// Exactly one must be supplied; `hold` keeps a converted String alive.
static const char *getbytes(mrb_state *mrb, mrb_value vdata, mrb_value vstring, mrb_value &hold, size_t &size)
{
	if (!mrb_undef_p(vdata) && !mrb_nil_p(vdata))
	{
		Data *d = mrbx_checktype<Data>(mrb, vdata);
		size = d->getSize();
		return (const char *) d->getData();
	}
	if (!mrb_undef_p(vstring) && !mrb_nil_p(vstring))
	{
		hold = mrb_ensure_string_type(mrb, vstring);
		size = RSTRING_LEN(hold);
		return RSTRING_PTR(hold);
	}
	mrb_raise(mrb, E_ARGUMENT_ERROR, "expected `data:` or `string:`");
	return nullptr;
}

// Wraps freshly-produced bytes as a ByteData (copying), releasing our ref.
static mrb_value newbytedata_copy(mrb_state *mrb, const char *bytes, size_t size)
{
	ByteData *d = nullptr;
	mrbx_catchexcept(mrb, [&]() { d = instance()->newByteData(bytes, size); });
	mrb_value r = mrbx_pushtype(mrb, d);
	if (d) d->release();
	return r;
}

// Wraps freshly-produced bytes as a ByteData taking ownership (frees on error).
static mrb_value newbytedata_own(mrb_state *mrb, char *bytes, size_t size)
{
	ByteData *d = nullptr;
	if (!mrbx_catchexcept(mrb, [&]() { d = instance()->newByteData((void *) bytes, size, true); }))
	{
		mrb_value r = mrbx_pushtype(mrb, d);
		if (d) d->release();
		return r;
	}
	delete[] bytes;
	return mrb_nil_value();
}

// --- Data base instance methods (inherited by every Data subtype) --------

static mrb_value d_getString(mrb_state *mrb, mrb_value self)
{
	Data *t = mrbx_checktype<Data>(mrb, self);
	mrb_value v[2];
	mrbx_get_kwargs(mrb, {"offset", "size"}, 0, v);

	int64 offset = mrbx_optint(mrb, v[0], 0);
	int64 size = mrb_undef_p(v[1]) ? ((int64) t->getSize() - offset) : (int64) mrbx_checkint(mrb, v[1]);

	if (size <= 0)
		mrb_raise(mrb, E_ARGUMENT_ERROR, "Invalid size parameter (must be greater than 0)");
	if (offset < 0 || offset + size > (int64) t->getSize())
		mrb_raise(mrb, E_ARGUMENT_ERROR, "The given offset and size parameters don't fit within the Data's size.");

	return mrb_str_new(mrb, (const char *) t->getData() + offset, (size_t) size);
}

static mrb_value d_getSize(mrb_state *mrb, mrb_value self)
{
	return mrbx_integer(mrb, (int) mrbx_checktype<Data>(mrb, self)->getSize());
}

template <typename T>
static mrb_value d_getT(mrb_state *mrb, mrb_value self)
{
	Data *t = mrbx_checktype<Data>(mrb, self);
	mrb_value v[2];
	mrbx_get_kwargs(mrb, {"offset", "count"}, 1, v);

	int64 offset = (int64) mrbx_checkint(mrb, v[0]);
	int count = mrbx_optint(mrb, v[1], 1);

	if (count <= 0)
		mrb_raise(mrb, E_ARGUMENT_ERROR, "Invalid count parameter (must be greater than 0)");
	if (offset < 0 || offset + (int64)(sizeof(T) * count) > (int64) t->getSize())
		mrb_raise(mrb, E_ARGUMENT_ERROR, "The given offset and count parameters don't fit within the Data's size.");

	auto data = (const T *)((uint8 *) t->getData() + offset);

	// Integer accessors read back as Integer, float/double as Float.
	auto push = [&](T x) -> mrb_value {
		if (std::is_integral<T>::value)
			return mrb_fixnum_value((mrb_int) x);
		return mrbx_number(mrb, (double) x);
	};

	// A single value reads back as a scalar; multiple as an Array.
	if (count == 1)
		return push(data[0]);

	mrb_value arr = mrb_ary_new_capa(mrb, count);
	for (int i = 0; i < count; i++)
		mrb_ary_push(mrb, arr, push(data[i]));
	return arr;
}

static mrb_value d_getFloat (mrb_state *mrb, mrb_value self) { return d_getT<float> (mrb, self); }
static mrb_value d_getDouble(mrb_state *mrb, mrb_value self) { return d_getT<double>(mrb, self); }
static mrb_value d_getInt8  (mrb_state *mrb, mrb_value self) { return d_getT<int8>  (mrb, self); }
static mrb_value d_getUInt8 (mrb_state *mrb, mrb_value self) { return d_getT<uint8> (mrb, self); }
static mrb_value d_getInt16 (mrb_state *mrb, mrb_value self) { return d_getT<int16> (mrb, self); }
static mrb_value d_getUInt16(mrb_state *mrb, mrb_value self) { return d_getT<uint16>(mrb, self); }
static mrb_value d_getInt32 (mrb_state *mrb, mrb_value self) { return d_getT<int32> (mrb, self); }
static mrb_value d_getUInt32(mrb_state *mrb, mrb_value self) { return d_getT<uint32>(mrb, self); }

static const MrbReg dataInstanceFunctions[] =
{
	{ "get_string", d_getString, MRB_ARGS_KEY(2, 0) },
	{ "get_size",   d_getSize,   MRB_ARGS_NONE() },
	{ "get_float",  d_getFloat,  MRB_ARGS_KEY(2, 0) },
	{ "get_double", d_getDouble, MRB_ARGS_KEY(2, 0) },
	{ "get_int8",   d_getInt8,   MRB_ARGS_KEY(2, 0) },
	{ "get_uint8",  d_getUInt8,  MRB_ARGS_KEY(2, 0) },
	{ "get_int16",  d_getInt16,  MRB_ARGS_KEY(2, 0) },
	{ "get_uint16", d_getUInt16, MRB_ARGS_KEY(2, 0) },
	{ "get_int32",  d_getInt32,  MRB_ARGS_KEY(2, 0) },
	{ "get_uint32", d_getUInt32, MRB_ARGS_KEY(2, 0) },
	// TODO(mruby) #data-ffi-atomic: Data#get_pointer / #get_ffi_pointer (raw/FFI
	// pointers) and #perform_atomic (mutex + block) not exposed. PORTING.md §A
	{ nullptr, nullptr, 0 }
};

// --- ByteData instance methods -------------------------------------------

static mrb_value bd_clone(mrb_state *mrb, mrb_value self)
{
	ByteData *c = nullptr;
	ByteData *t = mrbx_checktype<ByteData>(mrb, self);
	mrbx_catchexcept(mrb, [&]() { c = t->clone(); });
	mrb_value r = mrbx_pushtype(mrb, c);
	if (c) c->release();
	return r;
}

static mrb_value bd_setString(mrb_state *mrb, mrb_value self)
{
	Data *t = mrbx_checktype<Data>(mrb, self);
	mrb_value v[2];
	mrbx_get_kwargs(mrb, {"string", "offset"}, 1, v);

	mrb_value s = mrb_ensure_string_type(mrb, v[0]);
	size_t size = RSTRING_LEN(s);
	int64 offset = mrbx_optint(mrb, v[1], 0);

	if (size > t->getSize())
		size = t->getSize();
	if (size == 0)
		return self;
	if (offset < 0 || offset + (int64) size > (int64) t->getSize())
		mrb_raise(mrb, E_ARGUMENT_ERROR, "The given string offset and size don't fit within the Data's size.");

	memcpy((char *) t->getData() + (size_t) offset, RSTRING_PTR(s), size);
	return self;
}

template <typename T>
static mrb_value bd_setT(mrb_state *mrb, mrb_value self)
{
	ByteData *t = mrbx_checktype<ByteData>(mrb, self);
	mrb_value v[2];
	mrbx_get_kwargs(mrb, {"offset", "values"}, 2, v);

	int64 offset = (int64) mrbx_checkint(mrb, v[0]);
	mrb_value vals = v[1];
	bool isarray = mrb_array_p(vals);
	int n = isarray ? (int) RARRAY_LEN(vals) : 1;
	if (n < 1) n = 1;

	if (offset < 0 || offset + (int64)(sizeof(T) * n) > (int64) t->getSize())
		mrb_raise(mrb, E_ARGUMENT_ERROR, "The given offset and value parameters don't fit within the Data's size.");

	auto data = (T *)((uint8 *) t->getData() + offset);

	if (isarray)
	{
		for (int i = 0; i < n; i++)
			data[i] = (T) mrbx_checknumber(mrb, mrb_ary_ref(mrb, vals, i));
	}
	else
		data[0] = (T) mrbx_checknumber(mrb, vals);

	return self;
}

static mrb_value bd_setFloat (mrb_state *mrb, mrb_value self) { return bd_setT<float> (mrb, self); }
static mrb_value bd_setDouble(mrb_state *mrb, mrb_value self) { return bd_setT<double>(mrb, self); }
static mrb_value bd_setInt8  (mrb_state *mrb, mrb_value self) { return bd_setT<int8>  (mrb, self); }
static mrb_value bd_setUInt8 (mrb_state *mrb, mrb_value self) { return bd_setT<uint8> (mrb, self); }
static mrb_value bd_setInt16 (mrb_state *mrb, mrb_value self) { return bd_setT<int16> (mrb, self); }
static mrb_value bd_setUInt16(mrb_state *mrb, mrb_value self) { return bd_setT<uint16>(mrb, self); }
static mrb_value bd_setInt32 (mrb_state *mrb, mrb_value self) { return bd_setT<int32> (mrb, self); }
static mrb_value bd_setUInt32(mrb_state *mrb, mrb_value self) { return bd_setT<uint32>(mrb, self); }

static const MrbReg byteDataFunctions[] =
{
	{ "clone",      bd_clone,     MRB_ARGS_NONE() },
	{ "set_string", bd_setString, MRB_ARGS_KEY(2, 0) },
	{ "set_float",  bd_setFloat,  MRB_ARGS_KEY(2, 0) },
	{ "set_double", bd_setDouble, MRB_ARGS_KEY(2, 0) },
	{ "set_int8",   bd_setInt8,   MRB_ARGS_KEY(2, 0) },
	{ "set_uint8",  bd_setUInt8,  MRB_ARGS_KEY(2, 0) },
	{ "set_int16",  bd_setInt16,  MRB_ARGS_KEY(2, 0) },
	{ "set_uint16", bd_setUInt16, MRB_ARGS_KEY(2, 0) },
	{ "set_int32",  bd_setInt32,  MRB_ARGS_KEY(2, 0) },
	{ "set_uint32", bd_setUInt32, MRB_ARGS_KEY(2, 0) },
	{ nullptr, nullptr, 0 }
};

// --- DataView instance methods -------------------------------------------

static mrb_value dv_clone(mrb_state *mrb, mrb_value self)
{
	DataView *c = nullptr;
	DataView *t = mrbx_checktype<DataView>(mrb, self);
	mrbx_catchexcept(mrb, [&]() { c = t->clone(); });
	mrb_value r = mrbx_pushtype(mrb, c);
	if (c) c->release();
	return r;
}

static const MrbReg dataViewFunctions[] =
{
	{ "clone", dv_clone, MRB_ARGS_NONE() },
	{ nullptr, nullptr, 0 }
};

// --- CompressedData instance methods -------------------------------------

static mrb_value cd_clone(mrb_state *mrb, mrb_value self)
{
	CompressedData *c = nullptr;
	CompressedData *t = mrbx_checktype<CompressedData>(mrb, self);
	mrbx_catchexcept(mrb, [&]() { c = t->clone(); });
	mrb_value r = mrbx_pushtype(mrb, c);
	if (c) c->release();
	return r;
}

static mrb_value cd_getFormat(mrb_state *mrb, mrb_value self)
{
	CompressedData *t = mrbx_checktype<CompressedData>(mrb, self);
	const char *fname = nullptr;
	if (!Compressor::getConstant(t->getFormat(), fname))
		mrb_raise(mrb, E_RUNTIME_ERROR, "Unknown compressed data format.");
	return mrbx_string(mrb, fname);
}

static const MrbReg compressedDataFunctions[] =
{
	{ "clone",      cd_clone,     MRB_ARGS_NONE() },
	{ "get_format", cd_getFormat, MRB_ARGS_NONE() },
	{ nullptr, nullptr, 0 }
};

// --- Module-level functions (class methods on Love::Data) ----------------

static mrb_value w_newDataView(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[3];
	mrbx_get_kwargs(mrb, {"data", "offset", "size"}, 2, v);

	Data *data = mrbx_checktype<Data>(mrb, v[0]);
	int64 offset = (int64) mrbx_checkint(mrb, v[1]);
	int64 size = mrb_undef_p(v[2]) ? ((int64) data->getSize() - offset) : (int64) mrbx_checkint(mrb, v[2]);

	if (offset < 0 || size < 0)
		mrb_raise(mrb, E_ARGUMENT_ERROR, "DataView offset and size must not be negative.");

	DataView *d = nullptr;
	mrbx_catchexcept(mrb, [&]() { d = instance()->newDataView(data, (size_t) offset, (size_t) size); });
	mrb_value r = mrbx_pushtype(mrb, d);
	if (d) d->release();
	return r;
}

static mrb_value w_newByteData(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[4]; // size, string, data, offset
	mrbx_get_kwargs(mrb, {"size", "string", "data", "offset"}, 0, v);

	ByteData *d = nullptr;

	if (!mrb_undef_p(v[2])) // from a Data slice
	{
		Data *data = mrbx_checktype<Data>(mrb, v[2]);
		int64 offset = mrbx_optint(mrb, v[3], 0);
		int64 size = mrb_undef_p(v[0]) ? ((int64) data->getSize() - offset) : (int64) mrbx_checkint(mrb, v[0]);

		if (offset < 0)
			mrb_raise(mrb, E_ARGUMENT_ERROR, "Offset argument must not be negative.");
		if (size <= 0)
			mrb_raise(mrb, E_ARGUMENT_ERROR, "Size argument must be greater than zero.");
		if ((size_t)(offset + size) > data->getSize())
			mrb_raise(mrb, E_ARGUMENT_ERROR, "Offset and size arguments must fit within the given Data's size.");

		const char *bytes = (const char *) data->getData() + offset;
		mrbx_catchexcept(mrb, [&]() { d = instance()->newByteData(bytes, (size_t) size); });
	}
	else if (!mrb_undef_p(v[1])) // from a String
	{
		mrb_value s = mrb_ensure_string_type(mrb, v[1]);
		mrbx_catchexcept(mrb, [&]() { d = instance()->newByteData(RSTRING_PTR(s), (size_t) RSTRING_LEN(s)); });
	}
	else if (!mrb_undef_p(v[0])) // empty, given a size
	{
		int size = mrbx_checkint(mrb, v[0]);
		if (size <= 0)
			mrb_raise(mrb, E_ARGUMENT_ERROR, "Data size must be a positive number.");
		mrbx_catchexcept(mrb, [&]() { d = instance()->newByteData((size_t) size); });
	}
	else
		mrb_raise(mrb, E_ARGUMENT_ERROR, "expected one of `size:`, `string:`, or `data:`");

	mrb_value r = mrbx_pushtype(mrb, d);
	if (d) d->release();
	return r;
}

static mrb_value w_compress(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[5]; // format, data, string, container, level
	mrbx_get_kwargs(mrb, {"format", "data", "string", "container", "level"}, 1, v);

	std::string fstr = mrbx_checkstring(mrb, v[0]);
	Compressor::Format format = Compressor::FORMAT_LZ4;
	if (!Compressor::getConstant(fstr.c_str(), format))
		mrb_raisef(mrb, E_ARGUMENT_ERROR, "invalid compressed data format: %s", fstr.c_str());

	ContainerType ctype = getcontainer(mrb, v[3]);
	int level = mrbx_optint(mrb, v[4], -1);

	mrb_value hold = mrb_nil_value();
	size_t rawsize = 0;
	const char *raw = getbytes(mrb, v[1], v[2], hold, rawsize);

	CompressedData *cdata = nullptr;
	mrbx_catchexcept(mrb, [&]() { cdata = compress(format, raw, rawsize, level); });

	mrb_value r;
	if (ctype == CONTAINER_DATA)
		r = mrbx_pushtype(mrb, cdata);
	else
		r = mrb_str_new(mrb, (const char *) cdata->getData(), cdata->getSize());

	if (cdata) cdata->release();
	return r;
}

static mrb_value w_decompress(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[4]; // data, string, format, container
	mrbx_get_kwargs(mrb, {"data", "string", "format", "container"}, 0, v);

	ContainerType ctype = getcontainer(mrb, v[3]);

	char *rawbytes = nullptr;
	size_t rawsize = 0;

	if (!mrb_undef_p(v[0]) && mrbx_is(mrb, v[0], CompressedData::type))
	{
		CompressedData *data = mrbx_checktype<CompressedData>(mrb, v[0]);
		rawsize = data->getDecompressedSize();
		mrbx_catchexcept(mrb, [&]() { rawbytes = decompress(data, rawsize); });
	}
	else
	{
		std::string fstr = mrbx_checkstring(mrb, v[2]);
		Compressor::Format format = Compressor::FORMAT_LZ4;
		if (!Compressor::getConstant(fstr.c_str(), format))
			mrb_raisef(mrb, E_ARGUMENT_ERROR, "invalid compressed data format: %s", fstr.c_str());

		mrb_value hold = mrb_nil_value();
		size_t csize = 0;
		const char *cbytes = getbytes(mrb, v[0], v[1], hold, csize);
		mrbx_catchexcept(mrb, [&]() { rawbytes = decompress(format, cbytes, csize, rawsize); });
	}

	if (ctype == CONTAINER_DATA)
		return newbytedata_own(mrb, rawbytes, rawsize);

	mrb_value r = mrb_str_new(mrb, rawbytes, rawsize);
	delete[] rawbytes;
	return r;
}

static mrb_value w_encode(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[5]; // format, data, string, container, line_length
	mrbx_get_kwargs(mrb, {"format", "data", "string", "container", "line_length"}, 1, v);

	std::string fstr = mrbx_checkstring(mrb, v[0]);
	EncodeFormat format;
	if (!getConstant(fstr.c_str(), format))
		mrb_raisef(mrb, E_ARGUMENT_ERROR, "invalid encode format: %s", fstr.c_str());

	ContainerType ctype = getcontainer(mrb, v[3]);
	size_t linelen = (size_t) mrbx_optint(mrb, v[4], 0);

	mrb_value hold = mrb_nil_value();
	size_t srclen = 0;
	const char *src = getbytes(mrb, v[1], v[2], hold, srclen);

	size_t dstlen = 0;
	char *dst = nullptr;
	mrbx_catchexcept(mrb, [&]() { dst = encode(format, src, srclen, dstlen, linelen); });

	if (ctype == CONTAINER_DATA)
		return dst != nullptr ? newbytedata_own(mrb, dst, dstlen) : newbytedata_copy(mrb, "", 0);

	mrb_value r = dst != nullptr ? mrb_str_new(mrb, dst, dstlen) : mrb_str_new(mrb, "", 0);
	delete[] dst;
	return r;
}

static mrb_value w_decode(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[4]; // format, data, string, container
	mrbx_get_kwargs(mrb, {"format", "data", "string", "container"}, 1, v);

	std::string fstr = mrbx_checkstring(mrb, v[0]);
	EncodeFormat format;
	if (!getConstant(fstr.c_str(), format))
		mrb_raisef(mrb, E_ARGUMENT_ERROR, "invalid decode format: %s", fstr.c_str());

	ContainerType ctype = getcontainer(mrb, v[3]);

	mrb_value hold = mrb_nil_value();
	size_t srclen = 0;
	const char *src = getbytes(mrb, v[1], v[2], hold, srclen);

	size_t dstlen = 0;
	char *dst = nullptr;
	mrbx_catchexcept(mrb, [&]() { dst = decode(format, src, srclen, dstlen); });

	if (ctype == CONTAINER_DATA)
		return dst != nullptr ? newbytedata_own(mrb, dst, dstlen) : newbytedata_copy(mrb, "", 0);

	mrb_value r = dst != nullptr ? mrb_str_new(mrb, dst, dstlen) : mrb_str_new(mrb, "", 0);
	delete[] dst;
	return r;
}

static mrb_value w_hash(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[4]; // function, data, string, container
	mrbx_get_kwargs(mrb, {"function", "data", "string", "container"}, 1, v);

	std::string fstr = mrbx_checkstring(mrb, v[0]);
	HashFunction::Function function;
	if (!HashFunction::getConstant(fstr.c_str(), function))
		mrb_raisef(mrb, E_ARGUMENT_ERROR, "invalid hash function: %s", fstr.c_str());

	ContainerType ctype = getcontainer(mrb, v[3]);

	mrb_value hold = mrb_nil_value();
	size_t rawsize = 0;
	const char *raw = getbytes(mrb, v[1], v[2], hold, rawsize);

	HashFunction::Value hashvalue;
	mrbx_catchexcept(mrb, [&]() { love::data::hash(function, raw, rawsize, hashvalue); });

	if (ctype == CONTAINER_DATA)
		return newbytedata_copy(mrb, hashvalue.data, hashvalue.size);

	return mrb_str_new(mrb, hashvalue.data, hashvalue.size);
}

static const MrbReg moduleFunctions[] =
{
	{ "new_data_view", w_newDataView, MRB_ARGS_KEY(3, 0) },
	{ "new_byte_data", w_newByteData, MRB_ARGS_KEY(4, 0) },
	{ "compress",      w_compress,    MRB_ARGS_KEY(5, 0) },
	{ "decompress",    w_decompress,  MRB_ARGS_KEY(4, 0) },
	{ "encode",        w_encode,      MRB_ARGS_KEY(5, 0) },
	{ "decode",        w_decode,      MRB_ARGS_KEY(4, 0) },
	{ "hash",          w_hash,        MRB_ARGS_KEY(4, 0) },
	// TODO(mruby) #data-pack: pack / unpack / get_packed_size depend on Lua
	// 5.3's lstrlib (string.pack); deferred under the mruby port. PORTING.md §A
	{ nullptr, nullptr, 0 }
};

static void defineMethods(mrb_state *mrb, struct RClass *cls, const MrbReg *fns)
{
	for (const MrbReg *r = fns; r != nullptr && r->name != nullptr; r++)
		mrb_define_method(mrb, cls, r->name, r->func, r->aspec);
}

// Equivalent of luaopen_love_data. The module's functions live as class methods
// on Love::Data (see the file header for why), the Data base instance methods on
// the same class, and each subtype's own methods on its class — inheriting the
// base methods through the runtime's love::Type-mirrored class hierarchy.
extern "C" void mrb_love_data_init(mrb_state *mrb)
{
	DataModule *inst = instance();
	if (inst == nullptr)
		inst = new DataModule();
	inst->retain(); // keep the instance alive for the binding's lifetime

	struct RClass *dataClass = mrbx_gettypeclass(mrb, love::Data::type);

	defineMethods(mrb, dataClass, dataInstanceFunctions);
	defineMethods(mrb, mrbx_gettypeclass(mrb, ByteData::type), byteDataFunctions);
	defineMethods(mrb, mrbx_gettypeclass(mrb, DataView::type), dataViewFunctions);
	defineMethods(mrb, mrbx_gettypeclass(mrb, CompressedData::type), compressedDataFunctions);

	for (const MrbReg *r = moduleFunctions; r != nullptr && r->name != nullptr; r++)
		mrb_define_class_method(mrb, dataClass, r->name, r->func, r->aspec);
}

} // data
} // love
