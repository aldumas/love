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

#include <unordered_map>

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

static void mrbx_object_free(mrb_state *mrb, void *p)
{
	(void) mrb;
	if (p != nullptr)
		((love::Object *) p)->release();
}

const mrb_data_type mrbx_object_data_type = { "love::Object", mrbx_object_free };

// Maps each love::Type to its lazily-created Ruby class under Love::.
// Single-state foundation; a per-mrb_state map would be needed for multiple
// concurrently live states (threads), which is future work.
static std::unordered_map<const love::Type *, RClass *> typeClasses;

struct RClass *mrbx_gettypeclass(mrb_state *mrb, const love::Type &type)
{
	auto it = typeClasses.find(&type);
	if (it != typeClasses.end())
		return it->second;

	struct RClass *love = mrb_module_get(mrb, "Love");

	// type.getName() may be a dotted path (e.g. love.graphics.Image); use the
	// last component as the Ruby class name.
	std::string name = type.getName();
	size_t dot = name.find_last_of('.');
	if (dot != std::string::npos)
		name = name.substr(dot + 1);

	struct RClass *cls = mrb_define_class_under(mrb, love, name.c_str(), mrb->object_class);
	MRB_SET_INSTANCE_TT(cls, MRB_TT_DATA);

	typeClasses[&type] = cls;
	return cls;
}

mrb_value mrbx_pushtype(mrb_state *mrb, love::Type &type, love::Object *object)
{
	if (object == nullptr)
		return mrb_nil_value();

	struct RClass *cls = mrbx_gettypeclass(mrb, type);

	object->retain();
	struct RData *data = mrb_data_object_alloc(mrb, cls, object, &mrbx_object_data_type);
	return mrb_obj_value(data);
}

love::Object *mrbx_checktype(mrb_state *mrb, mrb_value v, const love::Type &type)
{
	void *p = mrb_data_check_get_ptr(mrb, v, &mrbx_object_data_type);
	if (p == nullptr)
		mrb_raisef(mrb, E_TYPE_ERROR, "expected a %s", type.getName());

	love::Object *o = (love::Object *) p;

	// Verify the concrete type derives from the requested one.
	auto it = typeClasses.find(&type);
	if (it != typeClasses.end() && !mrb_obj_is_kind_of(mrb, v, it->second))
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

	struct RClass *cls = mrb_obj_class(mrb, v);
	for (const auto &pair : typeClasses)
	{
		if (pair.second == cls)
			return const_cast<love::Type *>(pair.first);
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
		mrb_value hash = mrb_hash_new(mrb);
		for (const auto &kv : data.table->pairs)
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

void mrbx_register_module(mrb_state *mrb, const WrappedModule &m)
{
	if (m.module != nullptr)
		m.module->retain();

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
