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
class Variant;

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
 * --- Variant <-> Ruby ----------------------------------------------------
 *
 * LÖVE's Variant is the type-erased value used for data that crosses the
 * scripting boundary outside the normal argument path: event-queue messages,
 * thread channels, etc. These convert between a Variant and an mrb_value.
 *
 * Scalars map directly (nil/bool/number/string). A LOVEOBJECT becomes the
 * wrapped Ruby object (via mrbx_pushtype); a table becomes a Ruby Hash (and a
 * Ruby Hash or Array converts back to a table). Values that can't be stored
 * safely become Variant::UNKNOWN.
 **/
mrb_value mrbx_pushvariant(mrb_state *mrb, const Variant &v);
Variant   mrbx_checkvariant(mrb_state *mrb, mrb_value v);

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
 * Non-raising counterpart of mrbx_checktype: true if v wraps a love::Object
 * that is (or derives from) the given type. Mirrors the Lua-era luax_istype.
 * Use it to disambiguate arguments that accept an object or something else.
 **/
bool mrbx_istype(mrb_state *mrb, mrb_value v, const love::Type &type);

template <typename T>
bool mrbx_istype(mrb_state *mrb, mrb_value v)
{
	return mrbx_istype(mrb, v, T::type);
}

/**
 * Registers a Ruby class for the given love::Type (creating it lazily) under
 * the Love namespace, and returns it. Used both for modules-as-objects and for
 * regular object types.
 **/
struct RClass *mrbx_gettypeclass(mrb_state *mrb, const love::Type &type);

/**
 * Drops the cached type->class entries for a closing mrb_state. Call right
 * before mrb_close on a per-thread state so a later state reusing the same
 * address is never handed a stale RClass from this one.
 **/
void mrbx_forgetstate(mrb_state *mrb);

/**
 * Records a module instance as held by this state's bindings, so mrbx_close_state
 * releases it (once) when the state closes. The caller must already own exactly
 * one reference for this binding -- the `new` from first creation or an
 * `inst->retain()` for a reuse on another VM. mrbx_register_module calls this
 * for the modules it registers; the modules that bypass it (name-collision
 * cases: data/thread/joystick/font/video) call it directly.
 **/
void mrbx_track_module(mrb_state *mrb, love::Object *module);

/**
 * Tears down a love mrb_state: drops this state's cached entries (mrbx_forgetstate),
 * closes the VM (which releases every wrapped game object via its free callback),
 * then releases the module instances this state's bindings held -- in reverse
 * registration order, after the VM's objects are gone. This mirrors the Lua
 * build's lua_close GC, where objects (marked later) finalize before the modules
 * they depend on, and where releasing a module to a zero refcount runs its
 * destructor (tearing down the window/graphics/audio singletons). Use everywhere
 * a love mrb_state is closed, in place of a bare mrb_close.
 **/
void mrbx_close_state(mrb_state *mrb);

/**
 * --- Per-object user data ------------------------------------------------
 *
 * Associates one arbitrary Ruby value with a love::Object, keyed by the C++
 * object so the value round-trips no matter which Ruby wrapper instance fetches
 * it (e.g. set on a Body, read back via fixture.get_body in a callback). The
 * value is GC-protected (mrb_gc_register) while stored, mirroring the Lua-era
 * Reference. This is the mruby backend for the physics Body/Shape/Joint
 * set_user_data/get_user_data methods.
 *
 * mrbx_set_userdata replaces (and unprotects) any previous value; storing nil
 * clears it. mrbx_clear_userdata must be called when the object is destroyed so
 * the value can be collected and a reused address can't return stale data.
 **/
void mrbx_set_userdata(mrb_state *mrb, love::Object *object, mrb_value value);
mrb_value mrbx_get_userdata(mrb_state *mrb, love::Object *object);
void mrbx_clear_userdata(love::Object *object);

/**
 * --- Stored callbacks ----------------------------------------------------
 *
 * GC-protected handles to Ruby callables (Procs) that the engine invokes later
 * — the mruby equivalent of the Lua-era Reference used for the box2d World
 * collision callbacks and contact filter (#phys-callbacks). Each is keyed by an
 * arbitrary stable address the caller owns (the engine callback-holder
 * sub-object), so one engine object can register several. The owning mrb_state
 * is stored alongside the callable, so the engine's invocation site need not
 * thread a VM pointer through; mrbx_get_callback hands both back.
 *
 * mrbx_set_callback replaces (and unprotects) any previous callable for that
 * key; storing a nil/undef callable just clears it. mrbx_get_callback returns
 * false when no callable is stored. mrbx_clear_callback must run when the owner
 * is destroyed so the callable can be collected (and a reused address can't
 * return a stale one).
 **/
void mrbx_set_callback(mrb_state *mrb, const void *key, mrb_value callback);
bool mrbx_get_callback(const void *key, mrb_state **mrb_out, mrb_value *callback_out);
void mrbx_clear_callback(const void *key);

/**
 * Registers a LÖVE module as Love::<Name> with its keyword-argument methods.
 * The instance is tracked (via mrbx_track_module) so mrbx_close_state releases
 * it when the state closes; the caller's init owns the single binding reference
 * (the `new` on first creation or an `inst->retain()` on reuse).
 **/
void mrbx_register_module(mrb_state *mrb, const WrappedModule &m);

/**
 * Registers the Ruby class for an object type (e.g. RandomGenerator) under the
 * Love namespace and defines its instance methods. Each MrbReg func is an
 * instance method receiving the wrapped object as `self`; use
 * mrbx_checktype<T>(mrb, self) inside to recover the C++ pointer.
 * The functions array is terminated by a {nullptr, ...} entry.
 **/
void mrbx_register_type(mrb_state *mrb, const love::Type &type, const MrbReg *functions);

/**
 * Runs func, translating any C++ love::Exception (or std::exception) into an
 * mruby RuntimeError. Returns true if an exception was caught and an mruby
 * error raised (in which case the caller should return mrb_nil_value()).
 *
 * mrb_raise does a longjmp, so it must be called *after* the catch block has
 * exited: raising from inside the handler would skip __cxa_end_catch and leak
 * the in-flight C++ exception object plus its message. We copy the message into
 * thread_local storage (which survives the longjmp without a per-call leak, and
 * preserves long messages such as shader compile logs) and raise once the catch
 * has unwound cleanly.
 **/
template <typename T>
bool mrbx_catchexcept(mrb_state *mrb, const T &func)
{
	static thread_local std::string message;
	bool caught = false;
	try
	{
		func();
	}
	catch (const std::exception &e)
	{
		message = e.what();
		caught = true;
	}
	if (caught)
		mrb_raise(mrb, mrb_class_get(mrb, "RuntimeError"), message.c_str());
	return caught;
}

} // love

#endif // LOVE_MRB_RUNTIME_H
