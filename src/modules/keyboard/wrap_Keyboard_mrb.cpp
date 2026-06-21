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

// mruby port of wrap_Keyboard.cpp.
//
// API shape: a Love::Keyboard module, snake_case names, keyword arguments. Lua
// predicate names map to Ruby `?` methods:
//
//   love.keyboard.isDown("a", "b")  -> Love::Keyboard.down?(key: ["a", "b"])
//   love.keyboard.isDown("space")   -> Love::Keyboard.down?(key: "space")
//   love.keyboard.hasTextInput()    -> Love::Keyboard.text_input?
//
// Backend: the **real** love::keyboard::sdl::Keyboard. Key/scancode/modifier
// names are translated to and from the engine's enum tables via
// Keyboard::getConstant (the same tables the event backend uses for key names),
// so down?/scancode_down? and the conversion helpers use LÖVE's canonical key
// names rather than SDL's.

#include "common/config.h"
#include "common/mrb_runtime.h"
#include "common/Module.h"

#include "Keyboard.h"
#include "sdl/Keyboard.h"

#include <string>
#include <vector>

namespace love
{
namespace keyboard
{

#define instance() (Module::getInstance<Keyboard>(Module::M_KEYBOARD))

// Applies fn to a name kwarg that is either a single String or an Array of them.
template <typename F>
static void eachName(mrb_state *mrb, mrb_value v, F fn)
{
	if (mrb_array_p(v))
	{
		mrb_int n = RARRAY_LEN(v);
		for (mrb_int i = 0; i < n; i++)
			fn(mrbx_checkstring(mrb, mrb_ary_ref(mrb, v, i)));
	}
	else
		fn(mrbx_checkstring(mrb, v));
}

static Keyboard::Key checkKey(mrb_state *mrb, const std::string &name)
{
	Keyboard::Key k;
	if (!Keyboard::getConstant(name.c_str(), k))
		mrb_raisef(mrb, E_ARGUMENT_ERROR, "invalid key constant: %s", name.c_str());
	return k;
}

static Keyboard::Scancode checkScancode(mrb_state *mrb, const std::string &name)
{
	Keyboard::Scancode s;
	if (!Keyboard::getConstant(name.c_str(), s))
		mrb_raisef(mrb, E_ARGUMENT_ERROR, "invalid scancode: %s", name.c_str());
	return s;
}

// down?(key:) — key is a key-constant name or an Array of them; true if any held.
static mrb_value w_down(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[1];
	mrbx_get_kwargs(mrb, {"key"}, 1, v);

	std::vector<Keyboard::Key> keys;
	eachName(mrb, v[0], [&](const std::string &n) { keys.push_back(checkKey(mrb, n)); });
	return mrbx_boolean(mrb, instance()->isDown(keys));
}

// scancode_down?(scancode:) — a scancode name or an Array; true if any held.
static mrb_value w_scancode_down(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[1];
	mrbx_get_kwargs(mrb, {"scancode"}, 1, v);

	std::vector<Keyboard::Scancode> scancodes;
	eachName(mrb, v[0], [&](const std::string &n) { scancodes.push_back(checkScancode(mrb, n)); });
	return mrbx_boolean(mrb, instance()->isScancodeDown(scancodes));
}

static mrb_value w_get_scancode_from_key(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[1];
	mrbx_get_kwargs(mrb, {"key"}, 1, v);
	Keyboard::Key key = checkKey(mrb, mrbx_checkstring(mrb, v[0]));

	Keyboard::Scancode scancode = instance()->getScancodeFromKey(key);
	const char *str;
	if (!Keyboard::getConstant(scancode, str))
		mrb_raise(mrb, E_RUNTIME_ERROR, "Unknown scancode.");
	return mrbx_string(mrb, str);
}

static mrb_value w_get_key_from_scancode(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[1];
	mrbx_get_kwargs(mrb, {"scancode"}, 1, v);
	Keyboard::Scancode scancode = checkScancode(mrb, mrbx_checkstring(mrb, v[0]));

	Keyboard::Key key = instance()->getKeyFromScancode(scancode);
	const char *str;
	if (!Keyboard::getConstant(key, str))
		mrb_raise(mrb, E_RUNTIME_ERROR, "Unknown key constant.");
	return mrbx_string(mrb, str);
}

static mrb_value w_set_key_repeat(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[1];
	mrbx_get_kwargs(mrb, {"enable"}, 1, v);
	instance()->setKeyRepeat(mrbx_checkboolean(mrb, v[0]));
	return mrb_nil_value();
}

static mrb_value w_key_repeat(mrb_state *mrb, mrb_value self)
{
	(void) self;
	return mrbx_boolean(mrb, instance()->hasKeyRepeat());
}

static mrb_value w_set_text_input(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[5];
	mrbx_get_kwargs(mrb, {"enable", "x", "y", "width", "height"}, 1, v);
	bool enable = mrbx_checkboolean(mrb, v[0]);

	if (mrb_undef_p(v[1]))
		instance()->setTextInput(enable);
	else
		instance()->setTextInput(enable, mrbx_checknumber(mrb, v[1]), mrbx_checknumber(mrb, v[2]),
			mrbx_checknumber(mrb, v[3]), mrbx_checknumber(mrb, v[4]));
	return mrb_nil_value();
}

static mrb_value w_text_input(mrb_state *mrb, mrb_value self)
{
	(void) self;
	return mrbx_boolean(mrb, instance()->hasTextInput());
}

static mrb_value w_has_screen_keyboard(mrb_state *mrb, mrb_value self)
{
	(void) self;
	return mrbx_boolean(mrb, instance()->hasScreenKeyboard());
}

static mrb_value w_screen_keyboard_visible(mrb_state *mrb, mrb_value self)
{
	(void) self;
	return mrbx_boolean(mrb, instance()->isScreenKeyboardVisible());
}

// modifier_active?(key:) — one of the sticky modifiers (numlock/capslock/
// scrolllock/mode), matching love.keyboard.isModifierActive.
static mrb_value w_modifier_active(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[1];
	mrbx_get_kwargs(mrb, {"key"}, 1, v);
	std::string name = mrbx_checkstring(mrb, v[0]);

	Keyboard::ModifierKey key;
	if (!Keyboard::getConstant(name.c_str(), key))
		mrb_raisef(mrb, E_ARGUMENT_ERROR, "invalid modifier key: %s", name.c_str());
	return mrbx_boolean(mrb, instance()->isModifierActive(key));
}

static const MrbReg functions[] =
{
	{ "down?",                   w_down,                   MRB_ARGS_KEY(1, 0) },
	{ "scancode_down?",          w_scancode_down,          MRB_ARGS_KEY(1, 0) },
	{ "get_scancode_from_key",   w_get_scancode_from_key,  MRB_ARGS_KEY(1, 0) },
	{ "get_key_from_scancode",   w_get_key_from_scancode,  MRB_ARGS_KEY(1, 0) },
	{ "set_key_repeat",          w_set_key_repeat,         MRB_ARGS_KEY(1, 0) },
	{ "key_repeat?",             w_key_repeat,             MRB_ARGS_NONE() },
	{ "set_text_input",          w_set_text_input,         MRB_ARGS_KEY(5, 0) },
	{ "text_input?",             w_text_input,             MRB_ARGS_NONE() },
	{ "has_screen_keyboard?",    w_has_screen_keyboard,    MRB_ARGS_NONE() },
	{ "screen_keyboard_visible?", w_screen_keyboard_visible, MRB_ARGS_NONE() },
	{ "modifier_active?",        w_modifier_active,        MRB_ARGS_KEY(1, 0) },
	{ nullptr, nullptr, 0 }
};

extern "C" void mrb_love_keyboard_init(mrb_state *mrb)
{
	Keyboard *inst = instance();
	if (inst == nullptr)
		inst = new love::keyboard::sdl::Keyboard();
	else
		inst->retain();

	WrappedModule w;
	w.module = inst;
	w.name = "Keyboard";
	w.type = &Module::type;
	w.functions = functions;

	mrbx_register_module(mrb, w);
}

} // keyboard
} // love
