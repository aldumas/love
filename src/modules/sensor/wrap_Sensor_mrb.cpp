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

// mruby port of wrap_Sensor.cpp. Exposes Love::Sensor with snake_case keyword
// methods. The real SDL sensor backend is used. get_data returns the sensor's
// reading as an Array of floats (its length depends on the sensor).

#include "common/config.h"
#include "common/mrb_runtime.h"
#include "common/Module.h"

#include "Sensor.h"
#include "sdl/Sensor.h"

#include <vector>

namespace love
{
namespace sensor
{

#define instance() (Module::getInstance<Sensor>(Module::M_SENSOR))

static Sensor::SensorType checkSensorType(mrb_state *mrb, mrb_value v)
{
	std::string s = mrbx_checkstring(mrb, v);
	Sensor::SensorType type = Sensor::SENSOR_MAX_ENUM;
	if (!Sensor::getConstant(s.c_str(), type))
		mrb_raisef(mrb, E_ARGUMENT_ERROR, "invalid sensor type: %s", s.c_str());
	return type;
}

static mrb_value w_hasSensor(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[1];
	mrbx_get_kwargs(mrb, {"type"}, 1, v);
	return mrbx_boolean(mrb, instance()->hasSensor(checkSensorType(mrb, v[0])));
}

static mrb_value w_isEnabled(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[1];
	mrbx_get_kwargs(mrb, {"type"}, 1, v);
	return mrbx_boolean(mrb, instance()->isEnabled(checkSensorType(mrb, v[0])));
}

static mrb_value w_setEnabled(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[2];
	mrbx_get_kwargs(mrb, {"type", "enabled"}, 2, v);
	Sensor::SensorType type = checkSensorType(mrb, v[0]);
	bool enabled = mrbx_checkboolean(mrb, v[1]);
	mrbx_catchexcept(mrb, [&]() { instance()->setEnabled(type, enabled); });
	return mrb_nil_value();
}

static mrb_value w_getData(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[1];
	mrbx_get_kwargs(mrb, {"type"}, 1, v);
	Sensor::SensorType type = checkSensorType(mrb, v[0]);

	std::vector<float> data;
	if (mrbx_catchexcept(mrb, [&]() { data = instance()->getData(type); }))
		return mrb_nil_value();

	mrb_value arr = mrb_ary_new_capa(mrb, (mrb_int) data.size());
	for (float f : data)
		mrb_ary_push(mrb, arr, mrbx_number(mrb, f));
	return arr;
}

static mrb_value w_getName(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[1];
	mrbx_get_kwargs(mrb, {"type"}, 1, v);
	Sensor::SensorType type = checkSensorType(mrb, v[0]);

	const char *name = nullptr;
	if (mrbx_catchexcept(mrb, [&]() { name = instance()->getSensorName(type); }))
		return mrb_nil_value();
	return mrbx_string(mrb, name ? name : "");
}

static const MrbReg functions[] =
{
	{ "has_sensor?",  w_hasSensor,  MRB_ARGS_KEY(1, 0) },
	{ "enabled?",     w_isEnabled,  MRB_ARGS_KEY(1, 0) },
	{ "set_enabled",  w_setEnabled, MRB_ARGS_KEY(2, 0) },
	{ "get_data",     w_getData,    MRB_ARGS_KEY(1, 0) },
	{ "get_name",     w_getName,    MRB_ARGS_KEY(1, 0) },
	{ nullptr, nullptr, 0 }
};

extern "C" void mrb_love_sensor_init(mrb_state *mrb)
{
	Sensor *inst = instance();
	if (inst == nullptr)
		inst = new love::sensor::sdl::Sensor();
	else
		inst->retain();

	WrappedModule w;
	w.module = inst;
	w.name = "Sensor";
	w.type = &Module::type;
	w.functions = functions;

	mrbx_register_module(mrb, w);
}

} // sensor
} // love
