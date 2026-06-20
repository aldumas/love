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

// mruby port of wrap_Audio.cpp + wrap_Source.cpp + wrap_RecordingDevice.cpp.
//
// Exposes Love::Audio (the module), the Love::Source object type, and the
// Love::RecordingDevice object type. snake_case names, keyword arguments,
// `?`-suffixed predicates. The backend is the **real** OpenAL Audio (links
// system libopenal), with the null backend kept as the fallback exactly as the
// Lua loader did.
//
// Effect/Filter descriptions, which were Lua tables, become Ruby Hashes keyed
// by the same parameter-name strings (symbol or string keys both accepted); a
// `type:` entry is mandatory. Listener/source vectors that returned several Lua
// numbers return a Hash here ({x:, y:, z:} etc.).
//
// Source creation mirrors the Lua newSource convention:
//   Love::Audio.new_source(file: <name/Data>, type: "static"|"stream")
//   Love::Audio.new_source(decoder: <Decoder>, type: ...)
//   Love::Audio.new_source(sound_data: <SoundData>)        # always static
// Building a Decoder from a filename needs the sound + filesystem modules.
//
// Not ported (intentionally): Source#queue's raw-pointer (lightuserdata) form —
// an FFI-style path with no mruby analog; the SoundData form is supported.

#include "common/config.h"
#include "common/mrb_runtime.h"
#include "common/Module.h"
#include "common/Object.h"
#include "common/Data.h"
#include "common/Stream.h"
#include "common/Exception.h"

#include "Audio.h"
#include "Source.h"
#include "Effect.h"
#include "Filter.h"
#include "RecordingDevice.h"

// Backends.
#include "openal/Audio.h"
#include "null/Audio.h"

// For building Sources from files/Decoders/SoundData.
#include "sound/Sound.h"
#include "sound/Decoder.h"
#include "sound/SoundData.h"
#include "data/DataStream.h"
#include "filesystem/Filesystem.h"
#include "filesystem/File.h"
#include "filesystem/FileData.h"

#include <cmath>
#include <limits>
#include <map>
#include <vector>
#include <string>

namespace love
{
namespace audio
{

#define instance() (Module::getInstance<Audio>(Module::M_AUDIO))

// --- small helpers --------------------------------------------------------

static void hset(mrb_state *mrb, mrb_value h, const char *key, mrb_value v)
{
	mrb_hash_set(mrb, h, mrb_symbol_value(mrb_intern_cstr(mrb, key)), v);
}

// Collect a Ruby Hash into (name, value) pairs; keys may be symbols or strings.
static std::vector<std::pair<std::string, mrb_value>> hashEntries(mrb_state *mrb, mrb_value tbl)
{
	std::vector<std::pair<std::string, mrb_value>> out;
	mrb_value keys = mrb_hash_keys(mrb, tbl);
	mrb_int n = RARRAY_LEN(keys);
	for (mrb_int i = 0; i < n; i++)
	{
		mrb_value k = mrb_ary_ref(mrb, keys, i);
		std::string name = mrb_symbol_p(k)
			? std::string(mrb_sym_name(mrb, mrb_symbol(k)))
			: mrbx_checkstring(mrb, k);
		out.push_back({ name, mrb_hash_get(mrb, tbl, k) });
	}
	return out;
}

static std::vector<Source *> readSources(mrb_state *mrb, mrb_value arr)
{
	std::vector<Source *> v;
	mrb_int n = RARRAY_LEN(arr);
	v.reserve((size_t) n);
	for (mrb_int i = 0; i < n; i++)
		v.push_back(mrbx_checktype<Source>(mrb, mrb_ary_ref(mrb, arr, i)));
	return v;
}

static mrb_value pushStringList(mrb_state *mrb, const std::vector<std::string> &list)
{
	mrb_value arr = mrb_ary_new_capa(mrb, (mrb_int) list.size());
	for (const auto &s : list)
		mrb_ary_push(mrb, arr, mrbx_string(mrb, s));
	return arr;
}

static Source::Unit checkUnit(mrb_state *mrb, mrb_value u)
{
	if (mrb_undef_p(u) || mrb_nil_p(u))
		return Source::UNIT_SECONDS;
	std::string s = mrbx_checkstring(mrb, u);
	Source::Unit unit;
	if (!Source::getConstant(s.c_str(), unit))
		mrb_raisef(mrb, E_ARGUMENT_ERROR, "invalid time unit: %s", s.c_str());
	return unit;
}

// --- Effect <-> Hash ------------------------------------------------------

static void readEffect(mrb_state *mrb, mrb_value tbl, std::map<Effect::Parameter, float> &params)
{
	auto entries = hashEntries(mrb, tbl);

	Effect::Type type = Effect::TYPE_MAX_ENUM;
	bool hasType = false;
	for (auto &e : entries)
	{
		if (e.first == "type")
		{
			std::string ts = mrbx_checkstring(mrb, e.second);
			if (!Effect::getConstant(ts.c_str(), type))
				mrb_raisef(mrb, E_ARGUMENT_ERROR, "invalid effect type: %s", ts.c_str());
			hasType = true;
		}
	}
	if (!hasType)
		mrb_raise(mrb, E_ARGUMENT_ERROR, "Effect type not specified.");

	params[Effect::EFFECT_TYPE] = (float) (int) type;

	for (auto &e : entries)
	{
		if (e.first == "type")
			continue;

		Effect::Parameter param;
		if (!(Effect::getConstant(e.first.c_str(), param, type)
			|| Effect::getConstant(e.first.c_str(), param, Effect::TYPE_BASIC)))
			mrb_raisef(mrb, E_ARGUMENT_ERROR, "invalid effect parameter: %s", e.first.c_str());

		switch (Effect::getParameterType(param))
		{
		case Effect::PARAM_FLOAT:
			params[param] = (float) mrbx_checknumber(mrb, e.second);
			break;
		case Effect::PARAM_BOOL:
			params[param] = mrbx_checkboolean(mrb, e.second) ? 1.0f : 0.0f;
			break;
		case Effect::PARAM_WAVEFORM:
		{
			std::string ws = mrbx_checkstring(mrb, e.second);
			Effect::Waveform wf;
			if (!Effect::getConstant(ws.c_str(), wf))
				mrb_raisef(mrb, E_ARGUMENT_ERROR, "invalid waveform type: %s", ws.c_str());
			params[param] = (float) (int) wf;
			break;
		}
		case Effect::PARAM_TYPE:
		case Effect::PARAM_MAX_ENUM:
			break;
		}
	}
}

static mrb_value writeEffect(mrb_state *mrb, std::map<Effect::Parameter, float> &params)
{
	mrb_value h = mrb_hash_new(mrb);
	Effect::Type type = (Effect::Type) (int) params[Effect::EFFECT_TYPE];
	const char *keystr = nullptr, *valstr = nullptr;

	for (auto p : params)
	{
		if (!Effect::getConstant(p.first, keystr, type))
			Effect::getConstant(p.first, keystr, Effect::TYPE_BASIC);

		mrb_value val = mrb_nil_value();
		switch (Effect::getParameterType(p.first))
		{
		case Effect::PARAM_FLOAT:
			val = mrbx_number(mrb, p.second);
			break;
		case Effect::PARAM_BOOL:
			val = mrbx_boolean(mrb, p.second > 0.5f);
			break;
		case Effect::PARAM_WAVEFORM:
			Effect::getConstant((Effect::Waveform) (int) p.second, valstr);
			val = mrbx_string(mrb, valstr);
			break;
		case Effect::PARAM_TYPE:
			Effect::getConstant((Effect::Type) (int) p.second, valstr);
			val = mrbx_string(mrb, valstr);
			break;
		case Effect::PARAM_MAX_ENUM:
			break;
		}
		hset(mrb, h, keystr, val);
	}
	return h;
}

// --- Filter <-> Hash ------------------------------------------------------

static void readFilter(mrb_state *mrb, mrb_value tbl, std::map<Filter::Parameter, float> &params)
{
	auto entries = hashEntries(mrb, tbl);

	Filter::Type type = Filter::TYPE_MAX_ENUM;
	bool hasType = false;
	for (auto &e : entries)
	{
		if (e.first == "type")
		{
			std::string ts = mrbx_checkstring(mrb, e.second);
			if (!Filter::getConstant(ts.c_str(), type))
				mrb_raisef(mrb, E_ARGUMENT_ERROR, "invalid filter type: %s", ts.c_str());
			hasType = true;
		}
	}
	if (!hasType)
		mrb_raise(mrb, E_ARGUMENT_ERROR, "Filter type not specified.");

	params[Filter::FILTER_TYPE] = (float) (int) type;

	for (auto &e : entries)
	{
		if (e.first == "type")
			continue;

		Filter::Parameter param;
		if (!(Filter::getConstant(e.first.c_str(), param, type)
			|| Filter::getConstant(e.first.c_str(), param, Filter::TYPE_BASIC)))
			mrb_raisef(mrb, E_ARGUMENT_ERROR, "invalid filter parameter: %s", e.first.c_str());

		switch (Filter::getParameterType(param))
		{
		case Filter::PARAM_FLOAT:
			params[param] = (float) mrbx_checknumber(mrb, e.second);
			break;
		case Filter::PARAM_TYPE:
		case Filter::PARAM_MAX_ENUM:
			break;
		}
	}
}

static mrb_value writeFilter(mrb_state *mrb, std::map<Filter::Parameter, float> &params)
{
	mrb_value h = mrb_hash_new(mrb);
	Filter::Type type = (Filter::Type) (int) params[Filter::FILTER_TYPE];
	const char *keystr = nullptr, *valstr = nullptr;

	for (auto p : params)
	{
		if (!Filter::getConstant(p.first, keystr, type))
			Filter::getConstant(p.first, keystr, Filter::TYPE_BASIC);

		mrb_value val = mrb_nil_value();
		switch (Filter::getParameterType(p.first))
		{
		case Filter::PARAM_FLOAT:
			val = mrbx_number(mrb, p.second);
			break;
		case Filter::PARAM_TYPE:
			Filter::getConstant((Filter::Type) (int) p.second, valstr);
			val = mrbx_string(mrb, valstr);
			break;
		case Filter::PARAM_MAX_ENUM:
			break;
		}
		hset(mrb, h, keystr, val);
	}
	return h;
}

// ==========================================================================
// Source instance methods
// ==========================================================================

static mrb_value src_clone(mrb_state *mrb, mrb_value self)
{
	Source *t = mrbx_checktype<Source>(mrb, self), *c = nullptr;
	if (mrbx_catchexcept(mrb, [&]() { c = t->clone(); }))
		return mrb_nil_value();
	mrb_value r = mrbx_pushtype(mrb, c);
	if (c) c->release();
	return r;
}

static mrb_value src_play(mrb_state *mrb, mrb_value self)
{
	return mrbx_boolean(mrb, mrbx_checktype<Source>(mrb, self)->play());
}

static mrb_value src_stop(mrb_state *mrb, mrb_value self)
{
	mrbx_checktype<Source>(mrb, self)->stop();
	return self;
}

static mrb_value src_pause(mrb_state *mrb, mrb_value self)
{
	mrbx_checktype<Source>(mrb, self)->pause();
	return self;
}

static mrb_value src_setPitch(mrb_state *mrb, mrb_value self)
{
	Source *t = mrbx_checktype<Source>(mrb, self);
	mrb_value v[1];
	mrbx_get_kwargs(mrb, {"pitch"}, 1, v);
	float p = (float) mrbx_checknumber(mrb, v[0]);
	if (p != p)
		mrb_raise(mrb, E_ARGUMENT_ERROR, "Pitch cannot be NaN.");
	if (p <= 0.0f || !std::isfinite(p))
		mrb_raise(mrb, E_ARGUMENT_ERROR, "Pitch has to be a non-zero, positive, finite number.");
	t->setPitch(p);
	return self;
}

static mrb_value src_getPitch(mrb_state *mrb, mrb_value self)
{
	return mrbx_number(mrb, mrbx_checktype<Source>(mrb, self)->getPitch());
}

static mrb_value src_setVolume(mrb_state *mrb, mrb_value self)
{
	Source *t = mrbx_checktype<Source>(mrb, self);
	mrb_value v[1];
	mrbx_get_kwargs(mrb, {"volume"}, 1, v);
	t->setVolume((float) mrbx_checknumber(mrb, v[0]));
	return self;
}

static mrb_value src_getVolume(mrb_state *mrb, mrb_value self)
{
	return mrbx_number(mrb, mrbx_checktype<Source>(mrb, self)->getVolume());
}

static mrb_value src_seek(mrb_state *mrb, mrb_value self)
{
	Source *t = mrbx_checktype<Source>(mrb, self);
	mrb_value v[2];
	mrbx_get_kwargs(mrb, {"offset", "unit"}, 1, v);
	double offset = mrbx_checknumber(mrb, v[0]);
	if (offset < 0)
		mrb_raise(mrb, E_ARGUMENT_ERROR, "can't seek to a negative position");
	t->seek(offset, checkUnit(mrb, v[1]));
	return self;
}

static mrb_value src_tell(mrb_state *mrb, mrb_value self)
{
	Source *t = mrbx_checktype<Source>(mrb, self);
	mrb_value v[1];
	mrbx_get_kwargs(mrb, {"unit"}, 0, v);
	return mrbx_number(mrb, t->tell(checkUnit(mrb, v[0])));
}

static mrb_value src_getDuration(mrb_state *mrb, mrb_value self)
{
	Source *t = mrbx_checktype<Source>(mrb, self);
	mrb_value v[1];
	mrbx_get_kwargs(mrb, {"unit"}, 0, v);
	return mrbx_number(mrb, t->getDuration(checkUnit(mrb, v[0])));
}

static mrb_value src_setPosition(mrb_state *mrb, mrb_value self)
{
	Source *t = mrbx_checktype<Source>(mrb, self);
	mrb_value v[3];
	mrbx_get_kwargs(mrb, {"x", "y", "z"}, 2, v);
	float p[3] = { (float) mrbx_checknumber(mrb, v[0]), (float) mrbx_checknumber(mrb, v[1]), (float) mrbx_optnumber(mrb, v[2], 0.0) };
	mrbx_catchexcept(mrb, [&]() { t->setPosition(p); });
	return self;
}

static mrb_value src_getPosition(mrb_state *mrb, mrb_value self)
{
	Source *t = mrbx_checktype<Source>(mrb, self);
	float p[3];
	if (mrbx_catchexcept(mrb, [&]() { t->getPosition(p); }))
		return mrb_nil_value();
	mrb_value h = mrb_hash_new(mrb);
	hset(mrb, h, "x", mrbx_number(mrb, p[0]));
	hset(mrb, h, "y", mrbx_number(mrb, p[1]));
	hset(mrb, h, "z", mrbx_number(mrb, p[2]));
	return h;
}

static mrb_value src_setVelocity(mrb_state *mrb, mrb_value self)
{
	Source *t = mrbx_checktype<Source>(mrb, self);
	mrb_value v[3];
	mrbx_get_kwargs(mrb, {"x", "y", "z"}, 2, v);
	float p[3] = { (float) mrbx_checknumber(mrb, v[0]), (float) mrbx_checknumber(mrb, v[1]), (float) mrbx_optnumber(mrb, v[2], 0.0) };
	mrbx_catchexcept(mrb, [&]() { t->setVelocity(p); });
	return self;
}

static mrb_value src_getVelocity(mrb_state *mrb, mrb_value self)
{
	Source *t = mrbx_checktype<Source>(mrb, self);
	float p[3];
	if (mrbx_catchexcept(mrb, [&]() { t->getVelocity(p); }))
		return mrb_nil_value();
	mrb_value h = mrb_hash_new(mrb);
	hset(mrb, h, "x", mrbx_number(mrb, p[0]));
	hset(mrb, h, "y", mrbx_number(mrb, p[1]));
	hset(mrb, h, "z", mrbx_number(mrb, p[2]));
	return h;
}

static mrb_value src_setDirection(mrb_state *mrb, mrb_value self)
{
	Source *t = mrbx_checktype<Source>(mrb, self);
	mrb_value v[3];
	mrbx_get_kwargs(mrb, {"x", "y", "z"}, 2, v);
	float p[3] = { (float) mrbx_checknumber(mrb, v[0]), (float) mrbx_checknumber(mrb, v[1]), (float) mrbx_optnumber(mrb, v[2], 0.0) };
	mrbx_catchexcept(mrb, [&]() { t->setDirection(p); });
	return self;
}

static mrb_value src_getDirection(mrb_state *mrb, mrb_value self)
{
	Source *t = mrbx_checktype<Source>(mrb, self);
	float p[3];
	if (mrbx_catchexcept(mrb, [&]() { t->getDirection(p); }))
		return mrb_nil_value();
	mrb_value h = mrb_hash_new(mrb);
	hset(mrb, h, "x", mrbx_number(mrb, p[0]));
	hset(mrb, h, "y", mrbx_number(mrb, p[1]));
	hset(mrb, h, "z", mrbx_number(mrb, p[2]));
	return h;
}

static mrb_value src_setCone(mrb_state *mrb, mrb_value self)
{
	Source *t = mrbx_checktype<Source>(mrb, self);
	mrb_value v[4];
	mrbx_get_kwargs(mrb, {"inner_angle", "outer_angle", "outer_volume", "outer_high_gain"}, 2, v);
	float ia = (float) mrbx_checknumber(mrb, v[0]);
	float oa = (float) mrbx_checknumber(mrb, v[1]);
	float ov = (float) mrbx_optnumber(mrb, v[2], 0.0);
	float og = (float) mrbx_optnumber(mrb, v[3], 1.0);
	mrbx_catchexcept(mrb, [&]() { t->setCone(ia, oa, ov, og); });
	return self;
}

static mrb_value src_getCone(mrb_state *mrb, mrb_value self)
{
	Source *t = mrbx_checktype<Source>(mrb, self);
	float ia, oa, ov, og;
	if (mrbx_catchexcept(mrb, [&]() { t->getCone(ia, oa, ov, og); }))
		return mrb_nil_value();
	mrb_value h = mrb_hash_new(mrb);
	hset(mrb, h, "inner_angle", mrbx_number(mrb, ia));
	hset(mrb, h, "outer_angle", mrbx_number(mrb, oa));
	hset(mrb, h, "outer_volume", mrbx_number(mrb, ov));
	hset(mrb, h, "outer_high_gain", mrbx_number(mrb, og));
	return h;
}

static mrb_value src_setRelative(mrb_state *mrb, mrb_value self)
{
	Source *t = mrbx_checktype<Source>(mrb, self);
	mrb_value v[1];
	mrbx_get_kwargs(mrb, {"relative"}, 1, v);
	bool b = mrbx_checkboolean(mrb, v[0]);
	mrbx_catchexcept(mrb, [&]() { t->setRelative(b); });
	return self;
}

static mrb_value src_isRelative(mrb_state *mrb, mrb_value self)
{
	Source *t = mrbx_checktype<Source>(mrb, self);
	bool b = false;
	if (mrbx_catchexcept(mrb, [&]() { b = t->isRelative(); }))
		return mrb_nil_value();
	return mrbx_boolean(mrb, b);
}

static mrb_value src_setLooping(mrb_state *mrb, mrb_value self)
{
	Source *t = mrbx_checktype<Source>(mrb, self);
	mrb_value v[1];
	mrbx_get_kwargs(mrb, {"looping"}, 1, v);
	bool b = mrbx_checkboolean(mrb, v[0]);
	mrbx_catchexcept(mrb, [&]() { t->setLooping(b); });
	return self;
}

static mrb_value src_isLooping(mrb_state *mrb, mrb_value self)
{
	return mrbx_boolean(mrb, mrbx_checktype<Source>(mrb, self)->isLooping());
}

static mrb_value src_isPlaying(mrb_state *mrb, mrb_value self)
{
	return mrbx_boolean(mrb, mrbx_checktype<Source>(mrb, self)->isPlaying());
}

static mrb_value src_setVolumeLimits(mrb_state *mrb, mrb_value self)
{
	Source *t = mrbx_checktype<Source>(mrb, self);
	mrb_value v[2];
	mrbx_get_kwargs(mrb, {"min", "max"}, 2, v);
	float vmin = (float) mrbx_checknumber(mrb, v[0]);
	float vmax = (float) mrbx_checknumber(mrb, v[1]);
	if (vmin < 0.0f || vmin > 1.0f || vmax < 0.0f || vmax > 1.0f)
		mrb_raise(mrb, E_ARGUMENT_ERROR, "Invalid volume limits, must be in [0, 1]");
	t->setMinVolume(vmin);
	t->setMaxVolume(vmax);
	return self;
}

static mrb_value src_getVolumeLimits(mrb_state *mrb, mrb_value self)
{
	Source *t = mrbx_checktype<Source>(mrb, self);
	mrb_value h = mrb_hash_new(mrb);
	hset(mrb, h, "min", mrbx_number(mrb, t->getMinVolume()));
	hset(mrb, h, "max", mrbx_number(mrb, t->getMaxVolume()));
	return h;
}

static mrb_value src_setAttenuationDistances(mrb_state *mrb, mrb_value self)
{
	Source *t = mrbx_checktype<Source>(mrb, self);
	mrb_value v[2];
	mrbx_get_kwargs(mrb, {"ref", "max"}, 2, v);
	float dref = (float) mrbx_checknumber(mrb, v[0]);
	float dmax = (float) mrbx_checknumber(mrb, v[1]);
	if (dref < 0.0f || dmax < 0.0f)
		mrb_raise(mrb, E_ARGUMENT_ERROR, "Invalid distances, must be >= 0");
	mrbx_catchexcept(mrb, [&]() { t->setReferenceDistance(dref); t->setMaxDistance(dmax); });
	return self;
}

static mrb_value src_getAttenuationDistances(mrb_state *mrb, mrb_value self)
{
	Source *t = mrbx_checktype<Source>(mrb, self);
	mrb_value h = mrb_hash_new(mrb);
	if (mrbx_catchexcept(mrb, [&]() {
		hset(mrb, h, "ref", mrbx_number(mrb, t->getReferenceDistance()));
		hset(mrb, h, "max", mrbx_number(mrb, t->getMaxDistance()));
	}))
		return mrb_nil_value();
	return h;
}

static mrb_value src_setRolloff(mrb_state *mrb, mrb_value self)
{
	Source *t = mrbx_checktype<Source>(mrb, self);
	mrb_value v[1];
	mrbx_get_kwargs(mrb, {"rolloff"}, 1, v);
	float rolloff = (float) mrbx_checknumber(mrb, v[0]);
	if (rolloff < 0.0f)
		mrb_raise(mrb, E_ARGUMENT_ERROR, "Invalid rolloff, must be >= 0");
	mrbx_catchexcept(mrb, [&]() { t->setRolloffFactor(rolloff); });
	return self;
}

static mrb_value src_getRolloff(mrb_state *mrb, mrb_value self)
{
	Source *t = mrbx_checktype<Source>(mrb, self);
	float r = 0.0f;
	if (mrbx_catchexcept(mrb, [&]() { r = t->getRolloffFactor(); }))
		return mrb_nil_value();
	return mrbx_number(mrb, r);
}

static mrb_value src_setAirAbsorption(mrb_state *mrb, mrb_value self)
{
	Source *t = mrbx_checktype<Source>(mrb, self);
	mrb_value v[1];
	mrbx_get_kwargs(mrb, {"factor"}, 1, v);
	float factor = (float) mrbx_checknumber(mrb, v[0]);
	if (factor < 0.0f)
		mrb_raise(mrb, E_ARGUMENT_ERROR, "Invalid air absorption factor, must be >= 0");
	mrbx_catchexcept(mrb, [&]() { t->setAirAbsorptionFactor(factor); });
	return self;
}

static mrb_value src_getAirAbsorption(mrb_state *mrb, mrb_value self)
{
	Source *t = mrbx_checktype<Source>(mrb, self);
	float f = 0.0f;
	if (mrbx_catchexcept(mrb, [&]() { f = t->getAirAbsorptionFactor(); }))
		return mrb_nil_value();
	return mrbx_number(mrb, f);
}

static mrb_value src_getChannelCount(mrb_state *mrb, mrb_value self)
{
	return mrbx_integer(mrb, mrbx_checktype<Source>(mrb, self)->getChannelCount());
}

// set_filter(settings: <Hash>) sets a filter; settings nil/false (or omitted)
// removes it. Returns whether the source supports filtering.
static mrb_value src_setFilter(mrb_state *mrb, mrb_value self)
{
	Source *t = mrbx_checktype<Source>(mrb, self);
	mrb_value v[1];
	mrbx_get_kwargs(mrb, {"settings"}, 0, v);

	bool ok = false;
	if (mrb_undef_p(v[0]) || mrb_nil_p(v[0]) || mrb_false_p(v[0]))
		mrbx_catchexcept(mrb, [&]() { ok = t->setFilter(); });
	else
	{
		std::map<Filter::Parameter, float> params;
		readFilter(mrb, v[0], params);
		mrbx_catchexcept(mrb, [&]() { ok = t->setFilter(params); });
	}
	return mrbx_boolean(mrb, ok);
}

static mrb_value src_getFilter(mrb_state *mrb, mrb_value self)
{
	Source *t = mrbx_checktype<Source>(mrb, self);
	std::map<Filter::Parameter, float> params;
	if (!t->getFilter(params))
		return mrb_nil_value();
	return writeFilter(mrb, params);
}

// set_effect(name:, filter: <Hash, opt>, enabled: <bool, opt>).
// enabled: false removes the effect; a filter Hash attaches a filter; with
// neither, the effect is set without a filter.
static mrb_value src_setEffect(mrb_state *mrb, mrb_value self)
{
	Source *t = mrbx_checktype<Source>(mrb, self);
	mrb_value v[3];
	mrbx_get_kwargs(mrb, {"name", "filter", "enabled"}, 1, v);
	std::string name = mrbx_checkstring(mrb, v[0]);

	if (!mrb_undef_p(v[2]) && !mrbx_checkboolean(mrb, v[2]))
	{
		bool ok = false;
		mrbx_catchexcept(mrb, [&]() { ok = t->unsetEffect(name.c_str()); });
		return mrbx_boolean(mrb, ok);
	}

	bool ok = false;
	if (!mrb_undef_p(v[1]) && mrb_hash_p(v[1]))
	{
		std::map<Filter::Parameter, float> params;
		readFilter(mrb, v[1], params);
		mrbx_catchexcept(mrb, [&]() { ok = t->setEffect(name.c_str(), params); });
	}
	else
		mrbx_catchexcept(mrb, [&]() { ok = t->setEffect(name.c_str()); });
	return mrbx_boolean(mrb, ok);
}

// get_effect(name:) -> false if not active, true if active without a filter, or
// the filter Hash if a filter is attached.
static mrb_value src_getEffect(mrb_state *mrb, mrb_value self)
{
	Source *t = mrbx_checktype<Source>(mrb, self);
	mrb_value v[1];
	mrbx_get_kwargs(mrb, {"name"}, 1, v);
	std::string name = mrbx_checkstring(mrb, v[0]);

	std::map<Filter::Parameter, float> params;
	if (!t->getEffect(name.c_str(), params))
		return mrbx_boolean(mrb, false);
	if (params.size() == 0)
		return mrbx_boolean(mrb, true);
	return writeFilter(mrb, params);
}

static mrb_value src_getActiveEffects(mrb_state *mrb, mrb_value self)
{
	Source *t = mrbx_checktype<Source>(mrb, self);
	std::vector<std::string> list;
	t->getActiveEffects(list);
	return pushStringList(mrb, list);
}

static mrb_value src_getFreeBufferCount(mrb_state *mrb, mrb_value self)
{
	return mrbx_integer(mrb, mrbx_checktype<Source>(mrb, self)->getFreeBufferCount());
}

// queue(sound_data:, offset: <opt 0>, length: <opt rest>) -> bool.
static mrb_value src_queue(mrb_state *mrb, mrb_value self)
{
	Source *t = mrbx_checktype<Source>(mrb, self);
	mrb_value v[3];
	mrbx_get_kwargs(mrb, {"sound_data", "offset", "length"}, 1, v);

	love::sound::SoundData *s = mrbx_checktype<love::sound::SoundData>(mrb, v[0]);
	int offset = mrbx_optint(mrb, v[1], 0);
	size_t length = mrb_undef_p(v[2]) ? (s->getSize() - offset) : (size_t) mrbx_checkint(mrb, v[2]);

	if (offset < 0 || length > s->getSize() - offset)
		mrb_raise(mrb, E_ARGUMENT_ERROR, "Data region out of bounds.");

	bool ok = false;
	mrbx_catchexcept(mrb, [&]() {
		ok = t->queue((unsigned char *) s->getData() + offset, length,
			s->getSampleRate(), s->getBitDepth(), s->getChannelCount());
	});
	return mrbx_boolean(mrb, ok);
}

static mrb_value src_getType(mrb_state *mrb, mrb_value self)
{
	Source *t = mrbx_checktype<Source>(mrb, self);
	const char *str = nullptr;
	if (!Source::getConstant(t->getType(), str))
		mrb_raise(mrb, E_RUNTIME_ERROR, "Unknown Source type.");
	return mrbx_string(mrb, str);
}

static const MrbReg sourceFunctions[] =
{
	{ "clone",                     src_clone,                  MRB_ARGS_NONE() },
	{ "play",                      src_play,                   MRB_ARGS_NONE() },
	{ "stop",                      src_stop,                   MRB_ARGS_NONE() },
	{ "pause",                     src_pause,                  MRB_ARGS_NONE() },
	{ "set_pitch",                 src_setPitch,               MRB_ARGS_KEY(1, 0) },
	{ "get_pitch",                 src_getPitch,               MRB_ARGS_NONE() },
	{ "set_volume",                src_setVolume,              MRB_ARGS_KEY(1, 0) },
	{ "get_volume",                src_getVolume,              MRB_ARGS_NONE() },
	{ "seek",                      src_seek,                   MRB_ARGS_KEY(2, 0) },
	{ "tell",                      src_tell,                   MRB_ARGS_KEY(1, 0) },
	{ "get_duration",              src_getDuration,            MRB_ARGS_KEY(1, 0) },
	{ "set_position",              src_setPosition,            MRB_ARGS_KEY(3, 0) },
	{ "get_position",              src_getPosition,            MRB_ARGS_NONE() },
	{ "set_velocity",              src_setVelocity,            MRB_ARGS_KEY(3, 0) },
	{ "get_velocity",              src_getVelocity,            MRB_ARGS_NONE() },
	{ "set_direction",             src_setDirection,           MRB_ARGS_KEY(3, 0) },
	{ "get_direction",             src_getDirection,           MRB_ARGS_NONE() },
	{ "set_cone",                  src_setCone,                MRB_ARGS_KEY(4, 0) },
	{ "get_cone",                  src_getCone,                MRB_ARGS_NONE() },
	{ "set_relative",              src_setRelative,            MRB_ARGS_KEY(1, 0) },
	{ "relative?",                 src_isRelative,             MRB_ARGS_NONE() },
	{ "set_looping",               src_setLooping,             MRB_ARGS_KEY(1, 0) },
	{ "looping?",                  src_isLooping,              MRB_ARGS_NONE() },
	{ "playing?",                  src_isPlaying,              MRB_ARGS_NONE() },
	{ "set_volume_limits",         src_setVolumeLimits,        MRB_ARGS_KEY(2, 0) },
	{ "get_volume_limits",         src_getVolumeLimits,        MRB_ARGS_NONE() },
	{ "set_attenuation_distances", src_setAttenuationDistances, MRB_ARGS_KEY(2, 0) },
	{ "get_attenuation_distances", src_getAttenuationDistances, MRB_ARGS_NONE() },
	{ "set_rolloff",               src_setRolloff,             MRB_ARGS_KEY(1, 0) },
	{ "get_rolloff",               src_getRolloff,             MRB_ARGS_NONE() },
	{ "set_air_absorption",        src_setAirAbsorption,       MRB_ARGS_KEY(1, 0) },
	{ "get_air_absorption",        src_getAirAbsorption,       MRB_ARGS_NONE() },
	{ "get_channel_count",         src_getChannelCount,        MRB_ARGS_NONE() },
	{ "set_filter",                src_setFilter,              MRB_ARGS_KEY(1, 0) },
	{ "get_filter",                src_getFilter,              MRB_ARGS_NONE() },
	{ "set_effect",                src_setEffect,              MRB_ARGS_KEY(3, 0) },
	{ "get_effect",                src_getEffect,              MRB_ARGS_KEY(1, 0) },
	{ "get_active_effects",        src_getActiveEffects,       MRB_ARGS_NONE() },
	{ "get_free_buffer_count",     src_getFreeBufferCount,     MRB_ARGS_NONE() },
	{ "queue",                     src_queue,                  MRB_ARGS_KEY(3, 0) },
	{ "get_type",                  src_getType,                MRB_ARGS_NONE() },
	{ nullptr, nullptr, 0 }
};

// ==========================================================================
// RecordingDevice instance methods
// ==========================================================================

static mrb_value rec_start(mrb_state *mrb, mrb_value self)
{
	RecordingDevice *d = mrbx_checktype<RecordingDevice>(mrb, self);
	mrb_value v[4];
	mrbx_get_kwargs(mrb, {"samples", "sample_rate", "bit_depth", "channels"}, 0, v);

	int samples, samplerate, bitdepth, channels;
	if (mrb_undef_p(v[0]))
	{
		samples = d->getMaxSamples();
		samplerate = d->getSampleRate();
		bitdepth = d->getBitDepth();
		channels = d->getChannelCount();
	}
	else
	{
		samples = mrbx_checkint(mrb, v[0]);
		samplerate = mrbx_optint(mrb, v[1], RecordingDevice::DEFAULT_SAMPLE_RATE);
		bitdepth = mrbx_optint(mrb, v[2], RecordingDevice::DEFAULT_BIT_DEPTH);
		channels = mrbx_optint(mrb, v[3], RecordingDevice::DEFAULT_CHANNELS);
	}

	bool ok = false;
	mrbx_catchexcept(mrb, [&]() { ok = d->start(samples, samplerate, bitdepth, channels); });
	return mrbx_boolean(mrb, ok);
}

static mrb_value rec_stop(mrb_state *mrb, mrb_value self)
{
	RecordingDevice *d = mrbx_checktype<RecordingDevice>(mrb, self);
	love::sound::SoundData *s = nullptr;
	mrbx_catchexcept(mrb, [&]() { s = d->getData(); });
	d->stop();
	if (s == nullptr)
		return mrb_nil_value();
	mrb_value r = mrbx_pushtype(mrb, s);
	s->release();
	return r;
}

static mrb_value rec_getData(mrb_state *mrb, mrb_value self)
{
	RecordingDevice *d = mrbx_checktype<RecordingDevice>(mrb, self);
	love::sound::SoundData *s = nullptr;
	mrbx_catchexcept(mrb, [&]() { s = d->getData(); });
	if (s == nullptr)
		return mrb_nil_value();
	mrb_value r = mrbx_pushtype(mrb, s);
	s->release();
	return r;
}

static mrb_value rec_getSampleCount(mrb_state *mrb, mrb_value self)
{
	return mrbx_integer(mrb, mrbx_checktype<RecordingDevice>(mrb, self)->getSampleCount());
}

static mrb_value rec_getSampleRate(mrb_state *mrb, mrb_value self)
{
	return mrbx_integer(mrb, mrbx_checktype<RecordingDevice>(mrb, self)->getSampleRate());
}

static mrb_value rec_getBitDepth(mrb_state *mrb, mrb_value self)
{
	return mrbx_integer(mrb, mrbx_checktype<RecordingDevice>(mrb, self)->getBitDepth());
}

static mrb_value rec_getChannelCount(mrb_state *mrb, mrb_value self)
{
	return mrbx_integer(mrb, mrbx_checktype<RecordingDevice>(mrb, self)->getChannelCount());
}

static mrb_value rec_getName(mrb_state *mrb, mrb_value self)
{
	return mrbx_string(mrb, mrbx_checktype<RecordingDevice>(mrb, self)->getName());
}

static mrb_value rec_isRecording(mrb_state *mrb, mrb_value self)
{
	return mrbx_boolean(mrb, mrbx_checktype<RecordingDevice>(mrb, self)->isRecording());
}

static const MrbReg recordingDeviceFunctions[] =
{
	{ "start",             rec_start,           MRB_ARGS_KEY(4, 0) },
	{ "stop",              rec_stop,            MRB_ARGS_NONE() },
	{ "get_data",          rec_getData,         MRB_ARGS_NONE() },
	{ "get_sample_count",  rec_getSampleCount,  MRB_ARGS_NONE() },
	{ "get_sample_rate",   rec_getSampleRate,   MRB_ARGS_NONE() },
	{ "get_bit_depth",     rec_getBitDepth,     MRB_ARGS_NONE() },
	{ "get_channel_count", rec_getChannelCount, MRB_ARGS_NONE() },
	{ "get_name",          rec_getName,         MRB_ARGS_NONE() },
	{ "recording?",        rec_isRecording,     MRB_ARGS_NONE() },
	{ nullptr, nullptr, 0 }
};

// ==========================================================================
// Module-level functions
// ==========================================================================

// Build a love::sound::Decoder from a `file:` keyword (filename String, a Data,
// or a Stream). On success returns a retained Decoder (caller releases); on
// failure leaves an mruby exception pending and returns nullptr.
static love::sound::Decoder *buildDecoder(mrb_state *mrb, mrb_value file, love::sound::Decoder::StreamSource source)
{
	using namespace love::sound;

	Sound *snd = Module::getInstance<Sound>(Module::M_SOUND);
	if (snd == nullptr)
	{
		mrb_raise(mrb, E_RUNTIME_ERROR, "The sound module must be loaded to create a Source from a file.");
		return nullptr;
	}

	Stream *stream = nullptr;
	if (mrbx_istype<Stream>(mrb, file))
	{
		stream = mrbx_checktype<Stream>(mrb, file);
		stream->retain();
	}
	else if (mrbx_istype<Data>(mrb, file))
	{
		Data *d = mrbx_checktype<Data>(mrb, file);
		if (mrbx_catchexcept(mrb, [&]() { stream = new love::data::DataStream(d); }))
			return nullptr;
	}
	else
	{
		std::string filename = mrbx_checkstring(mrb, file);
		auto fs = Module::getInstance<love::filesystem::Filesystem>(Module::M_FILESYSTEM);
		if (fs == nullptr)
		{
			mrb_raise(mrb, E_RUNTIME_ERROR, "The filesystem module must be loaded to open an audio file by name.");
			return nullptr;
		}
		if (source == Decoder::STREAM_FILE)
		{
			if (mrbx_catchexcept(mrb, [&]() {
				stream = fs->openFile(filename.c_str(), love::filesystem::File::MODE_READ);
			}))
				return nullptr;
		}
		else
		{
			if (mrbx_catchexcept(mrb, [&]() {
				StrongRef<love::filesystem::FileData> data(fs->read(filename.c_str()), Acquire::NORETAIN);
				stream = new love::data::DataStream(data);
			}))
				return nullptr;
		}
	}

	Decoder *dec = nullptr;
	bool err = mrbx_catchexcept(mrb, [&]() { dec = snd->newDecoder(stream, Decoder::DEFAULT_BUFFER_SIZE); });
	stream->release();
	if (err)
		return nullptr;
	return dec;
}

static mrb_value w_getActiveSourceCount(mrb_state *mrb, mrb_value self)
{
	(void) self;
	return mrbx_integer(mrb, instance()->getActiveSourceCount());
}

static mrb_value w_newSource(mrb_state *mrb, mrb_value self)
{
	(void) self;
	using namespace love::sound;

	mrb_value v[4];
	mrbx_get_kwargs(mrb, {"file", "type", "decoder", "sound_data"}, 0, v);

	Source::Type stype = Source::TYPE_STREAM;
	if (!mrb_undef_p(v[1]))
	{
		std::string ts = mrbx_checkstring(mrb, v[1]);
		if (!Source::getConstant(ts.c_str(), stype))
		{
			mrb_raisef(mrb, E_ARGUMENT_ERROR, "invalid source type: %s", ts.c_str());
			return mrb_nil_value();
		}
		if (stype == Source::TYPE_QUEUE)
		{
			mrb_raise(mrb, E_ARGUMENT_ERROR, "Cannot create queueable sources using new_source. Use new_queueable_source instead.");
			return mrb_nil_value();
		}
	}

	Source *t = nullptr;

	if (!mrb_undef_p(v[3])) // sound_data form (always static)
	{
		SoundData *sd = mrbx_checktype<SoundData>(mrb, v[3]);
		if (mrbx_catchexcept(mrb, [&]() { t = instance()->newSource(sd); }))
			return mrb_nil_value();
	}
	else if (!mrb_undef_p(v[2])) // decoder form
	{
		Decoder *dec = mrbx_checktype<Decoder>(mrb, v[2]);
		if (stype == Source::TYPE_STATIC)
		{
			Sound *snd = Module::getInstance<Sound>(Module::M_SOUND);
			SoundData *sd = nullptr;
			if (mrbx_catchexcept(mrb, [&]() { sd = snd->newSoundData(dec); }))
				return mrb_nil_value();
			bool err = mrbx_catchexcept(mrb, [&]() { t = instance()->newSource(sd); });
			sd->release();
			if (err)
				return mrb_nil_value();
		}
		else if (mrbx_catchexcept(mrb, [&]() { t = instance()->newSource(dec); }))
			return mrb_nil_value();
	}
	else if (!mrb_undef_p(v[0])) // file form: build a Decoder first
	{
		Decoder::StreamSource ss = (stype == Source::TYPE_STATIC) ? Decoder::STREAM_MEMORY : Decoder::STREAM_FILE;
		Decoder *dec = buildDecoder(mrb, v[0], ss);
		if (dec == nullptr)
			return mrb_nil_value();

		if (stype == Source::TYPE_STATIC)
		{
			Sound *snd = Module::getInstance<Sound>(Module::M_SOUND);
			SoundData *sd = nullptr;
			bool err = mrbx_catchexcept(mrb, [&]() { sd = snd->newSoundData(dec); });
			dec->release();
			if (err)
				return mrb_nil_value();
			err = mrbx_catchexcept(mrb, [&]() { t = instance()->newSource(sd); });
			sd->release();
			if (err)
				return mrb_nil_value();
		}
		else
		{
			bool err = mrbx_catchexcept(mrb, [&]() { t = instance()->newSource(dec); });
			dec->release();
			if (err)
				return mrb_nil_value();
		}
	}
	else
	{
		mrb_raise(mrb, E_ARGUMENT_ERROR, "expected `file:`, `decoder:`, or `sound_data:`");
		return mrb_nil_value();
	}

	mrb_value r = mrbx_pushtype(mrb, t);
	if (t) t->release();
	return r;
}

static mrb_value w_newQueueableSource(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[4];
	mrbx_get_kwargs(mrb, {"sample_rate", "bit_depth", "channels", "buffers"}, 3, v);

	int samplerate = mrbx_checkint(mrb, v[0]);
	int bitdepth = mrbx_checkint(mrb, v[1]);
	int channels = mrbx_checkint(mrb, v[2]);
	int buffers = mrbx_optint(mrb, v[3], 0);

	Source *t = nullptr;
	if (mrbx_catchexcept(mrb, [&]() { t = instance()->newSource(samplerate, bitdepth, channels, buffers); }))
		return mrb_nil_value();

	mrb_value r = mrbx_pushtype(mrb, t);
	if (t) t->release();
	return r;
}

static mrb_value w_play(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[2];
	mrbx_get_kwargs(mrb, {"source", "sources"}, 0, v);

	bool r = false;
	if (!mrb_undef_p(v[1]))
		r = instance()->play(readSources(mrb, v[1]));
	else if (!mrb_undef_p(v[0]))
		r = instance()->play(mrbx_checktype<Source>(mrb, v[0]));
	else
		mrb_raise(mrb, E_ARGUMENT_ERROR, "expected `source:` or `sources:`");
	return mrbx_boolean(mrb, r);
}

static mrb_value w_stop(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[2];
	mrbx_get_kwargs(mrb, {"source", "sources"}, 0, v);

	if (!mrb_undef_p(v[1]))
		instance()->stop(readSources(mrb, v[1]));
	else if (!mrb_undef_p(v[0]))
		instance()->stop(mrbx_checktype<Source>(mrb, v[0]));
	else
		instance()->stop();
	return mrb_nil_value();
}

static mrb_value w_pause(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[2];
	mrbx_get_kwargs(mrb, {"source", "sources"}, 0, v);

	if (!mrb_undef_p(v[1]))
	{
		instance()->pause(readSources(mrb, v[1]));
		return mrb_nil_value();
	}
	if (!mrb_undef_p(v[0]))
	{
		instance()->pause(mrbx_checktype<Source>(mrb, v[0]));
		return mrb_nil_value();
	}

	std::vector<Source *> paused = instance()->pause();
	mrb_value arr = mrb_ary_new_capa(mrb, (mrb_int) paused.size());
	for (auto s : paused)
		mrb_ary_push(mrb, arr, mrbx_pushtype(mrb, s));
	return arr;
}

static mrb_value w_setVolume(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[1];
	mrbx_get_kwargs(mrb, {"volume"}, 1, v);
	instance()->setVolume((float) mrbx_checknumber(mrb, v[0]));
	return mrb_nil_value();
}

static mrb_value w_getVolume(mrb_state *mrb, mrb_value self)
{
	(void) self;
	return mrbx_number(mrb, instance()->getVolume());
}

static mrb_value w_setPosition(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[3];
	mrbx_get_kwargs(mrb, {"x", "y", "z"}, 2, v);
	float p[3] = { (float) mrbx_checknumber(mrb, v[0]), (float) mrbx_checknumber(mrb, v[1]), (float) mrbx_optnumber(mrb, v[2], 0.0) };
	instance()->setPosition(p);
	return mrb_nil_value();
}

static mrb_value w_getPosition(mrb_state *mrb, mrb_value self)
{
	(void) self;
	float p[3];
	instance()->getPosition(p);
	mrb_value h = mrb_hash_new(mrb);
	hset(mrb, h, "x", mrbx_number(mrb, p[0]));
	hset(mrb, h, "y", mrbx_number(mrb, p[1]));
	hset(mrb, h, "z", mrbx_number(mrb, p[2]));
	return h;
}

static mrb_value w_setOrientation(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[6];
	mrbx_get_kwargs(mrb, {"fx", "fy", "fz", "ux", "uy", "uz"}, 6, v);
	float o[6];
	for (int i = 0; i < 6; i++)
		o[i] = (float) mrbx_checknumber(mrb, v[i]);
	instance()->setOrientation(o);
	return mrb_nil_value();
}

static mrb_value w_getOrientation(mrb_state *mrb, mrb_value self)
{
	(void) self;
	float o[6];
	instance()->getOrientation(o);
	mrb_value h = mrb_hash_new(mrb);
	const char *keys[6] = { "fx", "fy", "fz", "ux", "uy", "uz" };
	for (int i = 0; i < 6; i++)
		hset(mrb, h, keys[i], mrbx_number(mrb, o[i]));
	return h;
}

static mrb_value w_setVelocity(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[3];
	mrbx_get_kwargs(mrb, {"x", "y", "z"}, 2, v);
	float p[3] = { (float) mrbx_checknumber(mrb, v[0]), (float) mrbx_checknumber(mrb, v[1]), (float) mrbx_optnumber(mrb, v[2], 0.0) };
	instance()->setVelocity(p);
	return mrb_nil_value();
}

static mrb_value w_getVelocity(mrb_state *mrb, mrb_value self)
{
	(void) self;
	float p[3];
	instance()->getVelocity(p);
	mrb_value h = mrb_hash_new(mrb);
	hset(mrb, h, "x", mrbx_number(mrb, p[0]));
	hset(mrb, h, "y", mrbx_number(mrb, p[1]));
	hset(mrb, h, "z", mrbx_number(mrb, p[2]));
	return h;
}

static mrb_value w_setDopplerScale(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[1];
	mrbx_get_kwargs(mrb, {"scale"}, 1, v);
	instance()->setDopplerScale((float) mrbx_checknumber(mrb, v[0]));
	return mrb_nil_value();
}

static mrb_value w_getDopplerScale(mrb_state *mrb, mrb_value self)
{
	(void) self;
	return mrbx_number(mrb, instance()->getDopplerScale());
}

static mrb_value w_setDistanceModel(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[1];
	mrbx_get_kwargs(mrb, {"model"}, 1, v);
	std::string ms = mrbx_checkstring(mrb, v[0]);
	Audio::DistanceModel model;
	if (!Audio::getConstant(ms.c_str(), model))
		mrb_raisef(mrb, E_ARGUMENT_ERROR, "invalid distance model: %s", ms.c_str());
	instance()->setDistanceModel(model);
	return mrb_nil_value();
}

static mrb_value w_getDistanceModel(mrb_state *mrb, mrb_value self)
{
	(void) self;
	const char *str = nullptr;
	if (!Audio::getConstant(instance()->getDistanceModel(), str))
		return mrb_nil_value();
	return mrbx_string(mrb, str);
}

static mrb_value w_getRecordingDevices(mrb_state *mrb, mrb_value self)
{
	(void) self;
	const std::vector<RecordingDevice *> &devices = instance()->getRecordingDevices();
	mrb_value arr = mrb_ary_new_capa(mrb, (mrb_int) devices.size());
	for (auto d : devices)
		mrb_ary_push(mrb, arr, mrbx_pushtype(mrb, d));
	return arr;
}

static mrb_value w_setEffect(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[2];
	mrbx_get_kwargs(mrb, {"name", "settings"}, 1, v);
	std::string name = mrbx_checkstring(mrb, v[0]);

	if (mrb_undef_p(v[1]) || mrb_nil_p(v[1]) || mrb_false_p(v[1]))
		return mrbx_boolean(mrb, instance()->unsetEffect(name.c_str()));

	if (!mrb_hash_p(v[1]))
		mrb_raise(mrb, E_ARGUMENT_ERROR, "`settings:` must be a Hash describing the effect.");

	std::map<Effect::Parameter, float> params;
	readEffect(mrb, v[1], params);

	bool ok = false;
	mrbx_catchexcept(mrb, [&]() { ok = instance()->setEffect(name.c_str(), params); });
	return mrbx_boolean(mrb, ok);
}

static mrb_value w_getEffect(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[1];
	mrbx_get_kwargs(mrb, {"name"}, 1, v);
	std::string name = mrbx_checkstring(mrb, v[0]);

	std::map<Effect::Parameter, float> params;
	if (!instance()->getEffect(name.c_str(), params))
		return mrb_nil_value();
	return writeEffect(mrb, params);
}

static mrb_value w_getActiveEffects(mrb_state *mrb, mrb_value self)
{
	(void) self;
	std::vector<std::string> list;
	instance()->getActiveEffects(list);
	return pushStringList(mrb, list);
}

static mrb_value w_getMaxSceneEffects(mrb_state *mrb, mrb_value self)
{
	(void) self;
	return mrbx_integer(mrb, instance()->getMaxSceneEffects());
}

static mrb_value w_getMaxSourceEffects(mrb_state *mrb, mrb_value self)
{
	(void) self;
	return mrbx_integer(mrb, instance()->getMaxSourceEffects());
}

static mrb_value w_isEffectsSupported(mrb_state *mrb, mrb_value self)
{
	(void) self;
	return mrbx_boolean(mrb, instance()->isEFXsupported());
}

static mrb_value w_setOutputSpatialization(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[2];
	mrbx_get_kwargs(mrb, {"enable", "filter"}, 1, v);
	bool enable = mrbx_checkboolean(mrb, v[0]);
	std::string filter = mrbx_optstring(mrb, v[1], "");
	bool ok = instance()->setOutputSpatialization(enable, filter.empty() ? nullptr : filter.c_str());
	return mrbx_boolean(mrb, ok);
}

static mrb_value w_getOutputSpatialization(mrb_state *mrb, mrb_value self)
{
	(void) self;
	const char *filter = nullptr;
	bool enabled = instance()->getOutputSpatialization(filter);
	mrb_value h = mrb_hash_new(mrb);
	hset(mrb, h, "enabled", mrbx_boolean(mrb, enabled));
	hset(mrb, h, "filter", filter != nullptr ? mrbx_string(mrb, filter) : mrb_nil_value());
	return h;
}

static mrb_value w_getOutputSpatializationFilters(mrb_state *mrb, mrb_value self)
{
	(void) self;
	std::vector<std::string> filters;
	instance()->getOutputSpatializationFilters(filters);
	return pushStringList(mrb, filters);
}

static mrb_value w_setMixWithSystem(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[1];
	mrbx_get_kwargs(mrb, {"mix"}, 1, v);
	return mrbx_boolean(mrb, Audio::setMixWithSystem(mrbx_checkboolean(mrb, v[0])));
}

static mrb_value w_getPlaybackDevice(mrb_state *mrb, mrb_value self)
{
	(void) self;
	std::string device;
	if (mrbx_catchexcept(mrb, [&]() { device = instance()->getPlaybackDevice(); }))
		return mrb_nil_value();
	return mrbx_string(mrb, device);
}

static mrb_value w_getPlaybackDevices(mrb_state *mrb, mrb_value self)
{
	(void) self;
	std::vector<std::string> list;
	if (mrbx_catchexcept(mrb, [&]() { instance()->getPlaybackDevices(list); }))
		return mrb_nil_value();
	return pushStringList(mrb, list);
}

// set_playback_device(name: <opt, nil resets to default>) -> bool. Mirrors the
// Lua wrapper: returns false (rather than raising) when the device can't be set.
static mrb_value w_setPlaybackDevice(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[1];
	mrbx_get_kwargs(mrb, {"name"}, 0, v);
	std::string name = mrbx_optstring(mrb, v[0], "");

	bool ok = true;
	try
	{
		instance()->setPlaybackDevice(name.empty() ? nullptr : name.c_str());
	}
	catch (love::Exception &)
	{
		ok = false;
	}
	return mrbx_boolean(mrb, ok);
}

static const MrbReg functions[] =
{
	{ "get_active_source_count",          w_getActiveSourceCount,          MRB_ARGS_NONE() },
	{ "new_source",                       w_newSource,                     MRB_ARGS_KEY(4, 0) },
	{ "new_queueable_source",             w_newQueueableSource,            MRB_ARGS_KEY(4, 0) },
	{ "play",                             w_play,                          MRB_ARGS_KEY(2, 0) },
	{ "stop",                             w_stop,                          MRB_ARGS_KEY(2, 0) },
	{ "pause",                            w_pause,                         MRB_ARGS_KEY(2, 0) },
	{ "set_volume",                       w_setVolume,                     MRB_ARGS_KEY(1, 0) },
	{ "get_volume",                       w_getVolume,                     MRB_ARGS_NONE() },
	{ "set_position",                     w_setPosition,                   MRB_ARGS_KEY(3, 0) },
	{ "get_position",                     w_getPosition,                   MRB_ARGS_NONE() },
	{ "set_orientation",                  w_setOrientation,                MRB_ARGS_KEY(6, 0) },
	{ "get_orientation",                  w_getOrientation,                MRB_ARGS_NONE() },
	{ "set_velocity",                     w_setVelocity,                   MRB_ARGS_KEY(3, 0) },
	{ "get_velocity",                     w_getVelocity,                   MRB_ARGS_NONE() },
	{ "set_doppler_scale",                w_setDopplerScale,               MRB_ARGS_KEY(1, 0) },
	{ "get_doppler_scale",                w_getDopplerScale,               MRB_ARGS_NONE() },
	{ "set_distance_model",               w_setDistanceModel,              MRB_ARGS_KEY(1, 0) },
	{ "get_distance_model",               w_getDistanceModel,              MRB_ARGS_NONE() },
	{ "get_recording_devices",            w_getRecordingDevices,           MRB_ARGS_NONE() },
	{ "set_effect",                       w_setEffect,                     MRB_ARGS_KEY(2, 0) },
	{ "get_effect",                       w_getEffect,                     MRB_ARGS_KEY(1, 0) },
	{ "get_active_effects",               w_getActiveEffects,              MRB_ARGS_NONE() },
	{ "get_max_scene_effects",            w_getMaxSceneEffects,            MRB_ARGS_NONE() },
	{ "get_max_source_effects",           w_getMaxSourceEffects,           MRB_ARGS_NONE() },
	{ "effects_supported?",               w_isEffectsSupported,            MRB_ARGS_NONE() },
	{ "set_output_spatialization",        w_setOutputSpatialization,       MRB_ARGS_KEY(2, 0) },
	{ "get_output_spatialization",        w_getOutputSpatialization,       MRB_ARGS_NONE() },
	{ "get_output_spatialization_filters", w_getOutputSpatializationFilters, MRB_ARGS_NONE() },
	{ "set_mix_with_system",              w_setMixWithSystem,              MRB_ARGS_KEY(1, 0) },
	{ "get_playback_device",              w_getPlaybackDevice,             MRB_ARGS_NONE() },
	{ "get_playback_devices",             w_getPlaybackDevices,            MRB_ARGS_NONE() },
	{ "set_playback_device",              w_setPlaybackDevice,             MRB_ARGS_KEY(1, 0) },
	{ nullptr, nullptr, 0 }
};

// Equivalent of luaopen_love_audio: creates the module instance (real OpenAL,
// null fallback), registers Love::Audio plus the Love::Source and
// Love::RecordingDevice types.
extern "C" void mrb_love_audio_init(mrb_state *mrb)
{
	Audio *inst = instance();

	if (inst == nullptr)
	{
		// Try OpenAL first.
		try { inst = new love::audio::openal::Audio(); }
		catch (love::Exception &) { inst = nullptr; }
	}
	else
		inst->retain();

	if (inst == nullptr)
	{
		// Fall back to null audio.
		try { inst = new love::audio::null::Audio(); }
		catch (love::Exception &) { inst = nullptr; }
	}

	if (inst == nullptr)
	{
		mrb_raise(mrb, E_RUNTIME_ERROR, "Could not open any audio module.");
		return;
	}

	WrappedModule w;
	w.module = inst;
	w.name = "Audio";
	w.type = &Module::type;
	w.functions = functions;

	mrbx_register_module(mrb, w);
	mrbx_register_type(mrb, Source::type, sourceFunctions);
	mrbx_register_type(mrb, RecordingDevice::type, recordingDeviceFunctions);
}

} // audio
} // love
