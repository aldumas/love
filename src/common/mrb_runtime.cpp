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

#include "mrb_runtime.h"
#include "Module.h"
#include "Variant.h"

#include <map>
#include <unordered_map>
#include <utility>
#include <mutex>

namespace love
{

// --- Keyword arguments ---------------------------------------------------

void mrbx_get_kwargs(mrb_state *mrb, std::initializer_list<const char *> keys, mrb_int required, mrb_value *out)
{
	const mrb_int n = (mrb_int) keys.size();

	// mrb_kwargs wants a contiguous array of interned symbols.
	std::vector<mrb_sym> syms;
	syms.reserve(n);
	for (const char *k : keys)
		syms.push_back(mrb_intern_cstr(mrb, k));

	const mrb_kwargs kw = { (uint32_t) n, (uint32_t) required, syms.data(), out, nullptr };
	mrb_get_args(mrb, ":", &kw);
}

// --- Value conversions ---------------------------------------------------

float mrbx_checkfloat(mrb_state *mrb, mrb_value v)
{
	return (float) mrb_as_float(mrb, v);
}

float mrbx_optfloat(mrb_state *mrb, mrb_value v, float def)
{
	return mrb_undef_p(v) ? def : (float) mrb_as_float(mrb, v);
}

int mrbx_checkint(mrb_state *mrb, mrb_value v)
{
	return (int) mrb_as_int(mrb, v);
}

int mrbx_optint(mrb_state *mrb, mrb_value v, int def)
{
	return mrb_undef_p(v) ? def : (int) mrb_as_int(mrb, v);
}

double mrbx_checknumber(mrb_state *mrb, mrb_value v)
{
	return mrb_as_float(mrb, v);
}

double mrbx_optnumber(mrb_state *mrb, mrb_value v, double def)
{
	return mrb_undef_p(v) ? def : mrb_as_float(mrb, v);
}

bool mrbx_checkboolean(mrb_state *mrb, mrb_value v)
{
	(void) mrb;
	return mrb_test(v);
}

bool mrbx_optboolean(mrb_state *mrb, mrb_value v, bool def)
{
	(void) mrb;
	return mrb_undef_p(v) ? def : mrb_test(v);
}

std::string mrbx_checkstring(mrb_state *mrb, mrb_value v)
{
	mrb_value s = mrb_ensure_string_type(mrb, v);
	return std::string(RSTRING_PTR(s), RSTRING_LEN(s));
}

std::string mrbx_optstring(mrb_state *mrb, mrb_value v, const std::string &def)
{
	return mrb_undef_p(v) ? def : mrbx_checkstring(mrb, v);
}

mrb_value mrbx_number(mrb_state *mrb, double n)
{
	return mrb_float_value(mrb, n);
}

mrb_value mrbx_integer(mrb_state *mrb, int n)
{
	return mrb_fixnum_value(n);
}

mrb_value mrbx_boolean(mrb_state *mrb, bool b)
{
	(void) mrb;
	return mrb_bool_value(b);
}

mrb_value mrbx_string(mrb_state *mrb, const std::string &s)
{
	return mrb_str_new(mrb, s.data(), s.size());
}

// --- Object <-> Ruby binding ---------------------------------------------

// The registries below (objectWrappers, moduleInstances, typeClasses, userData,
// callbacks) are process-global but keyed by mrb_state, and the thread module
// runs each Love::Thread in its own mrb_state on its own OS thread. So several
// VMs boot (registering types, minting wrappers, tracking modules), run, and
// tear down (mrbx_forgetstate / mrbx_close_state) concurrently, all touching
// these same std::maps. Without a lock those concurrent map mutations are a data
// race that corrupts the tree (TSan-confirmed; it segfaults uninstrumented).
// One recursive mutex serializes every access. Recursive because the locked
// public entry points nest: mrbx_gettypeclass recurses on parent types,
// mrbx_pushtype -> mrbx_gettypeclass, mrbx_close_state -> mrbx_forgetstate, and
// mrb_data_object_alloc inside mrbx_pushtype can trigger GC -> mrbx_object_free,
// all on the same thread. mruby's GC is per-VM and touches no other global lock,
// so holding this across an allocation introduces no lock-order inversion.
static std::recursive_mutex g_runtimeMutex;
typedef std::lock_guard<std::recursive_mutex> RuntimeLock;

// Identity registry: (mrb_state*, love::Object*) -> the one live Ruby wrapper
// for that object in that VM. This is a *weak* map — it is not GC-protected, so
// it never keeps a wrapper (or the C++ object the wrapper retains) alive. The
// wrapper's free callback (mrbx_object_free) evicts the entry when the wrapper
// is collected, so a later mrbx_pushtype mints a fresh one. While a wrapper is
// alive, every push of the same engine object returns it, so the two compare
// equal (==) — matching the Lua-era weak-valued userdata table.
static std::map<std::pair<mrb_state *, love::Object *>, mrb_value> objectWrappers;

// Module instances held by each state's bindings, in registration order. Each
// entry is one binding reference, released (in reverse) by mrbx_close_state.
static std::map<mrb_state *, std::vector<love::Object *>> moduleInstances;

static void mrbx_object_free(mrb_state *mrb, void *p)
{
	RuntimeLock lock(g_runtimeMutex);
	if (p != nullptr)
	{
		// Drop the weak identity entry before releasing: this wrapper is gone, so
		// the next push of the same object must build a new one.
		objectWrappers.erase(std::make_pair(mrb, (love::Object *) p));
		((love::Object *) p)->release();
	}
}

const mrb_data_type mrbx_object_data_type = { "love::Object", mrbx_object_free };

// Maps each love::Type to its lazily-created Ruby class under Love::.
// Maps a love::Type to its Ruby class, per mrb_state. The state is part of the
// key because each thread runs its own mrb_state (and an RClass belongs to one
// state); without it a thread VM would get back the main VM's stale class.
// Entries for a state are dropped by mrbx_forgetstate when that state closes.
static std::map<std::pair<mrb_state *, const love::Type *>, RClass *> typeClasses;

struct RClass *mrbx_gettypeclass(mrb_state *mrb, const love::Type &type)
{
	RuntimeLock lock(g_runtimeMutex);
	auto key = std::make_pair(mrb, &type);
	auto it = typeClasses.find(key);
	if (it != typeClasses.end())
		return it->second;

	struct RClass *love = mrb_module_get(mrb, "Love");

	// Mirror the love::Type hierarchy in the Ruby class hierarchy: a type's Ruby
	// superclass is its parent type's Ruby class (recursively), bottoming out at
	// love::Object::type, which maps to Ruby's Object. This gives real Ruby
	// inheritance — e.g. a ByteData is_a?(Love::Data) — so base-class methods
	// (the Data instance methods) can be registered once on the base and are
	// inherited by every subclass, and mrbx_checktype's kind_of check accepts
	// subtypes.
	struct RClass *super = mrb->object_class;
	love::Type *parent = type.getParent();
	if (parent != nullptr && parent != &love::Object::type)
		super = mrbx_gettypeclass(mrb, *parent);

	// type.getName() may be a dotted path (e.g. love.graphics.Image); use the
	// last component as the Ruby class name.
	std::string name = type.getName();
	size_t dot = name.find_last_of('.');
	if (dot != std::string::npos)
		name = name.substr(dot + 1);

	struct RClass *cls = mrb_define_class_under(mrb, love, name.c_str(), super);
	MRB_SET_INSTANCE_TT(cls, MRB_TT_DATA);

	typeClasses[key] = cls;
	return cls;
}

// --- Per-object user data -------------------------------------------------

// (mrb_state*, love::Object*) -> the GC-protected user-data value. The state is
// part of the key so distinct VMs don't collide and a closing state's entries
// can be dropped; the value is registered with that state's GC while present.
static std::map<std::pair<mrb_state *, love::Object *>, mrb_value> userData;

void mrbx_set_userdata(mrb_state *mrb, love::Object *object, mrb_value value)
{
	RuntimeLock lock(g_runtimeMutex);
	auto key = std::make_pair(mrb, object);
	auto it = userData.find(key);
	if (it != userData.end())
	{
		mrb_gc_unregister(mrb, it->second);
		userData.erase(it);
	}

	// Storing nil just clears the slot (matching getUserData's nil default).
	if (mrb_nil_p(value))
		return;

	mrb_gc_register(mrb, value);
	userData[key] = value;
}

mrb_value mrbx_get_userdata(mrb_state *mrb, love::Object *object)
{
	RuntimeLock lock(g_runtimeMutex);
	auto it = userData.find(std::make_pair(mrb, object));
	return it == userData.end() ? mrb_nil_value() : it->second;
}

void mrbx_clear_userdata(love::Object *object)
{
	RuntimeLock lock(g_runtimeMutex);
	// Called from engine teardown without an mrb_state in hand, so drop the
	// object's entry across every state, unregistering with each one's GC.
	for (auto it = userData.begin(); it != userData.end(); )
	{
		if (it->first.second == object)
		{
			mrb_gc_unregister(it->first.first, it->second);
			it = userData.erase(it);
		}
		else
			++it;
	}
}

// --- Stored callbacks -----------------------------------------------------

// (const void* key) -> (owning mrb_state, GC-protected callable). Used by the
// box2d World collision callbacks / contact filter (#phys-callbacks): the
// binding stores a Ruby Proc here and the engine invokes it during World::update.
// The callable is GC-protected while stored; the key is a stable address the
// caller owns (the engine callback-holder sub-object, identified by its `this`).
static std::map<const void *, std::pair<mrb_state *, mrb_value>> callbacks;

void mrbx_set_callback(mrb_state *mrb, const void *key, mrb_value callback)
{
	RuntimeLock lock(g_runtimeMutex);
	auto it = callbacks.find(key);
	if (it != callbacks.end())
	{
		mrb_gc_unregister(it->second.first, it->second.second);
		callbacks.erase(it);
	}

	// A nil/undef callable just clears the slot (matching the Lua setCallbacks,
	// where an omitted function disables that event).
	if (mrb_nil_p(callback) || mrb_undef_p(callback))
		return;

	mrb_gc_register(mrb, callback);
	callbacks[key] = std::make_pair(mrb, callback);
}

bool mrbx_get_callback(const void *key, mrb_state **mrb_out, mrb_value *callback_out)
{
	RuntimeLock lock(g_runtimeMutex);
	auto it = callbacks.find(key);
	if (it == callbacks.end())
		return false;
	if (mrb_out != nullptr)
		*mrb_out = it->second.first;
	if (callback_out != nullptr)
		*callback_out = it->second.second;
	return true;
}

void mrbx_clear_callback(const void *key)
{
	RuntimeLock lock(g_runtimeMutex);
	auto it = callbacks.find(key);
	if (it == callbacks.end())
		return;
	mrb_gc_unregister(it->second.first, it->second.second);
	callbacks.erase(it);
}

// Drop all cached type->class entries for a closing mrb_state, so a later state
// reusing the same address can't be handed a stale RClass. Call before mrb_close.
void mrbx_forgetstate(mrb_state *mrb)
{
	RuntimeLock lock(g_runtimeMutex);
	for (auto it = typeClasses.begin(); it != typeClasses.end(); )
	{
		if (it->first.first == mrb)
			it = typeClasses.erase(it);
		else
			++it;
	}

	// Likewise drop any user-data entries bound to this state (their values die
	// with the VM). No need to unregister — the GC roots go with the state.
	for (auto it = userData.begin(); it != userData.end(); )
	{
		if (it->first.first == mrb)
			it = userData.erase(it);
		else
			++it;
	}

	// And the weak identity entries: the wrappers die with the VM, so drop the
	// dangling keys before mrb_close runs their free callbacks.
	for (auto it = objectWrappers.begin(); it != objectWrappers.end(); )
	{
		if (it->first.first == mrb)
			it = objectWrappers.erase(it);
		else
			++it;
	}

	// Stored callbacks bound to this state die with it too (their GC roots go
	// with the state, so no unregister needed). Dropping them here means a later
	// World teardown on a reused address can't touch this closed state.
	for (auto it = callbacks.begin(); it != callbacks.end(); )
	{
		if (it->second.first == mrb)
			it = callbacks.erase(it);
		else
			++it;
	}
}

void mrbx_close_state(mrb_state *mrb)
{
	RuntimeLock lock(g_runtimeMutex);
	// Take this state's tracked module instances before closing the VM; the map
	// key dangles once mrb is freed, so we must detach the list first.
	std::vector<love::Object *> modules;
	auto it = moduleInstances.find(mrb);
	if (it != moduleInstances.end())
	{
		modules = std::move(it->second);
		moduleInstances.erase(it);
	}

	// Drop this state's cached entries, then close the VM. mrb_close runs every
	// object wrapper's free callback (mrbx_object_free), releasing the game
	// objects -- so the modules they depend on are still alive while those
	// destructors run (objects-before-modules, as in the Lua build's lua_close).
	mrbx_forgetstate(mrb);
	mrb_close(mrb);

	// Now release the binding references, in reverse registration order. This
	// tears down graphics before the window, so ~Graphics frees its GPU resources
	// (and, via ~Window's cascade, runs) while the GL context the window owns is
	// still alive -- matching the Lua build, where the window-owned graphics dies
	// before the window's context does.
	//
	// One dependency can't be expressed by a single linear pass: the graphics
	// module owns an internal default Font that is NOT a Ruby object, so it
	// survives mrb_close and is destroyed inside ~Graphics. That Font's FreeType
	// face uses the FT_Library owned by the font module by raw pointer (see
	// modules/font/freetype/Font.cpp), so the font module must outlive graphics --
	// but graphics is registered after font, so reverse order would free font
	// first. Defer the font module past every other module to keep its FT_Library
	// alive through the graphics teardown.
	//
	// A module shared with another live VM (a thread) only drops one ref here and
	// is actually destroyed when the last VM releases it.
	love::Object *fontModule = nullptr;
	for (auto i = modules.rbegin(); i != modules.rend(); ++i)
	{
		love::Module *mod = dynamic_cast<love::Module *>(*i);
		if (mod != nullptr && mod->getModuleType() == Module::M_FONT)
		{
			fontModule = *i;
			continue;
		}
		(*i)->release();
	}
	if (fontModule != nullptr)
		fontModule->release();
}

mrb_value mrbx_pushtype(mrb_state *mrb, love::Type &type, love::Object *object)
{
	if (object == nullptr)
		return mrb_nil_value();

	RuntimeLock lock(g_runtimeMutex);

	// Return the existing wrapper for this object if one is still alive, so all
	// Ruby handles to the same engine object are identical (==). See the
	// objectWrappers note above for why this weak cache can't leak.
	auto key = std::make_pair(mrb, object);
	auto it = objectWrappers.find(key);
	if (it != objectWrappers.end())
		return it->second;

	struct RClass *cls = mrbx_gettypeclass(mrb, type);

	object->retain();
	struct RData *data = mrb_data_object_alloc(mrb, cls, object, &mrbx_object_data_type);
	mrb_value wrapper = mrb_obj_value(data);
	objectWrappers[key] = wrapper;
	return wrapper;
}

bool mrbx_istype(mrb_state *mrb, mrb_value v, const love::Type &type)
{
	if (mrb_data_check_get_ptr(mrb, v, &mrbx_object_data_type) == nullptr)
		return false;
	return mrb_obj_is_kind_of(mrb, v, mrbx_gettypeclass(mrb, type));
}

love::Object *mrbx_checktype(mrb_state *mrb, mrb_value v, const love::Type &type)
{
	void *p = mrb_data_check_get_ptr(mrb, v, &mrbx_object_data_type);
	if (p == nullptr)
		mrb_raisef(mrb, E_TYPE_ERROR, "expected a %s", type.getName());

	love::Object *o = (love::Object *) p;

	// Verify the concrete type derives from the requested one. Ensuring the
	// requested type's class exists (rather than only checking if it happens to
	// be registered) makes the check sound even when the base class hasn't been
	// touched yet — e.g. mrbx_checktype<Data> before any Data subtype is pushed.
	struct RClass *cls = mrbx_gettypeclass(mrb, type);
	if (!mrb_obj_is_kind_of(mrb, v, cls))
		mrb_raisef(mrb, E_TYPE_ERROR, "expected a %s", type.getName());

	return o;
}

// Finds the love::Type a wrapped Ruby object was created with, by reverse
// lookup of its class in the type->class map. Returns nullptr if v isn't a
// wrapped love::Object.
static love::Type *mrbx_typeof(mrb_state *mrb, mrb_value v)
{
	if (mrb_data_check_get_ptr(mrb, v, &mrbx_object_data_type) == nullptr)
		return nullptr;

	RuntimeLock lock(g_runtimeMutex);
	struct RClass *cls = mrb_obj_class(mrb, v);
	for (const auto &pair : typeClasses)
	{
		if (pair.first.first == mrb && pair.second == cls)
			return const_cast<love::Type *>(pair.first.second);
	}
	return nullptr;
}

// --- Variant <-> Ruby ----------------------------------------------------

mrb_value mrbx_pushvariant(mrb_state *mrb, const Variant &v)
{
	const Variant::Data &data = v.getData();

	switch (v.getType())
	{
	case Variant::BOOLEAN:
		return mrb_bool_value(data.boolean);
	case Variant::NUMBER:
		return mrb_float_value(mrb, data.number);
	case Variant::STRING:
		return mrb_str_new(mrb, data.string->str, data.string->len);
	case Variant::SMALLSTRING:
		return mrb_str_new(mrb, data.smallstring.str, data.smallstring.len);
	case Variant::LOVEOBJECT:
		return mrbx_pushtype(mrb, *data.objectproxy.type, data.objectproxy.object);
	case Variant::TABLE:
	{
		const auto &pairs = data.table->pairs;

		// Reconstruct an Array when the table is a contiguous 1-based integer
		// sequence -- the exact shape mrbx_checkvariant produces for a Ruby
		// Array -- so arrays round-trip as arrays; otherwise build a Hash.
		bool sequence = !pairs.empty();
		for (size_t i = 0; i < pairs.size() && sequence; i++)
		{
			const Variant &k = pairs[i].first;
			if (k.getType() != Variant::NUMBER || k.getData().number != (double)(i + 1))
				sequence = false;
		}

		if (sequence)
		{
			mrb_value arr = mrb_ary_new_capa(mrb, (mrb_int) pairs.size());
			for (const auto &kv : pairs)
				mrb_ary_push(mrb, arr, mrbx_pushvariant(mrb, kv.second));
			return arr;
		}

		mrb_value hash = mrb_hash_new(mrb);
		for (const auto &kv : pairs)
			mrb_hash_set(mrb, hash, mrbx_pushvariant(mrb, kv.first), mrbx_pushvariant(mrb, kv.second));
		return hash;
	}
	case Variant::LUSERDATA:
		// Light userdata (raw pointers, e.g. touch ids) has no safe Ruby
		// representation; surface it as an integer address.
		return mrb_fixnum_value((mrb_int)(intptr_t) data.userdata);
	case Variant::NIL:
	case Variant::UNKNOWN:
	default:
		return mrb_nil_value();
	}
}

Variant mrbx_checkvariant(mrb_state *mrb, mrb_value v)
{
	switch (mrb_type(v))
	{
	case MRB_TT_FALSE:
		// MRB_TT_FALSE covers both nil and false; distinguish by value.
		return mrb_nil_p(v) ? Variant() : Variant(false);
	case MRB_TT_TRUE:
		return Variant(true);
	case MRB_TT_INTEGER:
		return Variant((double) mrb_integer(v));
	case MRB_TT_FLOAT:
		return Variant(mrb_float(v));
	case MRB_TT_STRING:
		return Variant(RSTRING_PTR(v), RSTRING_LEN(v));
	case MRB_TT_SYMBOL:
	{
		mrb_int len = 0;
		const char *name = mrb_sym_name_len(mrb, mrb_symbol(v), &len);
		return Variant(name, (size_t) len);
	}
	case MRB_TT_ARRAY:
	{
		// Map an Array to a table with 1-based integer keys (Lua convention),
		// so round-tripping through native code matches LÖVE's table semantics.
		Variant::SharedTable *table = new Variant::SharedTable();
		mrb_int n = RARRAY_LEN(v);
		table->pairs.reserve(n);
		for (mrb_int i = 0; i < n; i++)
			table->pairs.emplace_back(Variant((double)(i + 1)), mrbx_checkvariant(mrb, mrb_ary_ref(mrb, v, i)));
		// Variant takes ownership of the table's initial reference.
		return Variant(table);
	}
	case MRB_TT_HASH:
	{
		Variant::SharedTable *table = new Variant::SharedTable();
		mrb_value keys = mrb_hash_keys(mrb, v);
		mrb_int n = RARRAY_LEN(keys);
		table->pairs.reserve(n);
		for (mrb_int i = 0; i < n; i++)
		{
			mrb_value k = mrb_ary_ref(mrb, keys, i);
			table->pairs.emplace_back(mrbx_checkvariant(mrb, k), mrbx_checkvariant(mrb, mrb_hash_get(mrb, v, k)));
		}
		// Variant takes ownership of the table's initial reference.
		return Variant(table);
	}
	case MRB_TT_DATA:
	{
		love::Type *type = mrbx_typeof(mrb, v);
		if (type != nullptr)
			return Variant(type, (love::Object *) mrb_data_check_get_ptr(mrb, v, &mrbx_object_data_type));
		return Variant::unknown();
	}
	default:
		return Variant::unknown();
	}
}

// --- Module registration -------------------------------------------------

void mrbx_track_module(mrb_state *mrb, love::Object *module)
{
	RuntimeLock lock(g_runtimeMutex);
	if (module != nullptr)
		moduleInstances[mrb].push_back(module);
}

void mrbx_register_module(mrb_state *mrb, const WrappedModule &m)
{
	// The caller's init already holds one reference for this binding (the `new`
	// on first creation, or an inst->retain() on reuse); track it so the matching
	// release happens in mrbx_close_state. No extra retain here -- that would
	// leave the singleton at a nonzero refcount on quit, so its destructor (the
	// window/graphics/audio teardown) would never run.
	mrbx_track_module(mrb, m.module);

	struct RClass *love = mrb_module_get(mrb, "Love");
	struct RClass *mod = mrb_define_module_under(mrb, love, m.name);

	for (const MrbReg *r = m.functions; r != nullptr && r->name != nullptr; r++)
		mrb_define_module_function(mrb, mod, r->name, r->func, r->aspec);
}

void mrbx_register_type(mrb_state *mrb, const love::Type &type, const MrbReg *functions)
{
	struct RClass *cls = mrbx_gettypeclass(mrb, type);

	for (const MrbReg *r = functions; r != nullptr && r->name != nullptr; r++)
		mrb_define_method(mrb, cls, r->name, r->func, r->aspec);
}

} // love
