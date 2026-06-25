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
#include "thread/threads.h"

#include <type_traits>
#include <cstring>
#include <cstdint>
#include <cstddef>
#include <climits>

namespace love
{
namespace data
{

#define instance() (Module::getInstance<DataModule>(Module::M_DATA))

// --- shared helpers ------------------------------------------------------

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

// Raw data pointer as a TT_CPTR value, for native interop (mirrors the Lua
// lightuserdata getPointer). The window module exposes get_pointer the same way.
static mrb_value d_getPointer(mrb_state *mrb, mrb_value self)
{
	Data *t = mrbx_checktype<Data>(mrb, self);
	return mrb_cptr_value(mrb, t->getData());
}

// FFI pointer: a LuaJIT-FFI-only fast path with no mruby analog. The Lua base
// returns nil (the FFI layer overrode it only when the FFI was available); mruby
// has no FFI, so this faithfully always yields nil. See PORTING.md §C.
static mrb_value d_getFFIPointer(mrb_state *mrb, mrb_value self)
{
	(void) self;
	return mrb_nil_value();
}

// Forwards the block + receiver through mrb_protect_error so the mutex is
// released even if the block raises (the C++ stack is not unwound by mruby's
// longjmp-based exceptions, so RAII can't be relied on here).
struct AtomicCtx { mrb_value blk; mrb_value self; };

// perform_atomic { |data| ... } -- runs the block with the Data's mutex held,
// so a read-modify-write on the buffer is atomic. Returns the block's value and
// re-raises any error after unlocking. Mirrors Lua's Data:performAtomic (which
// pcall'd under a love::thread::Lock); the channel module's perform_atomic too.
static mrb_value d_performAtomic(mrb_state *mrb, mrb_value self)
{
	Data *t = mrbx_checktype<Data>(mrb, self);
	mrb_value blk = mrb_nil_value();
	mrb_get_args(mrb, "&", &blk);
	if (mrb_nil_p(blk))
		mrb_raise(mrb, E_ARGUMENT_ERROR, "perform_atomic requires a block");

	love::thread::Mutex *mutex = t->getMutex();
	mutex->lock();

	AtomicCtx ctx = { blk, self };
	mrb_bool error = FALSE;
	mrb_value result = mrb_protect_error(mrb, [](mrb_state *m, void *ud) -> mrb_value {
		AtomicCtx *c = (AtomicCtx *) ud;
		return mrb_yield_argv(m, c->blk, 1, &c->self);
	}, &ctx, &error);

	mutex->unlock();

	if (error)
		mrb_exc_raise(mrb, result);
	return result;
}

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
	{ "get_pointer",     d_getPointer,     MRB_ARGS_NONE() },
	{ "get_ffi_pointer", d_getFFIPointer,  MRB_ARGS_NONE() },
	{ "perform_atomic",  d_performAtomic,  MRB_ARGS_BLOCK() },
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

	if (!mrb_undef_p(v[0]) && mrbx_istype(mrb, v[0], CompressedData::type))
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

// --- binary pack / unpack (#data-pack) -----------------------------------
// Native reimplementation of Lua 5.3's string.pack / string.unpack / packsize
// (libraries/lua53/lstrlib.c). The mruby VM has no string.pack, so the format
// mini-language is ported here, operating on a std::string buffer and an Array
// of Ruby values instead of the Lua stack. mrb_int is 64-bit (matching
// lua_Integer) and mrb_float is double (matching lua_Number), so the size and
// overflow logic carries over unchanged. See PORTING.md §A.

namespace {

const int      PK_NB        = 8;                  // bits per byte
const unsigned PK_MC        = (1u << PK_NB) - 1;  // one-byte mask
const int      PK_SZINT     = (int) sizeof(mrb_int);
const int      PK_MAXINTSIZE = 16;

const union { int dummy; char little; } pk_nativeendian = {1};

// Native alignment probe (mirrors lstrlib's struct cD): offset of the union
// gives the platform's max alignment, the upper bound for the '!' option.
struct PkAlignProbe { char c; union { double d; void *p; mrb_int i; double n; } u; };
const int PK_MAXALIGN = (int) offsetof(struct PkAlignProbe, u);

// Union for byte-faithful serialization of floats (enough room for any type).
union PkFtypes { float f; double d; double n; char buff[5 * sizeof(double)]; };

enum PkOption {
	PK_Kint, PK_Kuint, PK_Kfloat, PK_Kchar,
	PK_Kstring, PK_Kzstr, PK_Kpadding, PK_Kpaddalign, PK_Knop
};

struct PkHeader { mrb_state *mrb; int islittle; int maxalign; };

inline int pk_digit(int c) { return '0' <= c && c <= '9'; }

// Read an integer numeral from 'fmt', or 'df' if there is none.
int pk_getnum(const char **fmt, int df)
{
	if (!pk_digit((unsigned char) **fmt))
		return df;
	int a = 0;
	do {
		a = a * 10 + (*((*fmt)++) - '0');
	} while (pk_digit((unsigned char) **fmt) && a <= (INT_MAX - 9) / 10);
	return a;
}

int pk_getnumlimit(PkHeader *h, const char **fmt, int df)
{
	mrb_state *mrb = h->mrb;
	int sz = pk_getnum(fmt, df);
	if (sz > PK_MAXINTSIZE || sz <= 0)
		mrb_raisef(mrb, E_ARGUMENT_ERROR, "integral size (%d) out of limits [1,%d]", sz, PK_MAXINTSIZE);
	return sz;
}

// Read and classify the next format option; '*size' receives its size.
PkOption pk_getoption(PkHeader *h, const char **fmt, int *size)
{
	mrb_state *mrb = h->mrb;
	int opt = *((*fmt)++);
	*size = 0;
	switch (opt) {
		case 'b': *size = sizeof(char);    return PK_Kint;
		case 'B': *size = sizeof(char);    return PK_Kuint;
		case 'h': *size = sizeof(short);   return PK_Kint;
		case 'H': *size = sizeof(short);   return PK_Kuint;
		case 'l': *size = sizeof(long);    return PK_Kint;
		case 'L': *size = sizeof(long);    return PK_Kuint;
		case 'j': *size = sizeof(mrb_int); return PK_Kint;
		case 'J': *size = sizeof(mrb_int); return PK_Kuint;
		case 'T': *size = sizeof(size_t);  return PK_Kuint;
		case 'f': *size = sizeof(float);   return PK_Kfloat;
		case 'd': *size = sizeof(double);  return PK_Kfloat;
		case 'n': *size = sizeof(double);  return PK_Kfloat;
		case 'i': *size = pk_getnumlimit(h, fmt, sizeof(int));    return PK_Kint;
		case 'I': *size = pk_getnumlimit(h, fmt, sizeof(int));    return PK_Kuint;
		case 's': *size = pk_getnumlimit(h, fmt, sizeof(size_t)); return PK_Kstring;
		case 'c':
			*size = pk_getnum(fmt, -1);
			if (*size == -1)
				mrb_raise(h->mrb, E_ARGUMENT_ERROR, "missing size for format option 'c'");
			return PK_Kchar;
		case 'z': return PK_Kzstr;
		case 'x': *size = 1; return PK_Kpadding;
		case 'X': return PK_Kpaddalign;
		case ' ': break;
		case '<': h->islittle = 1; break;
		case '>': h->islittle = 0; break;
		case '=': h->islittle = pk_nativeendian.little; break;
		case '!': h->maxalign = pk_getnumlimit(h, fmt, PK_MAXALIGN); break;
		default: mrb_raisef(h->mrb, E_ARGUMENT_ERROR, "invalid format option '%c'", opt);
	}
	return PK_Knop;
}

// Read the next option plus its alignment requirement ('*ntoalign').
PkOption pk_getdetails(PkHeader *h, size_t totalsize, const char **fmt, int *psize, int *ntoalign)
{
	mrb_state *mrb = h->mrb;
	PkOption opt = pk_getoption(h, fmt, psize);
	int align = *psize;
	if (opt == PK_Kpaddalign) { // 'X' takes alignment from the following option
		if (**fmt == '\0' || pk_getoption(h, fmt, &align) == PK_Kchar || align == 0)
			mrb_raise(h->mrb, E_ARGUMENT_ERROR, "invalid next option for option 'X'");
	}
	if (align <= 1 || opt == PK_Kchar)
		*ntoalign = 0;
	else {
		if (align > h->maxalign)
			align = h->maxalign;
		if ((align & (align - 1)) != 0)
			mrb_raise(h->mrb, E_ARGUMENT_ERROR, "format asks for alignment not power of 2");
		*ntoalign = (align - (int)(totalsize & (align - 1))) & (align - 1);
	}
	return opt;
}

// Pack integer 'n' as 'size' bytes with the given endianness (sign-extending
// when 'size' exceeds the native integer width and 'neg' is set).
void pk_packint(std::string &b, uint64_t n, int islittle, int size, int neg)
{
	std::vector<char> buff(size);
	buff[islittle ? 0 : size - 1] = (char)(n & PK_MC);
	for (int i = 1; i < size; i++) {
		n >>= PK_NB;
		buff[islittle ? i : size - 1 - i] = (char)(n & PK_MC);
	}
	if (neg && size > PK_SZINT) {
		for (int i = PK_SZINT; i < size; i++)
			buff[islittle ? i : size - 1 - i] = (char) PK_MC;
	}
	b.append(buff.data(), size);
}

// Copy 'size' bytes, reversing them iff the requested endianness differs.
void pk_copywithendian(char *dest, const char *src, int size, int islittle)
{
	if (islittle == pk_nativeendian.little) {
		while (size-- != 0) *(dest++) = *(src++);
	} else {
		dest += size - 1;
		while (size-- != 0) *(dest--) = *(src++);
	}
}

// Unpack a 'size'-byte integer, sign-extending or overflow-checking as needed.
mrb_int pk_unpackint(mrb_state *mrb, const char *str, int islittle, int size, int issigned)
{
	uint64_t res = 0;
	int limit = (size <= PK_SZINT) ? size : PK_SZINT;
	for (int i = limit - 1; i >= 0; i--) {
		res <<= PK_NB;
		res |= (uint64_t)(unsigned char) str[islittle ? i : size - 1 - i];
	}
	if (size < PK_SZINT) {
		if (issigned) {
			uint64_t mask = (uint64_t)1 << (size * PK_NB - 1);
			res = ((res ^ mask) - mask); // sign extension
		}
	} else if (size > PK_SZINT) { // check the unread high bytes
		int mask = (!issigned || (mrb_int) res >= 0) ? 0 : (int) PK_MC;
		for (int i = limit; i < size; i++) {
			if ((unsigned char) str[islittle ? i : size - 1 - i] != mask)
				mrb_raisef(mrb, E_ARGUMENT_ERROR, "%d-byte integer does not fit into Integer", size);
		}
	}
	return (mrb_int) res;
}

// Fetch the next value to pack, erroring if the values Array is exhausted.
mrb_value pk_nextval(mrb_state *mrb, mrb_value values, mrb_int &argi, mrb_int nargs)
{
	if (argi >= nargs)
		mrb_raise(mrb, E_ARGUMENT_ERROR, "too few values to pack for the given format");
	return mrb_ary_ref(mrb, values, argi++);
}

// Pack 'values' per 'fmt' into a freshly built byte buffer.
std::string pk_pack_core(mrb_state *mrb, const char *fmt, mrb_value values)
{
	PkHeader h = { mrb, pk_nativeendian.little, 1 };
	std::string b;
	size_t totalsize = 0;
	mrb_int argi = 0;
	mrb_int nargs = RARRAY_LEN(values);

	while (*fmt != '\0') {
		int size, ntoalign;
		PkOption opt = pk_getdetails(&h, totalsize, &fmt, &size, &ntoalign);
		totalsize += ntoalign + size;
		while (ntoalign-- > 0)
			b.push_back((char) 0x00); // alignment padding

		switch (opt) {
		case PK_Kint:
		case PK_Kuint: {
			mrb_int n = mrb_as_int(mrb, pk_nextval(mrb, values, argi, nargs));
			if (opt == PK_Kint) {
				if (size < PK_SZINT) {
					mrb_int lim = (mrb_int)1 << (size * PK_NB - 1);
					if (!(-lim <= n && n < lim))
						mrb_raise(mrb, E_ARGUMENT_ERROR, "integer overflow");
				}
				pk_packint(b, (uint64_t) n, h.islittle, size, (n < 0));
			} else {
				if (size < PK_SZINT && (uint64_t) n >= ((uint64_t)1 << (size * PK_NB)))
					mrb_raise(mrb, E_ARGUMENT_ERROR, "unsigned overflow");
				pk_packint(b, (uint64_t) n, h.islittle, size, 0);
			}
			break;
		}
		case PK_Kfloat: {
			double n = mrb_as_float(mrb, pk_nextval(mrb, values, argi, nargs));
			PkFtypes u;
			if (size == (int) sizeof(u.f)) u.f = (float) n;
			else if (size == (int) sizeof(u.d)) u.d = (double) n;
			else u.n = n;
			char tmp[sizeof(u.buff)];
			pk_copywithendian(tmp, u.buff, size, h.islittle);
			b.append(tmp, size);
			break;
		}
		case PK_Kchar: {
			mrb_value s = mrb_ensure_string_type(mrb, pk_nextval(mrb, values, argi, nargs));
			size_t len = RSTRING_LEN(s);
			if (len > (size_t) size)
				mrb_raise(mrb, E_ARGUMENT_ERROR, "string longer than given size");
			b.append(RSTRING_PTR(s), len);
			while (len++ < (size_t) size)
				b.push_back((char) 0x00);
			break;
		}
		case PK_Kstring: {
			mrb_value s = mrb_ensure_string_type(mrb, pk_nextval(mrb, values, argi, nargs));
			size_t len = RSTRING_LEN(s);
			if (!(size >= (int) sizeof(size_t) || len < ((size_t)1 << (size * PK_NB))))
				mrb_raise(mrb, E_ARGUMENT_ERROR, "string length does not fit in given size");
			pk_packint(b, (uint64_t) len, h.islittle, size, 0);
			b.append(RSTRING_PTR(s), len);
			totalsize += len;
			break;
		}
		case PK_Kzstr: {
			mrb_value s = mrb_ensure_string_type(mrb, pk_nextval(mrb, values, argi, nargs));
			size_t len = RSTRING_LEN(s);
			if (memchr(RSTRING_PTR(s), '\0', len) != nullptr)
				mrb_raise(mrb, E_ARGUMENT_ERROR, "string contains zeros");
			b.append(RSTRING_PTR(s), len);
			b.push_back('\0');
			totalsize += len + 1;
			break;
		}
		case PK_Kpadding:
			b.push_back((char) 0x00);
			break;
		case PK_Kpaddalign:
		case PK_Knop:
			break;
		}
	}
	return b;
}

// Compute the packed size of 'fmt' (errors on variable-length options).
size_t pk_packsize_core(mrb_state *mrb, const char *fmt)
{
	PkHeader h = { mrb, pk_nativeendian.little, 1 };
	size_t totalsize = 0;
	while (*fmt != '\0') {
		int size, ntoalign;
		PkOption opt = pk_getdetails(&h, totalsize, &fmt, &size, &ntoalign);
		size += ntoalign;
		if (totalsize > (size_t) -1 - (size_t) size)
			mrb_raise(mrb, E_ARGUMENT_ERROR, "format result too large");
		totalsize += size;
		if (opt == PK_Kstring || opt == PK_Kzstr)
			mrb_raise(mrb, E_ARGUMENT_ERROR, "variable-length format");
	}
	return totalsize;
}

// Translate a 1-based (negative = from end) position, like Lua's posrelat.
mrb_int pk_posrelat(mrb_int pos, size_t len)
{
	if (pos >= 0) return pos;
	else if ((uint64_t)(0u - (uint64_t) pos) > len) return 0;
	else return (mrb_int) len + pos + 1;
}

// Unpack values per 'fmt' from 'data'; '*nextpos' receives the 1-based position
// just past the consumed bytes (the trailing value Lua's string.unpack returns).
mrb_value pk_unpack_core(mrb_state *mrb, const char *fmt, const char *data, size_t ld, mrb_int posarg, mrb_int *nextpos)
{
	PkHeader h = { mrb, pk_nativeendian.little, 1 };
	size_t pos = (size_t) pk_posrelat(posarg, ld) - 1;
	if (pos > ld)
		mrb_raise(mrb, E_ARGUMENT_ERROR, "initial position out of string");

	mrb_value arr = mrb_ary_new(mrb);
	while (*fmt != '\0') {
		int size, ntoalign;
		PkOption opt = pk_getdetails(&h, pos, &fmt, &size, &ntoalign);
		if ((size_t) ntoalign + size > ~pos || pos + ntoalign + size > ld)
			mrb_raise(mrb, E_ARGUMENT_ERROR, "data string too short");
		pos += ntoalign;
		switch (opt) {
		case PK_Kint:
		case PK_Kuint:
			mrb_ary_push(mrb, arr, mrb_int_value(mrb, pk_unpackint(mrb, data + pos, h.islittle, size, (opt == PK_Kint))));
			break;
		case PK_Kfloat: {
			PkFtypes u;
			pk_copywithendian(u.buff, data + pos, size, h.islittle);
			double num;
			if (size == (int) sizeof(u.f)) num = (double) u.f;
			else if (size == (int) sizeof(u.d)) num = (double) u.d;
			else num = u.n;
			mrb_ary_push(mrb, arr, mrb_float_value(mrb, num));
			break;
		}
		case PK_Kchar:
			mrb_ary_push(mrb, arr, mrb_str_new(mrb, data + pos, size));
			break;
		case PK_Kstring: {
			size_t len = (size_t) pk_unpackint(mrb, data + pos, h.islittle, size, 0);
			if (pos + len + size > ld)
				mrb_raise(mrb, E_ARGUMENT_ERROR, "data string too short");
			mrb_ary_push(mrb, arr, mrb_str_new(mrb, data + pos + size, len));
			pos += len;
			break;
		}
		case PK_Kzstr: {
			size_t len = strlen(data + pos);
			mrb_ary_push(mrb, arr, mrb_str_new(mrb, data + pos, len));
			pos += len + 1;
			break;
		}
		case PK_Kpaddalign:
		case PK_Kpadding:
		case PK_Knop:
			break;
		}
		pos += size;
	}
	*nextpos = (mrb_int) pos + 1;
	return arr;
}

} // anonymous namespace

// Love::Data.pack(format:, values:, container:, data:, offset:)
//   Packs the Array `values:` per the format string. Two output forms:
//   - into an existing ByteData: pass `data:` (+ optional `offset:`, default 0);
//     returns that ByteData.
//   - otherwise `container:` "string" (default, returns a String) or "data"
//     (returns a new ByteData).
static mrb_value w_pack(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[5]; // format, values, container, data, offset
	mrbx_get_kwargs(mrb, {"format", "values", "container", "data", "offset"}, 1, v);

	std::string fmt = mrbx_checkstring(mrb, v[0]);

	mrb_value values = v[1];
	if (mrb_undef_p(values) || mrb_nil_p(values))
		values = mrb_ary_new(mrb);
	else if (!mrb_array_p(values))
		mrb_raise(mrb, E_ARGUMENT_ERROR, "`values:` must be an Array");

	std::string packed = pk_pack_core(mrb, fmt.c_str(), values);

	// Form 1: pack into an existing ByteData at a byte offset.
	if (!mrb_undef_p(v[3]) && !mrb_nil_p(v[3]))
	{
		ByteData *d = mrbx_checktype<ByteData>(mrb, v[3]);
		int64 offset = mrbx_optint(mrb, v[4], 0);
		if (offset < 0 || offset + (int64) packed.size() > (int64) d->getSize())
			mrb_raise(mrb, E_ARGUMENT_ERROR, "The given byte offset and pack format parameters do not fit within the ByteData's size.");
		memcpy((uint8 *) d->getData() + offset, packed.data(), packed.size());
		return v[3];
	}

	// Form 2: container "string" (default) or "data".
	ContainerType ctype = getcontainer(mrb, v[2]);
	if (ctype == CONTAINER_DATA)
		return newbytedata_copy(mrb, packed.data(), packed.size());
	return mrb_str_new(mrb, packed.data(), packed.size());
}

// Love::Data.unpack(format:, data:/string:, offset:)
//   Reads bytes from `data:` (a Data) or `string:` (a String) per the format,
//   starting at the 1-based `offset:` (default 1; negative counts from the end).
//   Returns a Hash { values: [...], offset: <1-based position past the data> }.
static mrb_value w_unpack(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[4]; // format, data, string, offset
	mrbx_get_kwargs(mrb, {"format", "data", "string", "offset"}, 1, v);

	std::string fmt = mrbx_checkstring(mrb, v[0]);

	mrb_value hold = mrb_nil_value();
	size_t datasize = 0;
	const char *data = getbytes(mrb, v[1], v[2], hold, datasize);

	mrb_int posarg = mrb_undef_p(v[3]) ? 1 : mrb_as_int(mrb, v[3]);
	mrb_int nextpos = 0;
	mrb_value arr = pk_unpack_core(mrb, fmt.c_str(), data, datasize, posarg, &nextpos);

	mrb_value result = mrb_hash_new_capa(mrb, 2);
	mrb_hash_set(mrb, result, mrb_symbol_value(mrb_intern_lit(mrb, "values")), arr);
	mrb_hash_set(mrb, result, mrb_symbol_value(mrb_intern_lit(mrb, "offset")), mrb_int_value(mrb, nextpos));
	return result;
}

// Love::Data.get_packed_size(format:) -> Integer (errors on variable-length).
static mrb_value w_getPackedSize(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[1]; // format
	mrbx_get_kwargs(mrb, {"format"}, 1, v);
	std::string fmt = mrbx_checkstring(mrb, v[0]);
	return mrb_int_value(mrb, (mrb_int) pk_packsize_core(mrb, fmt.c_str()));
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
	{ "pack",            w_pack,          MRB_ARGS_KEY(5, 0) },
	{ "unpack",          w_unpack,        MRB_ARGS_KEY(4, 0) },
	{ "get_packed_size", w_getPackedSize, MRB_ARGS_KEY(1, 0) },
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
	else
		inst->retain();
	mrbx_track_module(mrb, inst); // released on state close (mrbx_close_state)

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
