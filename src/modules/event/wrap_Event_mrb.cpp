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

// mruby port of wrap_Event.cpp.
//
// API shape: a Love::Event module, snake_case names, keyword arguments. The
// Lua iterator pattern (love.event.poll() returning poll_i) is replaced by the
// Ruby idiom of Love::Event.poll returning an Array of events, each event being
// [name_symbol, *args]:
//
//   love.event.poll()         -> Love::Event.poll      (Array of [:name, *args])
//   for e in love.event.poll  -> Love::Event.poll.each { |name, a, b, c| ... }
//   love.event.push("x", 1)   -> Love::Event.push(name: "x", args: [1])
//   love.event.quit(0)        -> Love::Event.quit(code: 0)
//
// Backend note: LÖVE's full SDL event backend (event/sdl/Event.cpp) pulls in
// the entire input/window/graphics/audio dependency graph to translate every
// SDL event. Until those modules are ported, this file provides a lean backend
// (HarnessEvent) that reuses the platform-independent message queue from
// event/Event.cpp and converts only the window-independent SDL events, using
// SDL directly (no love::keyboard / love::window dependency). When window and
// the input modules land, this can be swapped for the real sdl::Event with no
// change to the Ruby-facing API.

#include "common/config.h"
#include "common/mrb_runtime.h"
#include "common/Variant.h"
#include "common/int.h"
#include "Event.h"

// The input family + keyboard enum tables + window/graphics, used to translate
// the full SDL event set the way event/sdl/Event.cpp does. We deliberately do
// NOT pull in window/sdl/Window.h (the real backend dynamic_casts to it only for
// the live-resize modal-draw hook): that would force the renderer/graphics-
// context window backend (#win-backend). Everything else here works against the
// base Window interface, so the lean window backend is fine.
#include "keyboard/Keyboard.h"
#include "keyboard/sdl/Keyboard.h"
#include "joystick/JoystickModule.h"
#include "joystick/Joystick.h"
#include "joystick/sdl/Joystick.h"
#include "touch/Touch.h"
#include "touch/sdl/Touch.h"
#include "sensor/Sensor.h"
#include "sensor/sdl/Sensor.h"
#include "window/Window.h"
#include "filesystem/Filesystem.h"
#include "filesystem/File.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace love
{

// Defined in the keyboard wrapper: whether repeat keypressed events should be
// forwarded (true if the keyboard module is absent or key repeat is enabled).
namespace keyboard { bool harnessKeyRepeatEnabled(); }

namespace event
{

#define instance() (Module::getInstance<Event>(Module::M_EVENT))

// =========================================================================
// SDL event backend
// =========================================================================

// TODO(mruby) #event-backend: this reproduces event/sdl/Event.cpp's full event
// translation (keyboard/text/mouse/touch/joystick/gamepad/sensor/window/drop/
// system) but keeps the lean pump() and omits the one sdl::Window-only hook
// (the live-resize modal-draw dynamic_cast). A true wholesale swap to
// event/sdl/Event.cpp additionally needs #win-backend (sdl::Window typeinfo for
// that dynamic_cast) and #kbd-backend (a real keyboard::Keyboard instance for
// the key-repeat check). See PORTING.md §B.
class HarnessEvent : public love::event::Event
{
public:

	HarnessEvent()
		: love::event::Event("love.event.harness")
	{
		if (!SDL_InitSubSystem(SDL_INIT_EVENTS))
			throw love::Exception("Could not initialize SDL events subsystem (%s)", SDL_GetError());
	}

	~HarnessEvent() override
	{
		SDL_QuitSubSystem(SDL_INIT_EVENTS);
	}

	void pump(float waitTimeout) override
	{
		SDL_Event e;

		if (waitTimeout != 0.0f)
		{
			int ms = 0;
			if (std::isinf(waitTimeout) || waitTimeout < 0.0f)
				ms = -1; // wait forever
			else
				ms = (int) std::min<double>(2147483647.0, 1000.0 * waitTimeout);

			if (SDL_WaitEventTimeout(&e, ms))
			{
				StrongRef<Message> msg(convert(e), Acquire::NORETAIN);
				if (msg)
					push(msg);
			}
		}
		else
		{
			SDL_PumpEvents();
		}

		while (SDL_PollEvent(&e))
		{
			StrongRef<Message> msg(convert(e), Acquire::NORETAIN);
			if (msg)
				push(msg);
		}
	}

	Message *wait() override
	{
		SDL_Event e;
		if (!SDL_WaitEvent(&e))
			return nullptr;
		return convert(e);
	}

private:

	// --- coordinate helpers (operate on the base Window interface) ---------

	static void windowToDPICoords(love::window::Window *window, double *x, double *y)
	{
		if (window)
			window->windowToDPICoords(x, y);
	}

	static void clampToWindow(love::window::Window *window, double *x, double *y)
	{
		if (window)
			window->clampPositionInWindow(x, y);
	}

	static void normalizedToDPICoords(love::window::Window *window, double *x, double *y)
	{
		double w = 1.0, h = 1.0;
		if (window)
		{
			w = window->getWidth();
			h = window->getHeight();
			window->windowToDPICoords(&w, &h);
		}
		if (x) *x = (*x) * w;
		if (y) *y = (*y) * h;
	}

	// Faithful port of love::event::sdl::Event::convert, minus the sdl::Window
	// live-resize modal-draw hook (which dynamic_casts to window::sdl::Window —
	// the one piece that would force the renderer window backend, #win-backend).
	// Key names come from the real keyboard enum tables; key repeat honours the
	// lean keyboard module via harnessKeyRepeatEnabled() (its instance isn't a
	// keyboard::Keyboard, so getInstance<Keyboard> can't be used here).
	static Message *convert(const SDL_Event &e)
	{
		using namespace love;
		Message *msg = nullptr;
		std::vector<Variant> vargs;
		vargs.reserve(4);

		window::Window *win = Module::getInstance<window::Window>(Module::M_WINDOW);

		keyboard::Keyboard::Key key = keyboard::Keyboard::KEY_UNKNOWN;
		keyboard::Keyboard::Scancode scancode = keyboard::Keyboard::SCANCODE_UNKNOWN;
		const char *txt = nullptr, *txt2 = nullptr;
		touch::Touch::TouchInfo touchinfo = {};
		touch::sdl::Touch *touchmodule = nullptr;
		filesystem::Filesystem *fs = nullptr;
		sensor::Sensor *sensorInstance = nullptr;

		switch (e.type)
		{
		case SDL_EVENT_KEY_DOWN:
			// Drop auto-repeat keypresses when key repeat is disabled (#kbd-keyrepeat).
			if (e.key.repeat != 0 && !keyboard::harnessKeyRepeatEnabled())
				break;
			keyboard::sdl::Keyboard::getConstant(e.key.key, key);
			if (!keyboard::Keyboard::getConstant(key, txt)) txt = "unknown";
			keyboard::sdl::Keyboard::getConstant(e.key.scancode, scancode);
			if (!keyboard::Keyboard::getConstant(scancode, txt2)) txt2 = "unknown";
			vargs.emplace_back(txt, strlen(txt));
			vargs.emplace_back(txt2, strlen(txt2));
			vargs.emplace_back(e.key.repeat != 0);
			msg = new Message("keypressed", vargs);
			break;
		case SDL_EVENT_KEY_UP:
			keyboard::sdl::Keyboard::getConstant(e.key.key, key);
			if (!keyboard::Keyboard::getConstant(key, txt)) txt = "unknown";
			keyboard::sdl::Keyboard::getConstant(e.key.scancode, scancode);
			if (!keyboard::Keyboard::getConstant(scancode, txt2)) txt2 = "unknown";
			vargs.emplace_back(txt, strlen(txt));
			vargs.emplace_back(txt2, strlen(txt2));
			msg = new Message("keyreleased", vargs);
			break;
		case SDL_EVENT_TEXT_INPUT:
			txt = e.text.text;
			vargs.emplace_back(txt, strlen(txt));
			msg = new Message("textinput", vargs);
			break;
		case SDL_EVENT_TEXT_EDITING:
			txt = e.edit.text;
			vargs.emplace_back(txt, strlen(txt));
			vargs.emplace_back((double) e.edit.start);
			vargs.emplace_back((double) e.edit.length);
			msg = new Message("textedited", vargs);
			break;
		case SDL_EVENT_MOUSE_MOTION:
		{
			double x = e.motion.x, y = e.motion.y, xrel = e.motion.xrel, yrel = e.motion.yrel;
			clampToWindow(win, &x, &y);
			windowToDPICoords(win, &x, &y);
			windowToDPICoords(win, &xrel, &yrel);
			vargs.emplace_back(x);
			vargs.emplace_back(y);
			vargs.emplace_back(xrel);
			vargs.emplace_back(yrel);
			vargs.emplace_back(e.motion.which == SDL_TOUCH_MOUSEID);
			msg = new Message("mousemoved", vargs);
			break;
		}
		case SDL_EVENT_MOUSE_BUTTON_DOWN:
		case SDL_EVENT_MOUSE_BUTTON_UP:
		{
			// LÖVE uses button 2 for right and 3 for middle.
			int button = e.button.button;
			if (button == SDL_BUTTON_RIGHT) button = 2;
			else if (button == SDL_BUTTON_MIDDLE) button = 3;

			double px = e.button.x, py = e.button.y;
			clampToWindow(win, &px, &py);
			windowToDPICoords(win, &px, &py);

			vargs.emplace_back(px);
			vargs.emplace_back(py);
			vargs.emplace_back((double) button);
			vargs.emplace_back(e.button.which == SDL_TOUCH_MOUSEID);
			vargs.emplace_back((double) e.button.clicks);
			bool down = e.type == SDL_EVENT_MOUSE_BUTTON_DOWN;
			msg = new Message(down ? "mousepressed" : "mousereleased", vargs);
			break;
		}
		case SDL_EVENT_MOUSE_WHEEL:
			vargs.emplace_back((double) e.wheel.x);
			vargs.emplace_back((double) e.wheel.y);
			txt = e.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? "flipped" : "standard";
			vargs.emplace_back(txt, strlen(txt));
			msg = new Message("wheelmoved", vargs);
			break;
		case SDL_EVENT_FINGER_DOWN:
		case SDL_EVENT_FINGER_UP:
		case SDL_EVENT_FINGER_MOTION:
			touchinfo.id = (int64) e.tfinger.fingerID;
			touchinfo.x = e.tfinger.x;
			touchinfo.y = e.tfinger.y;
			touchinfo.dx = e.tfinger.dx;
			touchinfo.dy = e.tfinger.dy;
			touchinfo.pressure = e.tfinger.pressure;
			touchinfo.deviceType = touch::sdl::Touch::getDeviceType(SDL_GetTouchDeviceType(e.tfinger.touchID));
			touchinfo.mouse = e.tfinger.touchID == SDL_MOUSE_TOUCHID;

			// SDL's coords are normalized to [0, 1]; we want screen coords for touchscreens.
			if (touchinfo.deviceType == touch::Touch::DEVICE_TOUCHSCREEN)
			{
				normalizedToDPICoords(win, &touchinfo.x, &touchinfo.y);
				normalizedToDPICoords(win, &touchinfo.dx, &touchinfo.dy);
			}

			touchmodule = (touch::sdl::Touch *) Module::getInstance("love.touch.sdl");
			if (touchmodule)
				touchmodule->onEvent(e.type, touchinfo);

			if (!touch::Touch::getConstant(touchinfo.deviceType, txt)) txt = "unknown";

			// The id is emitted as lightuserdata; mrbx_pushvariant surfaces it as the
			// same Integer the touch module uses for ids.
			vargs.emplace_back((void *)(intptr_t) touchinfo.id);
			vargs.emplace_back(touchinfo.x);
			vargs.emplace_back(touchinfo.y);
			vargs.emplace_back(touchinfo.dx);
			vargs.emplace_back(touchinfo.dy);
			vargs.emplace_back(touchinfo.pressure);
			vargs.emplace_back(txt, strlen(txt));
			vargs.emplace_back(touchinfo.mouse);

			if (e.type == SDL_EVENT_FINGER_DOWN) txt = "touchpressed";
			else if (e.type == SDL_EVENT_FINGER_UP || e.type == SDL_EVENT_FINGER_CANCELED) txt = "touchreleased";
			else txt = "touchmoved";
			msg = new Message(txt, vargs);
			break;
		case SDL_EVENT_JOYSTICK_BUTTON_DOWN:
		case SDL_EVENT_JOYSTICK_BUTTON_UP:
		case SDL_EVENT_JOYSTICK_AXIS_MOTION:
		case SDL_EVENT_JOYSTICK_HAT_MOTION:
		case SDL_EVENT_JOYSTICK_ADDED:
		case SDL_EVENT_JOYSTICK_REMOVED:
		case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
		case SDL_EVENT_GAMEPAD_BUTTON_UP:
		case SDL_EVENT_GAMEPAD_AXIS_MOTION:
#if defined(LOVE_ENABLE_SENSOR)
		case SDL_EVENT_GAMEPAD_SENSOR_UPDATE:
#endif
			msg = convertJoystickEvent(e);
			break;
		case SDL_EVENT_WINDOW_FOCUS_GAINED:
		case SDL_EVENT_WINDOW_FOCUS_LOST:
		case SDL_EVENT_WINDOW_MOUSE_ENTER:
		case SDL_EVENT_WINDOW_MOUSE_LEAVE:
		case SDL_EVENT_WINDOW_SHOWN:
		case SDL_EVENT_WINDOW_HIDDEN:
		case SDL_EVENT_WINDOW_RESIZED:
		case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
		case SDL_EVENT_WINDOW_MINIMIZED:
		case SDL_EVENT_WINDOW_RESTORED:
		case SDL_EVENT_WINDOW_EXPOSED:
		case SDL_EVENT_WINDOW_OCCLUDED:
			msg = convertWindowEvent(e, win);
			break;
		case SDL_EVENT_DISPLAY_ORIENTATION:
		{
			auto orientation = window::Window::ORIENTATION_UNKNOWN;
			switch ((SDL_DisplayOrientation) e.display.data1)
			{
			case SDL_ORIENTATION_LANDSCAPE: orientation = window::Window::ORIENTATION_LANDSCAPE; break;
			case SDL_ORIENTATION_LANDSCAPE_FLIPPED: orientation = window::Window::ORIENTATION_LANDSCAPE_FLIPPED; break;
			case SDL_ORIENTATION_PORTRAIT: orientation = window::Window::ORIENTATION_PORTRAIT; break;
			case SDL_ORIENTATION_PORTRAIT_FLIPPED: orientation = window::Window::ORIENTATION_PORTRAIT_FLIPPED; break;
			case SDL_ORIENTATION_UNKNOWN: default: orientation = window::Window::ORIENTATION_UNKNOWN; break;
			}
			if (!window::Window::getConstant(orientation, txt)) txt = "unknown";

			int count = 0, displayindex = 0;
			SDL_DisplayID *displays = SDL_GetDisplays(&count);
			for (int i = 0; displays && i < count; i++)
			{
				if (displays[i] == e.display.displayID) { displayindex = i; break; }
			}
			SDL_free(displays);
			vargs.emplace_back((double)(displayindex + 1));
			vargs.emplace_back(txt, strlen(txt));
			msg = new Message("displayrotated", vargs);
			break;
		}
		case SDL_EVENT_DROP_BEGIN:
			msg = new Message("dropbegan");
			break;
		case SDL_EVENT_DROP_COMPLETE:
		{
			double x = e.drop.x, y = e.drop.y;
			windowToDPICoords(win, &x, &y);
			vargs.emplace_back(x);
			vargs.emplace_back(y);
			msg = new Message("dropcompleted", vargs);
			break;
		}
		case SDL_EVENT_DROP_POSITION:
		{
			double x = e.drop.x, y = e.drop.y;
			windowToDPICoords(win, &x, &y);
			vargs.emplace_back(x);
			vargs.emplace_back(y);
			msg = new Message("dropmoved", vargs);
			break;
		}
		case SDL_EVENT_DROP_FILE:
			fs = Module::getInstance<filesystem::Filesystem>(Module::M_FILESYSTEM);
			if (fs != nullptr)
			{
				const char *filepath = e.drop.data;
				fs->allowMountingForPath(filepath);

				double x = e.drop.x, y = e.drop.y;
				windowToDPICoords(win, &x, &y);

				if (fs->isRealDirectory(filepath))
				{
					vargs.emplace_back(filepath, strlen(filepath));
					vargs.emplace_back(x);
					vargs.emplace_back(y);
					msg = new Message("directorydropped", vargs);
				}
				else
				{
					auto *file = fs->openNativeFile(filepath, filesystem::File::MODE_CLOSED);
					vargs.emplace_back(&filesystem::File::type, file);
					vargs.emplace_back(x);
					vargs.emplace_back(y);
					msg = new Message("filedropped", vargs);
					file->release();
				}
			}
			break;
		case SDL_EVENT_QUIT:
		case SDL_EVENT_TERMINATING:
			msg = new Message("quit");
			break;
		case SDL_EVENT_LOW_MEMORY:
			msg = new Message("lowmemory");
			break;
		case SDL_EVENT_LOCALE_CHANGED:
			msg = new Message("localechanged");
			break;
		case SDL_EVENT_SYSTEM_THEME_CHANGED:
			msg = new Message("themechanged");
			break;
		case SDL_EVENT_SENSOR_UPDATE:
			sensorInstance = Module::getInstance<sensor::Sensor>(Module::M_SENSOR);
			if (sensorInstance)
			{
				for (void *s : sensorInstance->getHandles())
				{
					SDL_Sensor *sensor = (SDL_Sensor *) s;
					if (e.sensor.which == SDL_GetSensorID(sensor))
					{
						const char *sensorType;
						if (!sensor::Sensor::getConstant(sensor::sdl::Sensor::convert(SDL_GetSensorType(sensor)), sensorType))
							sensorType = "unknown";
						vargs.emplace_back(sensorType, strlen(sensorType));
						vargs.emplace_back((double) e.sensor.data[0]);
						vargs.emplace_back((double) e.sensor.data[1]);
						vargs.emplace_back((double) e.sensor.data[2]);
						msg = new Message("sensorupdated", vargs);
						break;
					}
				}
			}
			break;
		default:
			break;
		}

		return msg;
	}

	// Faithful port of love::event::sdl::Event::convertJoystickEvent.
	static Message *convertJoystickEvent(const SDL_Event &e)
	{
		using namespace love;
		auto joymodule = Module::getInstance<joystick::JoystickModule>(Module::M_JOYSTICK);
		if (!joymodule)
			return nullptr;

		Message *msg = nullptr;
		std::vector<Variant> vargs;
		vargs.reserve(4);

		love::Type *joysticktype = &joystick::Joystick::type;
		joystick::Joystick *stick = nullptr;
		joystick::Joystick::Hat hat;
		joystick::Joystick::GamepadButton padbutton;
		joystick::Joystick::GamepadAxis padaxis;
		const char *txt;

		switch (e.type)
		{
		case SDL_EVENT_JOYSTICK_BUTTON_DOWN:
		case SDL_EVENT_JOYSTICK_BUTTON_UP:
			stick = joymodule->getJoystickFromID(e.jbutton.which);
			if (!stick) break;
			vargs.emplace_back(joysticktype, stick);
			vargs.emplace_back((double)(e.jbutton.button + 1));
			msg = new Message(e.type == SDL_EVENT_JOYSTICK_BUTTON_DOWN ? "joystickpressed" : "joystickreleased", vargs);
			break;
		case SDL_EVENT_JOYSTICK_AXIS_MOTION:
		{
			stick = joymodule->getJoystickFromID(e.jaxis.which);
			if (!stick) break;
			vargs.emplace_back(joysticktype, stick);
			vargs.emplace_back((double)(e.jaxis.axis + 1));
			float value = joystick::Joystick::clampval(e.jaxis.value / 32768.0f);
			vargs.emplace_back((double) value);
			msg = new Message("joystickaxis", vargs);
			break;
		}
		case SDL_EVENT_JOYSTICK_HAT_MOTION:
			if (!joystick::sdl::Joystick::getConstant(e.jhat.value, hat) || !joystick::Joystick::getConstant(hat, txt))
				break;
			stick = joymodule->getJoystickFromID(e.jhat.which);
			if (!stick) break;
			vargs.emplace_back(joysticktype, stick);
			vargs.emplace_back((double)(e.jhat.hat + 1));
			vargs.emplace_back(txt, strlen(txt));
			msg = new Message("joystickhat", vargs);
			break;
		case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
		case SDL_EVENT_GAMEPAD_BUTTON_UP:
		{
			const auto &b = e.gbutton;
			if (!joystick::sdl::Joystick::getConstant((SDL_GamepadButton) b.button, padbutton)) break;
			if (!joystick::Joystick::getConstant(padbutton, txt)) break;
			stick = joymodule->getJoystickFromID(b.which);
			if (!stick) break;
			vargs.emplace_back(joysticktype, stick);
			vargs.emplace_back(txt, strlen(txt));
			msg = new Message(e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN ? "gamepadpressed" : "gamepadreleased", vargs);
			break;
		}
		case SDL_EVENT_GAMEPAD_AXIS_MOTION:
			if (joystick::sdl::Joystick::getConstant((SDL_GamepadAxis) e.gaxis.axis, padaxis))
			{
				if (!joystick::Joystick::getConstant(padaxis, txt)) break;
				stick = joymodule->getJoystickFromID(e.gaxis.which);
				if (!stick) break;
				vargs.emplace_back(joysticktype, stick);
				vargs.emplace_back(txt, strlen(txt));
				float value = joystick::Joystick::clampval(e.gaxis.value / 32768.0f);
				vargs.emplace_back((double) value);
				msg = new Message("gamepadaxis", vargs);
			}
			break;
		case SDL_EVENT_JOYSTICK_ADDED:
			stick = joymodule->addJoystick(e.jdevice.which);
			if (stick)
			{
				vargs.emplace_back(joysticktype, stick);
				msg = new Message("joystickadded", vargs);
			}
			break;
		case SDL_EVENT_JOYSTICK_REMOVED:
			stick = joymodule->getJoystickFromID(e.jdevice.which);
			if (stick)
			{
				joymodule->removeJoystick(stick);
				vargs.emplace_back(joysticktype, stick);
				msg = new Message("joystickremoved", vargs);
			}
			break;
#if defined(LOVE_ENABLE_SENSOR)
		case SDL_EVENT_GAMEPAD_SENSOR_UPDATE:
		{
			const auto &sens = e.gsensor;
			stick = joymodule->getJoystickFromID(sens.which);
			if (stick)
			{
				const char *sensorName;
				auto sensorType = sensor::sdl::Sensor::convert((SDL_SensorType) sens.sensor);
				if (!sensor::Sensor::getConstant(sensorType, sensorName)) sensorName = "unknown";
				vargs.emplace_back(joysticktype, stick);
				vargs.emplace_back(sensorName, strlen(sensorName));
				vargs.emplace_back((double) sens.data[0]);
				vargs.emplace_back((double) sens.data[1]);
				vargs.emplace_back((double) sens.data[2]);
				msg = new Message("joysticksensorupdated", vargs);
			}
			break;
		}
#endif
		default:
			break;
		}

		return msg;
	}

	// Faithful port of love::event::sdl::Event::convertWindowEvent (the resize
	// branch consults the graphics module's size, like upstream).
	static Message *convertWindowEvent(const SDL_Event &e, love::window::Window *win)
	{
		using namespace love;
		Message *msg = nullptr;
		std::vector<Variant> vargs;
		vargs.reserve(4);

		auto event = e.type;
		switch (event)
		{
		case SDL_EVENT_WINDOW_FOCUS_GAINED:
		case SDL_EVENT_WINDOW_FOCUS_LOST:
			vargs.emplace_back(event == SDL_EVENT_WINDOW_FOCUS_GAINED);
			msg = new Message("focus", vargs);
			break;
		case SDL_EVENT_WINDOW_MOUSE_ENTER:
		case SDL_EVENT_WINDOW_MOUSE_LEAVE:
			vargs.emplace_back(event == SDL_EVENT_WINDOW_MOUSE_ENTER);
			msg = new Message("mousefocus", vargs);
			break;
		case SDL_EVENT_WINDOW_SHOWN:
		case SDL_EVENT_WINDOW_HIDDEN:
		case SDL_EVENT_WINDOW_MINIMIZED:
		case SDL_EVENT_WINDOW_RESTORED:
			vargs.emplace_back(event == SDL_EVENT_WINDOW_SHOWN || event == SDL_EVENT_WINDOW_RESTORED);
			msg = new Message("visible", vargs);
			break;
		case SDL_EVENT_WINDOW_EXPOSED:
			msg = new Message("exposed");
			break;
		case SDL_EVENT_WINDOW_OCCLUDED:
			msg = new Message("occluded");
			break;
		case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
		{
			// Upstream reports the graphics-side size here; the lean backend has no
			// real graphics module, so we report the window's (DPI-converted) size.
			double width = e.window.data1, height = e.window.data2;
			if (win)
			{
				win->onSizeChanged(e.window.data1, e.window.data2);
				width = win->getWidth();
				height = win->getHeight();
				windowToDPICoords(win, &width, &height);
			}
			vargs.emplace_back(width);
			vargs.emplace_back(height);
			msg = new Message("resize", vargs);
			break;
		}
		default:
			break;
		}

		return msg;
	}

}; // HarnessEvent

// =========================================================================
// Love::Event module functions
// =========================================================================

// Builds the [name_symbol, *args] Array for one message.
static mrb_value pushmessage(mrb_state *mrb, const Message &m)
{
	mrb_value arr = mrb_ary_new_capa(mrb, (mrb_int) m.args.size() + 1);
	mrb_ary_push(mrb, arr, mrb_symbol_value(mrb_intern_cstr(mrb, m.name.c_str())));
	for (const Variant &v : m.args)
		mrb_ary_push(mrb, arr, mrbx_pushvariant(mrb, v));
	return arr;
}

static mrb_value w_pump(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[1];
	mrbx_get_kwargs(mrb, {"timeout"}, 0, v);
	float timeout = mrbx_optfloat(mrb, v[0], 0.0f);
	mrbx_catchexcept(mrb, [&]() { instance()->pump(timeout); });
	return mrb_nil_value();
}

// Drains the event queue, returning an Array of [name_symbol, *args] events.
static mrb_value w_poll(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value arr = mrb_ary_new(mrb);

	Message *m = nullptr;
	while (instance()->poll(m) && m != nullptr)
	{
		mrb_ary_push(mrb, arr, pushmessage(mrb, *m));
		m->release();
		m = nullptr;
	}
	return arr;
}

static mrb_value w_push(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[2];
	mrbx_get_kwargs(mrb, {"name", "args"}, 1, v);

	std::string name = mrbx_checkstring(mrb, v[0]);

	std::vector<Variant> vargs;
	if (!mrb_undef_p(v[1]) && mrb_array_p(v[1]))
	{
		mrb_int n = RARRAY_LEN(v[1]);
		vargs.reserve(n);
		for (mrb_int i = 0; i < n; i++)
		{
			Variant var = mrbx_checkvariant(mrb, mrb_ary_ref(mrb, v[1], i));
			if (var.getType() == Variant::UNKNOWN)
				mrb_raisef(mrb, E_ARGUMENT_ERROR, "Argument %d can't be stored safely.", (int)(i + 1));
			vargs.push_back(var);
		}
	}

	StrongRef<Message> m(new Message(name, vargs), Acquire::NORETAIN);
	instance()->push(m);
	return mrb_true_value();
}

static mrb_value w_clear(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrbx_catchexcept(mrb, [&]() { instance()->clear(); });
	return mrb_nil_value();
}

static mrb_value w_quit(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[1];
	mrbx_get_kwargs(mrb, {"code"}, 0, v);

	std::vector<Variant> args;
	args.emplace_back((double) mrbx_optint(mrb, v[0], 0));

	StrongRef<Message> m(new Message("quit", args), Acquire::NORETAIN);
	instance()->push(m);
	return mrb_true_value();
}

static mrb_value w_restart(mrb_state *mrb, mrb_value self)
{
	(void) self;
	std::vector<Variant> args;
	args.emplace_back("restart", strlen("restart"));

	StrongRef<Message> m(new Message("quit", args), Acquire::NORETAIN);
	instance()->push(m);
	return mrb_true_value();
}

static const MrbReg functions[] =
{
	{ "pump",    w_pump,    MRB_ARGS_KEY(1, 0) },
	{ "poll",    w_poll,    MRB_ARGS_NONE() },
	{ "push",    w_push,    MRB_ARGS_KEY(2, 0) },
	{ "clear",   w_clear,   MRB_ARGS_NONE() },
	{ "quit",    w_quit,    MRB_ARGS_KEY(1, 0) },
	{ "restart", w_restart, MRB_ARGS_NONE() },
	{ nullptr, nullptr, 0 }
};

// Equivalent of luaopen_love_event: creates the backend and registers
// Love::Event with its keyword-argument methods.
extern "C" void mrb_love_event_init(mrb_state *mrb)
{
	Event *inst = instance();
	if (inst == nullptr)
		inst = new HarnessEvent();
	else
		inst->retain();

	WrappedModule w;
	w.module = inst;
	w.name = "Event";
	w.type = &Module::type;
	w.functions = functions;

	mrbx_register_module(mrb, w);
}

} // event
} // love
