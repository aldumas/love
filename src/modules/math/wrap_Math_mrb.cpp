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

// mruby port of wrap_Math.cpp + wrap_RandomGenerator.cpp.
//
// API shape: Love::Math module functions and a Love::RandomGenerator class,
// snake_case names, keyword arguments. Notable differences from the Lua API:
//
//   love.math.random()        -> Love::Math.random              (Float in [0,1))
//   love.math.random(n)       -> Love::Math.random(max: n)      (Integer 1..n)
//   love.math.random(l, u)    -> Love::Math.random(min: l, max: u)
//   rng = love.math.newRandomGenerator(seed)
//                             -> rng = Love::Math.new_random_generator(seed: ...)
//   rng:random(l, u)          -> rng.random(min: l, max: u)
//
// The integer-range logic that lived in wrap_Math.lua / wrap_RandomGenerator.lua
// is implemented directly in C++ here; the LuaJIT-FFI fast paths are dropped.

#include "common/config.h"
#include "common/mrb_runtime.h"
#include "common/Vector.h"
#include "MathModule.h"
#include "RandomGenerator.h"

#include <vector>
#include <cmath>

namespace love
{
namespace math
{

#define instance() (Module::getInstance<Math>(Module::M_MATH))

// --- shared helpers ------------------------------------------------------

// Applies LÖVE's random-range semantics to a base value r in [0,1):
//   max undef         -> r (the raw float)
//   min undef         -> integer in [1, max]
//   both present      -> integer in [min, max]
static mrb_value applyRandomRange(mrb_state *mrb, double r, mrb_value vmin, mrb_value vmax)
{
	if (mrb_undef_p(vmax))
		return mrbx_number(mrb, r);

	double maxv = mrbx_checknumber(mrb, vmax);

	if (mrb_undef_p(vmin))
		return mrbx_integer(mrb, (int) std::floor(r * maxv) + 1);

	double minv = mrbx_checknumber(mrb, vmin);
	return mrbx_integer(mrb, (int) std::floor(r * (maxv - minv + 1)) + (int) minv);
}

// Reads a flat Ruby array of numbers [x0,y0,x1,y1,...] into a Vector2 list.
static std::vector<Vector2> checkVector2Array(mrb_state *mrb, mrb_value arr)
{
	std::vector<Vector2> out;
	if (!mrb_array_p(arr))
		mrb_raise(mrb, E_TYPE_ERROR, "expected an array of numbers for points:");

	mrb_int n = RARRAY_LEN(arr);
	out.reserve(n / 2);
	for (mrb_int i = 0; i + 1 < n; i += 2)
	{
		Vector2 v;
		v.x = (float) mrb_as_float(mrb, mrb_ary_ref(mrb, arr, i));
		v.y = (float) mrb_as_float(mrb, mrb_ary_ref(mrb, arr, i + 1));
		out.push_back(v);
	}
	return out;
}

static RandomGenerator::Seed seedFromValue(mrb_value v)
{
	RandomGenerator::Seed s;
	s.b64 = (uint64) mrb_integer(v);
	return s;
}

// =========================================================================
// Love::RandomGenerator instance methods
// =========================================================================

static mrb_value rg_random(mrb_state *mrb, mrb_value self)
{
	RandomGenerator *rng = mrbx_checktype<RandomGenerator>(mrb, self);
	mrb_value v[2];
	mrbx_get_kwargs(mrb, {"min", "max"}, 0, v);
	return applyRandomRange(mrb, rng->random(), v[0], v[1]);
}

static mrb_value rg_randomNormal(mrb_state *mrb, mrb_value self)
{
	RandomGenerator *rng = mrbx_checktype<RandomGenerator>(mrb, self);
	mrb_value v[2];
	mrbx_get_kwargs(mrb, {"stddev", "mean"}, 0, v);
	double stddev = mrbx_optnumber(mrb, v[0], 1.0);
	double mean   = mrbx_optnumber(mrb, v[1], 0.0);
	return mrbx_number(mrb, rng->randomNormal(stddev) + mean);
}

static mrb_value rg_setSeed(mrb_state *mrb, mrb_value self)
{
	RandomGenerator *rng = mrbx_checktype<RandomGenerator>(mrb, self);
	mrb_value v[1];
	mrbx_get_kwargs(mrb, {"seed"}, 1, v);
	mrbx_catchexcept(mrb, [&]() { rng->setSeed(seedFromValue(v[0])); });
	return mrb_nil_value();
}

static mrb_value rg_getSeed(mrb_state *mrb, mrb_value self)
{
	RandomGenerator *rng = mrbx_checktype<RandomGenerator>(mrb, self);
	RandomGenerator::Seed s = rng->getSeed();
	// Returned as [low, high] to avoid 64-bit precision loss in mruby integers.
	mrb_value pair[2] = { mrbx_integer(mrb, (int) s.b32.low), mrbx_integer(mrb, (int) s.b32.high) };
	return mrb_ary_new_from_values(mrb, 2, pair);
}

static mrb_value rg_setState(mrb_state *mrb, mrb_value self)
{
	RandomGenerator *rng = mrbx_checktype<RandomGenerator>(mrb, self);
	mrb_value v[1];
	mrbx_get_kwargs(mrb, {"state"}, 1, v);
	std::string state = mrbx_checkstring(mrb, v[0]);
	mrbx_catchexcept(mrb, [&]() { rng->setState(state); });
	return mrb_nil_value();
}

static mrb_value rg_getState(mrb_state *mrb, mrb_value self)
{
	RandomGenerator *rng = mrbx_checktype<RandomGenerator>(mrb, self);
	return mrbx_string(mrb, rng->getState());
}

static const MrbReg rg_functions[] =
{
	{ "random",        rg_random,       MRB_ARGS_KEY(2, 0) },
	{ "random_normal", rg_randomNormal, MRB_ARGS_KEY(2, 0) },
	{ "set_seed",      rg_setSeed,      MRB_ARGS_KEY(1, 0) },
	{ "get_seed",      rg_getSeed,      MRB_ARGS_NONE() },
	{ "set_state",     rg_setState,     MRB_ARGS_KEY(1, 0) },
	{ "get_state",     rg_getState,     MRB_ARGS_NONE() },
	{ nullptr, nullptr, 0 }
};

// =========================================================================
// Love::Math module functions
// =========================================================================

static mrb_value w_random(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[2];
	mrbx_get_kwargs(mrb, {"min", "max"}, 0, v);
	return applyRandomRange(mrb, instance()->getRandomGenerator()->random(), v[0], v[1]);
}

static mrb_value w_randomNormal(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[2];
	mrbx_get_kwargs(mrb, {"stddev", "mean"}, 0, v);
	double stddev = mrbx_optnumber(mrb, v[0], 1.0);
	double mean   = mrbx_optnumber(mrb, v[1], 0.0);
	return mrbx_number(mrb, instance()->getRandomGenerator()->randomNormal(stddev) + mean);
}

static mrb_value w_setRandomSeed(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[1];
	mrbx_get_kwargs(mrb, {"seed"}, 1, v);
	mrbx_catchexcept(mrb, [&]() { instance()->getRandomGenerator()->setSeed(seedFromValue(v[0])); });
	return mrb_nil_value();
}

static mrb_value w_newRandomGenerator(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[1];
	mrbx_get_kwargs(mrb, {"seed"}, 0, v);

	RandomGenerator *rng = instance()->newRandomGenerator();
	if (!mrb_undef_p(v[0]))
	{
		bool err = mrbx_catchexcept(mrb, [&]() { rng->setSeed(seedFromValue(v[0])); });
		if (err) { rng->release(); return mrb_nil_value(); }
	}

	mrb_value out = mrbx_pushtype(mrb, rng);
	rng->release();
	return out;
}

static mrb_value w_perlinNoise(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[4];
	mrbx_get_kwargs(mrb, {"x", "y", "z", "w"}, 1, v);
	double x = mrbx_checknumber(mrb, v[0]);
	if (!mrb_undef_p(v[3]))
		return mrbx_number(mrb, perlinNoise4(x, mrbx_checknumber(mrb, v[1]), mrbx_checknumber(mrb, v[2]), mrbx_checknumber(mrb, v[3])));
	if (!mrb_undef_p(v[2]))
		return mrbx_number(mrb, perlinNoise3(x, mrbx_checknumber(mrb, v[1]), mrbx_checknumber(mrb, v[2])));
	if (!mrb_undef_p(v[1]))
		return mrbx_number(mrb, perlinNoise2(x, mrbx_checknumber(mrb, v[1])));
	return mrbx_number(mrb, perlinNoise1(x));
}

static mrb_value w_simplexNoise(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[4];
	mrbx_get_kwargs(mrb, {"x", "y", "z", "w"}, 1, v);
	double x = mrbx_checknumber(mrb, v[0]);
	if (!mrb_undef_p(v[3]))
		return mrbx_number(mrb, simplexNoise4(x, mrbx_checknumber(mrb, v[1]), mrbx_checknumber(mrb, v[2]), mrbx_checknumber(mrb, v[3])));
	if (!mrb_undef_p(v[2]))
		return mrbx_number(mrb, simplexNoise3(x, mrbx_checknumber(mrb, v[1]), mrbx_checknumber(mrb, v[2])));
	if (!mrb_undef_p(v[1]))
		return mrbx_number(mrb, simplexNoise2(x, mrbx_checknumber(mrb, v[1])));
	return mrbx_number(mrb, simplexNoise1(x));
}

static mrb_value w_gammaToLinear(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[1];
	mrbx_get_kwargs(mrb, {"c"}, 1, v);
	double c = std::min(std::max(mrbx_checknumber(mrb, v[0]), 0.0), 1.0);
	return mrbx_number(mrb, gammaToLinear((float) c));
}

static mrb_value w_linearToGamma(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[1];
	mrbx_get_kwargs(mrb, {"c"}, 1, v);
	double c = std::min(std::max(mrbx_checknumber(mrb, v[0]), 0.0), 1.0);
	return mrbx_number(mrb, linearToGamma((float) c));
}

static mrb_value w_isConvex(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[1];
	mrbx_get_kwargs(mrb, {"points"}, 1, v);
	std::vector<Vector2> verts = checkVector2Array(mrb, v[0]);
	return mrbx_boolean(mrb, isConvex(verts));
}

static mrb_value w_triangulate(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[1];
	mrbx_get_kwargs(mrb, {"points"}, 1, v);
	std::vector<Vector2> verts = checkVector2Array(mrb, v[0]);

	if (verts.size() < 3)
		mrb_raisef(mrb, E_ARGUMENT_ERROR, "Need at least 3 vertices to triangulate (got %d).", (int) verts.size());

	std::vector<Triangle> tris;
	bool err = mrbx_catchexcept(mrb, [&]() {
		if (verts.size() == 3)
			tris.push_back(Triangle(verts[0], verts[1], verts[2]));
		else
			tris = triangulate(verts);
	});
	if (err)
		return mrb_nil_value();

	// Returns [[ax,ay,bx,by,cx,cy], ...].
	mrb_value result = mrb_ary_new_capa(mrb, (mrb_int) tris.size());
	for (const Triangle &t : tris)
	{
		mrb_value coords[6] = {
			mrbx_number(mrb, t.a.x), mrbx_number(mrb, t.a.y),
			mrbx_number(mrb, t.b.x), mrbx_number(mrb, t.b.y),
			mrbx_number(mrb, t.c.x), mrbx_number(mrb, t.c.y),
		};
		mrb_ary_push(mrb, result, mrb_ary_new_from_values(mrb, 6, coords));
	}
	return result;
}

static const MrbReg functions[] =
{
	{ "random",                w_random,             MRB_ARGS_KEY(2, 0) },
	{ "random_normal",         w_randomNormal,       MRB_ARGS_KEY(2, 0) },
	{ "set_random_seed",       w_setRandomSeed,      MRB_ARGS_KEY(1, 0) },
	{ "new_random_generator",  w_newRandomGenerator, MRB_ARGS_KEY(1, 0) },
	{ "perlin_noise",          w_perlinNoise,        MRB_ARGS_KEY(4, 0) },
	{ "simplex_noise",         w_simplexNoise,       MRB_ARGS_KEY(4, 0) },
	{ "gamma_to_linear",       w_gammaToLinear,      MRB_ARGS_KEY(1, 0) },
	{ "linear_to_gamma",       w_linearToGamma,      MRB_ARGS_KEY(1, 0) },
	{ "is_convex",             w_isConvex,           MRB_ARGS_KEY(1, 0) },
	{ "triangulate",           w_triangulate,        MRB_ARGS_KEY(1, 0) },
	{ nullptr, nullptr, 0 }
};

// Equivalent of luaopen_love_math: creates the module, registers Love::Math and
// the Love::RandomGenerator type.
extern "C" void mrb_love_math_init(mrb_state *mrb)
{
	Math *inst = instance();
	if (inst == nullptr)
		inst = new Math();
	else
		inst->retain();

	WrappedModule w;
	w.module = inst;
	w.name = "Math";
	w.type = &Module::type;
	w.functions = functions;

	mrbx_register_module(mrb, w);
	mrbx_register_type(mrb, RandomGenerator::type, rg_functions);
}

} // math
} // love
