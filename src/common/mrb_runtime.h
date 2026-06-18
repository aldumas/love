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

#ifndef LOVE_MRB_RUNTIME_H
#define LOVE_MRB_RUNTIME_H

// LOVE
#include "config.h"
#include "types.h"
#include "Object.h"

// mruby
extern "C" {
	#include <mruby.h>
	#include <mruby/data.h>
	#include <mruby/class.h>
	#include <mruby/string.h>
	#include <mruby/variable.h>
	#include <mruby/array.h>
	#include <mruby/hash.h>
	#include <mruby/error.h>
}

// C++
#include <exception>
#include <string>
#include <vector>
#include <initializer_list>

namespace love
{

class Module;

/**
 * mruby replacement for the Lua-era WrappedModule. Describes a LÖVE module to
 * be exposed as a Ruby module under the top-level `Love` namespace, e.g.
 * Love::Timer. Unlike the Lua version, methods are registered as singleton
 * (module) methods rather than entries in a function table.
 **/
struct MrbReg
{
	// Ruby method name (snake_case), e.g. "get_time". nullptr terminates.
	const char *name;

	// The C function implementing the method.
	mrb_func_t func;

	// Arg spec passed to mrb_define_module_function (use mrbx kwargs helpers
	// inside the function body to parse keyword arguments).
	mrb_aspec aspec;
};

struct WrappedModule
{
	// The native module instance providing the functionality.
	Module *module;

	// The Ruby module name under Love::, without the prefix, e.g. "Timer".
	const char *name;

	// The love::Type of this module.
	love::Type *type;

	// The methods of the module (last element {nullptr, nullptr, 0}).
	const MrbReg *functions;
};

/**
 * --- Keyword-argument parsing -------------------------------------------
 *
 * LÖVE's mruby API uses keyword arguments instead of positional parameters.
 * mrbx_get_kwargs wraps mrb_get_args(":", ...) so wrapper functions can simply
 * name the keywords they expect and read the resulting values. Any keyword the
 * caller omits is left as an mruby `undef` value; use the mrbx_opt* helpers (or
 * mrb_undef_p) to supply defaults.
 *
 * Example:
 *   mrb_value v[5];
 *   mrbx_get_kwargs(mrb, {"mode","x","y","width","height"}, 4, v);
 *   // v[0..3] required, v[4] (height) optional.
 **/
void mrbx_get_kwargs(mrb_state *mrb, std::initializer_list<const char *> keys, mrb_int required, mrb_value *out);

// --- Value conversions (mruby is value-based, not stack-based) -----------

float       mrbx_checkfloat (mrb_state *mrb, mrb_value v);
float       mrbx_optfloat   (mrb_state *mrb, mrb_value v, float def);
int         mrbx_checkint   (mrb_state *mrb, mrb_value v);
int         mrbx_optint     (mrb_state *mrb, mrb_value v, int def);
double      mrbx_checknumber(mrb_state *mrb, mrb_value v);
double      mrbx_optnumber  (mrb_state *mrb, mrb_value v, double def);
bool        mrbx_checkboolean(mrb_state *mrb, mrb_value v);
bool        mrbx_optboolean (mrb_state *mrb, mrb_value v, bool def);
std::string mrbx_checkstring(mrb_state *mrb, mrb_value v);
std::string mrbx_optstring  (mrb_state *mrb, mrb_value v, const std::string &def);

// Push helpers (return an mrb_value from a wrapper function).
mrb_value mrbx_number (mrb_state *mrb, double n);
mrb_value mrbx_integer(mrb_state *mrb, int n);
mrb_value mrbx_boolean(mrb_state *mrb, bool b);
mrb_value mrbx_string (mrb_state *mrb, const std::string &s);

/**
 * The single mruby data type used to wrap every love::Object. The Ruby class
 * carries the concrete love::Type; this data type carries the C++ pointer and
 * handles release() on garbage collection.
 **/
extern const mrb_data_type mrbx_object_data_type;

/**
 * Wraps a love::Object in a fresh Ruby object whose class corresponds to the
 * given love::Type. Retains the object; mruby releases it on GC. Returns the
 * wrapping mrb_value.
 **/
mrb_value mrbx_pushtype(mrb_state *mrb, love::Type &type, love::Object *object);

template <typename T>
mrb_value mrbx_pushtype(mrb_state *mrb, T *object)
{
	return mrbx_pushtype(mrb, T::type, object);
}

/**
 * Extracts the wrapped love::Object from a Ruby value, checking that it is of
 * (or derives from) the expected type. Raises a Ruby TypeError otherwise.
 **/
love::Object *mrbx_checktype(mrb_state *mrb, mrb_value v, const love::Type &type);

template <typename T>
T *mrbx_checktype(mrb_state *mrb, mrb_value v)
{
	return (T *) mrbx_checktype(mrb, v, T::type);
}

/**
 * Registers a Ruby class for the given love::Type (creating it lazily) under
 * the Love namespace, and returns it. Used both for modules-as-objects and for
 * regular object types.
 **/
struct RClass *mrbx_gettypeclass(mrb_state *mrb, const love::Type &type);

/**
 * Registers a LÖVE module as Love::<Name> with its keyword-argument methods.
 * The module instance is retained and stored so wrapper functions can reach it.
 **/
void mrbx_register_module(mrb_state *mrb, const WrappedModule &m);

/**
 * Runs func, translating any C++ love::Exception (or std::exception) into an
 * mruby RuntimeError. Returns true if an exception was caught and an mruby
 * error raised (in which case the caller should return mrb_nil_value()).
 *
 * Mirrors the Lua-era luax_catchexcept, but mruby's exception model lets us
 * raise directly rather than relying on longjmp semantics.
 **/
template <typename T>
bool mrbx_catchexcept(mrb_state *mrb, const T &func)
{
	try
	{
		func();
	}
	catch (const std::exception &e)
	{
		mrb_raise(mrb, mrb_class_get(mrb, "RuntimeError"), e.what());
		return true;
	}
	return false;
}

} // love

#endif // LOVE_MRB_RUNTIME_H
