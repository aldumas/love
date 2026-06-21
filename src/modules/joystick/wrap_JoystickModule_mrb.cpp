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

// mruby port of wrap_JoystickModule.cpp + wrap_Joystick.cpp. Exposes the
// Love::Joystick type (the connected-controller object) and the love.joystick
// module functions. The module name and the object type name collide ("Joystick"),
// so — like the data/thread modules — the module functions are hung on the
// Love::Joystick *class* as class methods, while the per-controller API lives as
// instance methods on the same class. The real SDL gamepad backend is used.
//
// Indices that were 1-based in the Lua API stay 1-based here (axes, buttons,
// hats, player index, joystick id). Multi-value Lua returns become Hashes.
// Gamepad axis/button names and hat directions are the same enum strings.

#include "common/config.h"
#include "common/mrb_runtime.h"
#include "common/Module.h"
#include "common/int.h"

#include "Joystick.h"
#include "JoystickModule.h"
#include "sdl/JoystickModule.h"

#include "sensor/Sensor.h"
#include "filesystem/Filesystem.h"

#include <string>
#include <vector>

namespace love
{
namespace joystick
{

#define instance() (Module::getInstance<JoystickModule>(Module::M_JOYSTICK))

static void hset(mrb_state *mrb, mrb_value h, const char *key, mrb_value v)
{
	mrb_hash_set(mrb, h, mrb_symbol_value(mrb_intern_cstr(mrb, key)), v);
}

// Reads a `buttons:` keyword that is either a single value or an Array, applying
// `fn` to each element. Returns the count read (0 if undef/nil).
template <typename F>
static int eachButton(mrb_state *mrb, mrb_value v, F fn)
{
	if (mrb_undef_p(v) || mrb_nil_p(v))
		return 0;
	if (mrb_array_p(v))
	{
		mrb_int n = RARRAY_LEN(v);
		for (mrb_int i = 0; i < n; i++)
			fn(mrb_ary_ref(mrb, v, i));
		return (int) n;
	}
	fn(v);
	return 1;
}

// ==========================================================================
// Joystick instance methods
// ==========================================================================

static mrb_value j_isConnected(mrb_state *mrb, mrb_value self)
{
	return mrbx_boolean(mrb, mrbx_checktype<Joystick>(mrb, self)->isConnected());
}

static mrb_value j_getName(mrb_state *mrb, mrb_value self)
{
	return mrbx_string(mrb, mrbx_checktype<Joystick>(mrb, self)->getName());
}

// get_id -> { id:, instance_id: } (both 1-based; instance_id nil if disconnected)
static mrb_value j_getID(mrb_state *mrb, mrb_value self)
{
	Joystick *j = mrbx_checktype<Joystick>(mrb, self);
	mrb_value h = mrb_hash_new(mrb);
	hset(mrb, h, "id", mrbx_integer(mrb, j->getID() + 1));
	int iid = j->getInstanceID();
	hset(mrb, h, "instance_id", iid >= 0 ? mrbx_integer(mrb, iid + 1) : mrb_nil_value());
	return h;
}

static mrb_value j_getGUID(mrb_state *mrb, mrb_value self)
{
	return mrbx_string(mrb, mrbx_checktype<Joystick>(mrb, self)->getGUID());
}

static mrb_value j_getDeviceInfo(mrb_state *mrb, mrb_value self)
{
	Joystick *j = mrbx_checktype<Joystick>(mrb, self);
	int vendorID = 0, productID = 0, productVersion = 0;
	j->getDeviceInfo(vendorID, productID, productVersion);
	mrb_value h = mrb_hash_new(mrb);
	hset(mrb, h, "vendor_id", mrbx_integer(mrb, vendorID));
	hset(mrb, h, "product_id", mrbx_integer(mrb, productID));
	hset(mrb, h, "product_version", mrbx_integer(mrb, productVersion));
	return h;
}

static mrb_value j_getJoystickType(mrb_state *mrb, mrb_value self)
{
	Joystick *j = mrbx_checktype<Joystick>(mrb, self);
	const char *str = "unknown";
	Joystick::getConstant(j->getJoystickType(), str);
	return mrbx_string(mrb, str);
}

static mrb_value j_getAxisCount(mrb_state *mrb, mrb_value self)
{
	return mrbx_integer(mrb, mrbx_checktype<Joystick>(mrb, self)->getAxisCount());
}

static mrb_value j_getButtonCount(mrb_state *mrb, mrb_value self)
{
	return mrbx_integer(mrb, mrbx_checktype<Joystick>(mrb, self)->getButtonCount());
}

static mrb_value j_getHatCount(mrb_state *mrb, mrb_value self)
{
	return mrbx_integer(mrb, mrbx_checktype<Joystick>(mrb, self)->getHatCount());
}

static mrb_value j_getAxis(mrb_state *mrb, mrb_value self)
{
	Joystick *j = mrbx_checktype<Joystick>(mrb, self);
	mrb_value v[1];
	mrbx_get_kwargs(mrb, {"axis"}, 1, v);
	int axisindex = mrbx_checkint(mrb, v[0]) - 1;
	return mrbx_number(mrb, j->getAxis(axisindex));
}

static mrb_value j_getAxes(mrb_state *mrb, mrb_value self)
{
	Joystick *j = mrbx_checktype<Joystick>(mrb, self);
	std::vector<float> axes = j->getAxes();
	mrb_value arr = mrb_ary_new_capa(mrb, (mrb_int) axes.size());
	for (float value : axes)
		mrb_ary_push(mrb, arr, mrbx_number(mrb, value));
	return arr;
}

static mrb_value j_getHat(mrb_state *mrb, mrb_value self)
{
	Joystick *j = mrbx_checktype<Joystick>(mrb, self);
	mrb_value v[1];
	mrbx_get_kwargs(mrb, {"hat"}, 1, v);
	int hatindex = mrbx_checkint(mrb, v[0]) - 1;
	Joystick::Hat h = j->getHat(hatindex);
	const char *direction = "";
	Joystick::getConstant(h, direction);
	return mrbx_string(mrb, direction);
}

// down?(buttons:) — buttons is a 1-based button index or an Array of them.
static mrb_value j_isDown(mrb_state *mrb, mrb_value self)
{
	Joystick *j = mrbx_checktype<Joystick>(mrb, self);
	mrb_value v[1];
	mrbx_get_kwargs(mrb, {"buttons"}, 1, v);

	std::vector<int> buttons;
	eachButton(mrb, v[0], [&](mrb_value b) { buttons.push_back(mrbx_checkint(mrb, b) - 1); });
	return mrbx_boolean(mrb, j->isDown(buttons));
}

static mrb_value j_setPlayerIndex(mrb_state *mrb, mrb_value self)
{
	Joystick *j = mrbx_checktype<Joystick>(mrb, self);
	mrb_value v[1];
	mrbx_get_kwargs(mrb, {"index"}, 1, v);
	j->setPlayerIndex(mrbx_checkint(mrb, v[0]) - 1);
	return self;
}

static mrb_value j_getPlayerIndex(mrb_state *mrb, mrb_value self)
{
	Joystick *j = mrbx_checktype<Joystick>(mrb, self);
	int index = j->getPlayerIndex();
	return mrbx_integer(mrb, index >= 0 ? index + 1 : index);
}

static mrb_value j_isGamepad(mrb_state *mrb, mrb_value self)
{
	return mrbx_boolean(mrb, mrbx_checktype<Joystick>(mrb, self)->isGamepad());
}

static mrb_value j_getGamepadType(mrb_state *mrb, mrb_value self)
{
	Joystick *j = mrbx_checktype<Joystick>(mrb, self);
	const char *str = "unknown";
	Joystick::getConstant(j->getGamepadType(), str);
	return mrbx_string(mrb, str);
}

static mrb_value j_getGamepadAxis(mrb_state *mrb, mrb_value self)
{
	Joystick *j = mrbx_checktype<Joystick>(mrb, self);
	mrb_value v[1];
	mrbx_get_kwargs(mrb, {"axis"}, 1, v);
	std::string str = mrbx_checkstring(mrb, v[0]);
	Joystick::GamepadAxis axis;
	if (!Joystick::getConstant(str.c_str(), axis))
		mrb_raisef(mrb, E_ARGUMENT_ERROR, "invalid gamepad axis: %s", str.c_str());
	return mrbx_number(mrb, j->getGamepadAxis(axis));
}

// gamepad_down?(buttons:) — a gamepad-button name String or an Array of them.
static mrb_value j_isGamepadDown(mrb_state *mrb, mrb_value self)
{
	Joystick *j = mrbx_checktype<Joystick>(mrb, self);
	mrb_value v[1];
	mrbx_get_kwargs(mrb, {"buttons"}, 1, v);

	std::vector<Joystick::GamepadButton> buttons;
	eachButton(mrb, v[0], [&](mrb_value b) {
		std::string str = mrbx_checkstring(mrb, b);
		Joystick::GamepadButton button;
		if (!Joystick::getConstant(str.c_str(), button))
			mrb_raisef(mrb, E_ARGUMENT_ERROR, "invalid gamepad button: %s", str.c_str());
		buttons.push_back(button);
	});
	return mrbx_boolean(mrb, j->isGamepadDown(buttons));
}

// get_gamepad_mapping(gamepad_input:) -> { input_type:, index:, direction? } or nil
static mrb_value j_getGamepadMapping(mrb_state *mrb, mrb_value self)
{
	Joystick *j = mrbx_checktype<Joystick>(mrb, self);
	mrb_value v[1];
	mrbx_get_kwargs(mrb, {"gamepad_input"}, 1, v);
	std::string gpbindstr = mrbx_checkstring(mrb, v[0]);

	Joystick::GamepadInput gpinput;
	if (Joystick::getConstant(gpbindstr.c_str(), gpinput.axis))
		gpinput.type = Joystick::INPUT_TYPE_AXIS;
	else if (Joystick::getConstant(gpbindstr.c_str(), gpinput.button))
		gpinput.type = Joystick::INPUT_TYPE_BUTTON;
	else
	{
		mrb_raisef(mrb, E_ARGUMENT_ERROR, "invalid gamepad axis/button: %s", gpbindstr.c_str());
		return mrb_nil_value();
	}

	Joystick::JoystickInput jinput;
	jinput.type = Joystick::INPUT_TYPE_MAX_ENUM;
	if (mrbx_catchexcept(mrb, [&]() { jinput = j->getGamepadMapping(gpinput); }))
		return mrb_nil_value();

	if (jinput.type == Joystick::INPUT_TYPE_MAX_ENUM)
		return mrb_nil_value();

	const char *inputtypestr = nullptr;
	if (!Joystick::getConstant(jinput.type, inputtypestr))
		mrb_raise(mrb, E_RUNTIME_ERROR, "Unknown joystick input type.");

	mrb_value h = mrb_hash_new(mrb);
	hset(mrb, h, "input_type", mrbx_string(mrb, inputtypestr));

	switch (jinput.type)
	{
	case Joystick::INPUT_TYPE_AXIS:
		hset(mrb, h, "index", mrbx_integer(mrb, jinput.axis + 1));
		break;
	case Joystick::INPUT_TYPE_BUTTON:
		hset(mrb, h, "index", mrbx_integer(mrb, jinput.button + 1));
		break;
	case Joystick::INPUT_TYPE_HAT:
	{
		hset(mrb, h, "index", mrbx_integer(mrb, jinput.hat.index + 1));
		const char *hatstr = nullptr;
		if (!Joystick::getConstant(jinput.hat.value, hatstr))
			mrb_raise(mrb, E_RUNTIME_ERROR, "Unknown joystick hat.");
		hset(mrb, h, "direction", mrbx_string(mrb, hatstr));
		break;
	}
	default:
		mrb_raise(mrb, E_RUNTIME_ERROR, "Unknown joystick input type.");
	}
	return h;
}

static mrb_value j_getGamepadMappingString(mrb_state *mrb, mrb_value self)
{
	Joystick *j = mrbx_checktype<Joystick>(mrb, self);
	std::string mapping = j->getGamepadMappingString();
	return mapping.empty() ? mrb_nil_value() : mrbx_string(mrb, mapping);
}

static mrb_value j_isVibrationSupported(mrb_state *mrb, mrb_value self)
{
	return mrbx_boolean(mrb, mrbx_checktype<Joystick>(mrb, self)->isVibrationSupported());
}

// set_vibration(left:, right:, duration:) — no args disables vibration.
static mrb_value j_setVibration(mrb_state *mrb, mrb_value self)
{
	Joystick *j = mrbx_checktype<Joystick>(mrb, self);
	mrb_value v[3];
	mrbx_get_kwargs(mrb, {"left", "right", "duration"}, 0, v);

	bool success;
	if (mrb_undef_p(v[0]))
		success = j->setVibration();
	else
	{
		float left = (float) mrbx_checknumber(mrb, v[0]);
		float right = (float) mrbx_optnumber(mrb, v[1], left);
		float duration = (float) mrbx_optnumber(mrb, v[2], -1.0); // -1 is infinite
		success = j->setVibration(left, right, duration);
	}
	return mrbx_boolean(mrb, success);
}

static mrb_value j_getVibration(mrb_state *mrb, mrb_value self)
{
	Joystick *j = mrbx_checktype<Joystick>(mrb, self);
	float left, right;
	j->getVibration(left, right);
	mrb_value h = mrb_hash_new(mrb);
	hset(mrb, h, "left", mrbx_number(mrb, left));
	hset(mrb, h, "right", mrbx_number(mrb, right));
	return h;
}

#ifdef LOVE_ENABLE_SENSOR

static love::sensor::Sensor::SensorType checkSensorType(mrb_state *mrb, mrb_value v)
{
	using namespace love::sensor;
	std::string s = mrbx_checkstring(mrb, v);
	Sensor::SensorType type;
	if (!Sensor::getConstant(s.c_str(), type))
		mrb_raisef(mrb, E_ARGUMENT_ERROR, "invalid sensor type: %s", s.c_str());
	return type;
}

static mrb_value j_hasSensor(mrb_state *mrb, mrb_value self)
{
	Joystick *j = mrbx_checktype<Joystick>(mrb, self);
	mrb_value v[1];
	mrbx_get_kwargs(mrb, {"type"}, 1, v);
	return mrbx_boolean(mrb, j->hasSensor(checkSensorType(mrb, v[0])));
}

static mrb_value j_isSensorEnabled(mrb_state *mrb, mrb_value self)
{
	Joystick *j = mrbx_checktype<Joystick>(mrb, self);
	mrb_value v[1];
	mrbx_get_kwargs(mrb, {"type"}, 1, v);
	return mrbx_boolean(mrb, j->isSensorEnabled(checkSensorType(mrb, v[0])));
}

static mrb_value j_setSensorEnabled(mrb_state *mrb, mrb_value self)
{
	Joystick *j = mrbx_checktype<Joystick>(mrb, self);
	mrb_value v[2];
	mrbx_get_kwargs(mrb, {"type", "enabled"}, 2, v);
	love::sensor::Sensor::SensorType type = checkSensorType(mrb, v[0]);
	bool enabled = mrbx_checkboolean(mrb, v[1]);
	mrbx_catchexcept(mrb, [&]() { j->setSensorEnabled(type, enabled); });
	return self;
}

static mrb_value j_getSensorData(mrb_state *mrb, mrb_value self)
{
	Joystick *j = mrbx_checktype<Joystick>(mrb, self);
	mrb_value v[1];
	mrbx_get_kwargs(mrb, {"type"}, 1, v);
	love::sensor::Sensor::SensorType type = checkSensorType(mrb, v[0]);

	std::vector<float> data;
	if (mrbx_catchexcept(mrb, [&]() { data = j->getSensorData(type); }))
		return mrb_nil_value();
	mrb_value arr = mrb_ary_new_capa(mrb, (mrb_int) data.size());
	for (float f : data)
		mrb_ary_push(mrb, arr, mrbx_number(mrb, f));
	return arr;
}

// get_device_power_info -> { state:, percent: } (percent nil if unknown)
static mrb_value j_getDevicePowerInfo(mrb_state *mrb, mrb_value self)
{
	Joystick *j = mrbx_checktype<Joystick>(mrb, self);
	int batteryPercent = 0;
	const char *str = "unknown";
	Joystick::PowerType state = j->getPowerInfo(batteryPercent);
	Joystick::getConstant(state, str);
	mrb_value h = mrb_hash_new(mrb);
	hset(mrb, h, "state", mrbx_string(mrb, str));
	hset(mrb, h, "percent", batteryPercent >= 0 ? mrbx_integer(mrb, batteryPercent) : mrb_nil_value());
	return h;
}

static mrb_value j_getDeviceConnectionState(mrb_state *mrb, mrb_value self)
{
	Joystick *j = mrbx_checktype<Joystick>(mrb, self);
	const char *str = "unknown";
	Joystick::getConstant(j->getConnectionState(), str);
	return mrbx_string(mrb, str);
}

#endif // LOVE_ENABLE_SENSOR

// From the module: this controller's connected index (1-based) or nil.
static mrb_value j_getConnectedIndex(mrb_state *mrb, mrb_value self)
{
	Joystick *j = mrbx_checktype<Joystick>(mrb, self);
	int index = instance()->getIndex(j);
	return index >= 0 ? mrbx_integer(mrb, index + 1) : mrb_nil_value();
}

static const MrbReg joystickFunctions[] =
{
	{ "connected?",                 j_isConnected,             MRB_ARGS_NONE() },
	{ "get_name",                   j_getName,                 MRB_ARGS_NONE() },
	{ "get_id",                     j_getID,                   MRB_ARGS_NONE() },
	{ "get_guid",                   j_getGUID,                 MRB_ARGS_NONE() },
	{ "get_device_info",            j_getDeviceInfo,           MRB_ARGS_NONE() },
	{ "get_joystick_type",          j_getJoystickType,         MRB_ARGS_NONE() },
	{ "get_axis_count",             j_getAxisCount,            MRB_ARGS_NONE() },
	{ "get_button_count",           j_getButtonCount,          MRB_ARGS_NONE() },
	{ "get_hat_count",              j_getHatCount,             MRB_ARGS_NONE() },
	{ "get_axis",                   j_getAxis,                 MRB_ARGS_KEY(1, 0) },
	{ "get_axes",                   j_getAxes,                 MRB_ARGS_NONE() },
	{ "get_hat",                    j_getHat,                  MRB_ARGS_KEY(1, 0) },
	{ "down?",                      j_isDown,                  MRB_ARGS_KEY(1, 0) },
	{ "set_player_index",           j_setPlayerIndex,          MRB_ARGS_KEY(1, 0) },
	{ "get_player_index",           j_getPlayerIndex,          MRB_ARGS_NONE() },
	{ "gamepad?",                   j_isGamepad,               MRB_ARGS_NONE() },
	{ "get_gamepad_type",           j_getGamepadType,          MRB_ARGS_NONE() },
	{ "get_gamepad_axis",           j_getGamepadAxis,          MRB_ARGS_KEY(1, 0) },
	{ "gamepad_down?",              j_isGamepadDown,           MRB_ARGS_KEY(1, 0) },
	{ "get_gamepad_mapping",        j_getGamepadMapping,       MRB_ARGS_KEY(1, 0) },
	{ "get_gamepad_mapping_string", j_getGamepadMappingString, MRB_ARGS_NONE() },
	{ "vibration_supported?",       j_isVibrationSupported,    MRB_ARGS_NONE() },
	{ "set_vibration",              j_setVibration,            MRB_ARGS_KEY(3, 0) },
	{ "get_vibration",              j_getVibration,            MRB_ARGS_NONE() },
#ifdef LOVE_ENABLE_SENSOR
	{ "has_sensor?",                j_hasSensor,               MRB_ARGS_KEY(1, 0) },
	{ "sensor_enabled?",            j_isSensorEnabled,         MRB_ARGS_KEY(1, 0) },
	{ "set_sensor_enabled",         j_setSensorEnabled,        MRB_ARGS_KEY(2, 0) },
	{ "get_sensor_data",            j_getSensorData,           MRB_ARGS_KEY(1, 0) },
	{ "get_device_power_info",      j_getDevicePowerInfo,      MRB_ARGS_NONE() },
	{ "get_device_connection_state", j_getDeviceConnectionState, MRB_ARGS_NONE() },
#endif
	{ "get_connected_index",        j_getConnectedIndex,       MRB_ARGS_NONE() },
	{ nullptr, nullptr, 0 }
};

// ==========================================================================
// Module-level functions (class methods on Love::Joystick)
// ==========================================================================

static mrb_value w_getJoysticks(mrb_state *mrb, mrb_value self)
{
	(void) self;
	int stickcount = instance()->getJoystickCount();
	mrb_value arr = mrb_ary_new_capa(mrb, stickcount);
	for (int i = 0; i < stickcount; i++)
		mrb_ary_push(mrb, arr, mrbx_pushtype(mrb, instance()->getJoystick(i)));
	return arr;
}

static mrb_value w_getJoystickCount(mrb_state *mrb, mrb_value self)
{
	(void) self;
	return mrbx_integer(mrb, instance()->getJoystickCount());
}

static mrb_value w_setBackgroundEvents(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[1];
	mrbx_get_kwargs(mrb, {"enabled"}, 1, v);
	instance()->setBackgroundEvents(mrbx_checkboolean(mrb, v[0]));
	return mrb_nil_value();
}

static mrb_value w_hasBackgroundEvents(mrb_state *mrb, mrb_value self)
{
	(void) self;
	return mrbx_boolean(mrb, instance()->hasBackgroundEvents());
}

// set_gamepad_mapping(guid:, gamepad_input:, input_type:, input_index:,
//   hat_direction: <required for input_type "hat">) -> bool.
static mrb_value w_setGamepadMapping(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[5];
	mrbx_get_kwargs(mrb, {"guid", "gamepad_input", "input_type", "input_index", "hat_direction"}, 4, v);

	std::string guid = mrbx_checkstring(mrb, v[0]);
	std::string gpbindstr = mrbx_checkstring(mrb, v[1]);

	Joystick::GamepadInput gpinput;
	if (Joystick::getConstant(gpbindstr.c_str(), gpinput.axis))
		gpinput.type = Joystick::INPUT_TYPE_AXIS;
	else if (Joystick::getConstant(gpbindstr.c_str(), gpinput.button))
		gpinput.type = Joystick::INPUT_TYPE_BUTTON;
	else
	{
		mrb_raisef(mrb, E_ARGUMENT_ERROR, "invalid gamepad axis/button: %s", gpbindstr.c_str());
		return mrb_nil_value();
	}

	std::string jinputtypestr = mrbx_checkstring(mrb, v[2]);
	Joystick::JoystickInput jinput;
	if (!Joystick::getConstant(jinputtypestr.c_str(), jinput.type))
	{
		mrb_raisef(mrb, E_ARGUMENT_ERROR, "invalid joystick input type: %s", jinputtypestr.c_str());
		return mrb_nil_value();
	}

	int index = mrbx_checkint(mrb, v[3]) - 1;
	switch (jinput.type)
	{
	case Joystick::INPUT_TYPE_AXIS:
		jinput.axis = index;
		break;
	case Joystick::INPUT_TYPE_BUTTON:
		jinput.button = index;
		break;
	case Joystick::INPUT_TYPE_HAT:
	{
		jinput.hat.index = index;
		if (mrb_undef_p(v[4]))
		{
			mrb_raise(mrb, E_ARGUMENT_ERROR, "`hat_direction:` is required when input_type is \"hat\"");
			return mrb_nil_value();
		}
		std::string hatstr = mrbx_checkstring(mrb, v[4]);
		if (!Joystick::getConstant(hatstr.c_str(), jinput.hat.value))
		{
			mrb_raisef(mrb, E_ARGUMENT_ERROR, "invalid joystick hat: %s", hatstr.c_str());
			return mrb_nil_value();
		}
		break;
	}
	default:
		mrb_raisef(mrb, E_ARGUMENT_ERROR, "invalid joystick input type: %s", jinputtypestr.c_str());
		return mrb_nil_value();
	}

	bool success = false;
	mrbx_catchexcept(mrb, [&]() { success = instance()->setGamepadMapping(guid, gpinput, jinput); });
	return mrbx_boolean(mrb, success);
}

// load_gamepad_mappings(data:) — `data` is either a filename (loaded via the
// filesystem module if it names an existing file) or a literal mappings string.
static mrb_value w_loadGamepadMappings(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[1];
	mrbx_get_kwargs(mrb, {"data"}, 1, v);
	std::string mappings = mrbx_checkstring(mrb, v[0]);

	auto fs = Module::getInstance<love::filesystem::Filesystem>(Module::M_FILESYSTEM);
	if (fs != nullptr)
	{
		love::filesystem::Filesystem::Info info = {};
		if (fs->getInfo(mappings.c_str(), info) && info.type == love::filesystem::Filesystem::FILETYPE_FILE)
		{
			bool err = mrbx_catchexcept(mrb, [&]() {
				StrongRef<love::filesystem::FileData> fd(fs->read(mappings.c_str()), Acquire::NORETAIN);
				mappings = std::string((const char *) fd->getData(), fd->getSize());
			});
			if (err)
				return mrb_nil_value();
		}
	}

	mrbx_catchexcept(mrb, [&]() { instance()->loadGamepadMappings(mappings); });
	return mrb_nil_value();
}

// save_gamepad_mappings(file: <opt>) -> the mappings String; if `file:` is given
// the string is also written there via the filesystem module.
static mrb_value w_saveGamepadMappings(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[1];
	mrbx_get_kwargs(mrb, {"file"}, 0, v);
	std::string mappings = instance()->saveGamepadMappings();

	if (!mrb_undef_p(v[0]) && !mrb_nil_p(v[0]))
	{
		std::string filename = mrbx_checkstring(mrb, v[0]);
		auto fs = Module::getInstance<love::filesystem::Filesystem>(Module::M_FILESYSTEM);
		if (fs == nullptr)
		{
			mrb_raise(mrb, E_RUNTIME_ERROR, "The filesystem module must be loaded to write gamepad mappings to a file.");
			return mrb_nil_value();
		}
		if (mrbx_catchexcept(mrb, [&]() { fs->write(filename.c_str(), mappings.data(), (int64) mappings.size()); }))
			return mrb_nil_value();
	}

	return mrbx_string(mrb, mappings);
}

static mrb_value w_getGamepadMappingString(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[1];
	mrbx_get_kwargs(mrb, {"guid"}, 1, v);
	std::string guid = mrbx_checkstring(mrb, v[0]);
	std::string mapping = instance()->getGamepadMappingString(guid.c_str());
	return mapping.empty() ? mrb_nil_value() : mrbx_string(mrb, mapping);
}

static const MrbReg moduleFunctions[] =
{
	{ "get_joysticks",              w_getJoysticks,            MRB_ARGS_NONE() },
	{ "get_joystick_count",         w_getJoystickCount,        MRB_ARGS_NONE() },
	{ "set_background_events",      w_setBackgroundEvents,     MRB_ARGS_KEY(1, 0) },
	{ "background_events?",         w_hasBackgroundEvents,     MRB_ARGS_NONE() },
	{ "set_gamepad_mapping",        w_setGamepadMapping,       MRB_ARGS_KEY(5, 0) },
	{ "load_gamepad_mappings",      w_loadGamepadMappings,     MRB_ARGS_KEY(1, 0) },
	{ "save_gamepad_mappings",      w_saveGamepadMappings,     MRB_ARGS_KEY(1, 0) },
	{ "get_gamepad_mapping_string", w_getGamepadMappingString, MRB_ARGS_KEY(1, 0) },
	{ nullptr, nullptr, 0 }
};

// The module name "Joystick" collides with the Joystick type, so the module
// functions are class methods on the Love::Joystick class (which also carries
// the per-controller instance methods) — same trick the data/thread modules use.
extern "C" void mrb_love_joystick_init(mrb_state *mrb)
{
	if (instance() == nullptr)
		(new love::joystick::sdl::JoystickModule())->retain();

	struct RClass *joystickClass = mrbx_gettypeclass(mrb, Joystick::type);

	for (const MrbReg *r = joystickFunctions; r->name != nullptr; r++)
		mrb_define_method(mrb, joystickClass, r->name, r->func, r->aspec);

	for (const MrbReg *r = moduleFunctions; r->name != nullptr; r++)
		mrb_define_class_method(mrb, joystickClass, r->name, r->func, r->aspec);
}

} // joystick
} // love
