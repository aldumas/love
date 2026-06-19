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

// mruby port of wrap_System.cpp.
//
// API shape: methods live on the Love::System module, use snake_case names with
// keyword arguments, and `?`-suffixed predicates. For example the Lua
// `love.system.getClipboardText()` becomes `Love::System.get_clipboard_text`,
// and `love.system.hasBackgroundMusic()` becomes `Love::System.background_music?`.
// The Lua multi-value return of getPowerInfo becomes a Hash with symbol keys
// (`:state`, `:percent`, `:seconds`), with nil for values that can't be
// determined.

#include "common/config.h"
#include "common/mrb_runtime.h"
#include "System.h"
#include "sdl/System.h"

namespace love
{
namespace system
{

#define instance() (Module::getInstance<System>(Module::M_SYSTEM))

// Love::System.get_os -> the name of the current operating system.
static mrb_value w_getOS(mrb_state *mrb, mrb_value self)
{
	(void) self;
	return mrbx_string(mrb, System::getOS());
}

// Love::System.get_processor_count -> number of reported logical CPU cores.
static mrb_value w_getProcessorCount(mrb_state *mrb, mrb_value self)
{
	(void) self;
	return mrbx_integer(mrb, instance()->getProcessorCount());
}

// Love::System.get_memory_size -> amount of system RAM in MiB.
static mrb_value w_getMemorySize(mrb_state *mrb, mrb_value self)
{
	(void) self;
	return mrbx_integer(mrb, instance()->getMemorySize());
}

// Love::System.set_clipboard_text(text:) -> replaces the clipboard contents.
static mrb_value w_setClipboardText(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[1];
	mrbx_get_kwargs(mrb, {"text"}, 1, v);
	std::string text = mrbx_checkstring(mrb, v[0]);
	mrbx_catchexcept(mrb, [&]() { instance()->setClipboardText(text); });
	return mrb_nil_value();
}

// Love::System.get_clipboard_text -> the current clipboard contents (string).
static mrb_value w_getClipboardText(mrb_state *mrb, mrb_value self)
{
	(void) self;
	std::string text;
	if (mrbx_catchexcept(mrb, [&]() { text = instance()->getClipboardText(); }))
		return mrb_nil_value();
	return mrbx_string(mrb, text);
}

static void hset(mrb_state *mrb, mrb_value h, const char *key, mrb_value val)
{
	mrb_hash_set(mrb, h, mrb_symbol_value(mrb_intern_cstr(mrb, key)), val);
}

// Love::System.get_power_info -> Hash { state:, percent:, seconds: }.
// state is one of "unknown"/"battery"/"nobattery"/"charging"/"charged";
// percent (0-100) and seconds are nil when they can't be determined.
static mrb_value w_getPowerInfo(mrb_state *mrb, mrb_value self)
{
	(void) self;
	int seconds = -1, percent = -1;

	System::PowerState state = instance()->getPowerInfo(seconds, percent);

	const char *str;
	if (!System::getConstant(state, str))
		str = "unknown";

	mrb_value out = mrb_hash_new(mrb);
	hset(mrb, out, "state", mrbx_string(mrb, str));
	hset(mrb, out, "percent", percent >= 0 ? mrbx_integer(mrb, percent) : mrb_nil_value());
	hset(mrb, out, "seconds", seconds >= 0 ? mrbx_integer(mrb, seconds) : mrb_nil_value());
	return out;
}

// Love::System.open_url(url:) -> whether the URL was opened successfully.
static mrb_value w_openURL(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[1];
	mrbx_get_kwargs(mrb, {"url"}, 1, v);
	std::string url = mrbx_checkstring(mrb, v[0]);
	return mrbx_boolean(mrb, instance()->openURL(url));
}

// Love::System.vibrate(seconds:) -> vibrates for the given time (default 0.5s).
static mrb_value w_vibrate(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[1];
	mrbx_get_kwargs(mrb, {"seconds"}, 0, v);
	instance()->vibrate(mrbx_optnumber(mrb, v[0], 0.5));
	return mrb_nil_value();
}

// Love::System.background_music? -> whether the user is playing background music.
static mrb_value w_hasBackgroundMusic(mrb_state *mrb, mrb_value self)
{
	(void) self;
	return mrbx_boolean(mrb, instance()->hasBackgroundMusic());
}

// Love::System.get_preferred_locales -> Array of locale strings (e.g. "en_US"),
// in order of user preference.
static mrb_value w_getPreferredLocales(mrb_state *mrb, mrb_value self)
{
	(void) self;
	std::vector<std::string> locales = instance()->getPreferredLocales();

	mrb_value arr = mrb_ary_new_capa(mrb, (mrb_int) locales.size());
	for (const std::string &str : locales)
		mrb_ary_push(mrb, arr, mrbx_string(mrb, str));

	return arr;
}

static const MrbReg functions[] =
{
	{ "get_os",                w_getOS,                MRB_ARGS_NONE() },
	{ "get_processor_count",   w_getProcessorCount,    MRB_ARGS_NONE() },
	{ "get_memory_size",       w_getMemorySize,        MRB_ARGS_NONE() },
	{ "set_clipboard_text",    w_setClipboardText,     MRB_ARGS_KEY(1, 0) },
	{ "get_clipboard_text",    w_getClipboardText,     MRB_ARGS_NONE() },
	{ "get_power_info",        w_getPowerInfo,         MRB_ARGS_NONE() },
	{ "open_url",              w_openURL,              MRB_ARGS_KEY(1, 0) },
	{ "vibrate",               w_vibrate,              MRB_ARGS_KEY(1, 0) },
	{ "background_music?",     w_hasBackgroundMusic,   MRB_ARGS_NONE() },
	{ "get_preferred_locales", w_getPreferredLocales,  MRB_ARGS_NONE() },
	{ nullptr, nullptr, 0 }
};

// Equivalent of luaopen_love_system: creates the SDL-backed module instance if
// needed and registers Love::System.
extern "C" void mrb_love_system_init(mrb_state *mrb)
{
	System *inst = instance();
	if (inst == nullptr)
		inst = new love::system::sdl::System();
	else
		inst->retain();

	WrappedModule w;
	w.module = inst;
	w.name = "System";
	w.type = &Module::type;
	w.functions = functions;

	mrbx_register_module(mrb, w);
}

} // system
} // love
