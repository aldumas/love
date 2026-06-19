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
// Backend note: like the other input/window modules, this is a LEAN backend.
// The real keyboard/sdl/Keyboard.cpp depends on the 621-line Keyboard.h key and
// scancode enum maps and the window module. Instead, HarnessKeyboard is a plain
// love::Module that resolves key/scancode names through SDL's own name lookup
// functions (SDL_GetKeyFromName / SDL_GetScancodeFromName) -- symmetric with the
// lean event backend, which produces those same names via SDL_GetKeyName /
// SDL_GetScancodeName for keypressed/keyreleased events. Swap for the full
// sdl::Keyboard once the key-constant tables are ported; the Ruby API is the same.

#include "common/config.h"
#include "common/mrb_runtime.h"
#include "common/Module.h"
#include "window/Window.h"

#include <SDL3/SDL.h>

#include <string>

namespace love
{
namespace keyboard
{

// TODO(mruby): lean backend — plain Module using SDL name lookups instead of
// Keyboard.h enum tables; swap for keyboard/sdl/Keyboard.cpp (PORTING.md §B)
class HarnessKeyboard : public love::Module
{
public:

	HarnessKeyboard()
		: love::Module(M_KEYBOARD, "love.keyboard.harness")
	{
	}

	bool isDown(const std::string &keyname) const
	{
		SDL_Keycode kc = SDL_GetKeyFromName(keyname.c_str());
		if (kc == SDLK_UNKNOWN)
			return false;
		return isScancodeDownRaw(SDL_GetScancodeFromKey(kc, nullptr));
	}

	bool isScancodeDown(const std::string &scancodename) const
	{
		return isScancodeDownRaw(SDL_GetScancodeFromName(scancodename.c_str()));
	}

	// "a" -> its physical scancode name, e.g. on a US layout still "a".
	std::string getScancodeFromKey(const std::string &keyname) const
	{
		SDL_Keycode kc = SDL_GetKeyFromName(keyname.c_str());
		const char *name = SDL_GetScancodeName(SDL_GetScancodeFromKey(kc, nullptr));
		return name != nullptr ? name : "unknown";
	}

	std::string getKeyFromScancode(const std::string &scancodename) const
	{
		SDL_Scancode sc = SDL_GetScancodeFromName(scancodename.c_str());
		const char *name = SDL_GetKeyName(SDL_GetKeyFromScancode(sc, SDL_KMOD_NONE, false));
		return name != nullptr ? name : "unknown";
	}

	// TODO(mruby): stored state only — lean event backend always forwards key
	// repeats regardless of this flag (see PORTING.md §A)
	void setKeyRepeat(bool enable) { keyRepeat = enable; }
	bool hasKeyRepeat() const { return keyRepeat; }

	void setTextInput(bool enable)
	{
		SDL_Window *w = windowHandle();
		if (w == nullptr)
			return;
		if (enable)
			SDL_StartTextInput(w);
		else
			SDL_StopTextInput(w);
	}

	void setTextInput(bool enable, double x, double y, double w, double h)
	{
		SDL_Window *win = windowHandle();
		if (win == nullptr)
			return;
		SDL_Rect rect = {(int) x, (int) y, (int) w, (int) h};
		SDL_SetTextInputArea(win, &rect, 0);
		setTextInput(enable);
	}

	bool hasTextInput() const
	{
		SDL_Window *w = windowHandle();
		return w != nullptr && SDL_TextInputActive(w);
	}

	bool hasScreenKeyboard() const { return SDL_HasScreenKeyboardSupport(); }

	bool isScreenKeyboardVisible() const
	{
		SDL_Window *w = windowHandle();
		return w != nullptr && SDL_ScreenKeyboardShown(w);
	}

	bool isModifierActive(const std::string &name) const
	{
		SDL_Keymod mod = SDL_GetModState();
		if (name == "ctrl")       return (mod & SDL_KMOD_CTRL)  != 0;
		if (name == "shift")      return (mod & SDL_KMOD_SHIFT) != 0;
		if (name == "alt")        return (mod & SDL_KMOD_ALT)   != 0;
		if (name == "gui")        return (mod & SDL_KMOD_GUI)   != 0;
		if (name == "capslock")   return (mod & SDL_KMOD_CAPS)  != 0;
		if (name == "numlock")    return (mod & SDL_KMOD_NUM)   != 0;
		if (name == "scrolllock") return (mod & SDL_KMOD_SCROLL) != 0;
		return false;
	}

private:

	static SDL_Window *windowHandle()
	{
		auto win = Module::getInstance<love::window::Window>(M_WINDOW);
		return win != nullptr ? (SDL_Window *) win->getHandle() : nullptr;
	}

	static bool isScancodeDownRaw(SDL_Scancode sc)
	{
		if (sc == SDL_SCANCODE_UNKNOWN)
			return false;
		int numkeys = 0;
		const bool *state = SDL_GetKeyboardState(&numkeys);
		return state != nullptr && (int) sc < numkeys && state[sc];
	}

	bool keyRepeat = false;

}; // HarnessKeyboard

#define instance() (Module::getInstance<HarnessKeyboard>(Module::M_KEYBOARD))

// =========================================================================
// Love::Keyboard module functions
// =========================================================================

// Runs pred over a key kwarg that is either a single name string or an Array of
// names, returning true if any matches (love.keyboard.isDown semantics).
template <typename Pred>
static bool anyMatch(mrb_state *mrb, mrb_value v, Pred pred)
{
	if (mrb_array_p(v))
	{
		mrb_int n = RARRAY_LEN(v);
		for (mrb_int i = 0; i < n; i++)
			if (pred(mrbx_checkstring(mrb, mrb_ary_ref(mrb, v, i))))
				return true;
		return false;
	}
	return pred(mrbx_checkstring(mrb, v));
}

static mrb_value w_down(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[1];
	mrbx_get_kwargs(mrb, {"key"}, 1, v);
	bool down = anyMatch(mrb, v[0], [](const std::string &n) { return instance()->isDown(n); });
	return mrbx_boolean(mrb, down);
}

static mrb_value w_scancode_down(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[1];
	mrbx_get_kwargs(mrb, {"scancode"}, 1, v);
	bool down = anyMatch(mrb, v[0], [](const std::string &n) { return instance()->isScancodeDown(n); });
	return mrbx_boolean(mrb, down);
}

static mrb_value w_get_scancode_from_key(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[1];
	mrbx_get_kwargs(mrb, {"key"}, 1, v);
	return mrbx_string(mrb, instance()->getScancodeFromKey(mrbx_checkstring(mrb, v[0])));
}

static mrb_value w_get_key_from_scancode(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[1];
	mrbx_get_kwargs(mrb, {"scancode"}, 1, v);
	return mrbx_string(mrb, instance()->getKeyFromScancode(mrbx_checkstring(mrb, v[0])));
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

static mrb_value w_modifier_active(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[1];
	mrbx_get_kwargs(mrb, {"key"}, 1, v);
	return mrbx_boolean(mrb, instance()->isModifierActive(mrbx_checkstring(mrb, v[0])));
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
	Module *inst = Module::getInstance<Module>(Module::M_KEYBOARD);
	if (inst == nullptr)
		inst = new HarnessKeyboard();
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
