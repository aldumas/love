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
#include "common/Matrix.h"
#include "MathModule.h"
#include "RandomGenerator.h"
#include "BezierCurve.h"
#include "Transform.h"

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

// Returns a flat Ruby array [x0,y0,x1,y1,...] from a Vector2 list.
static mrb_value pushVector2Array(mrb_state *mrb, const std::vector<Vector2> &points)
{
	mrb_value arr = mrb_ary_new_capa(mrb, (mrb_int) points.size() * 2);
	for (const Vector2 &p : points)
	{
		mrb_ary_push(mrb, arr, mrbx_number(mrb, p.x));
		mrb_ary_push(mrb, arr, mrbx_number(mrb, p.y));
	}
	return arr;
}

// Returns a Ruby array [x, y].
static mrb_value pushPoint(mrb_state *mrb, const Vector2 &p)
{
	mrb_value xy[2] = { mrbx_number(mrb, p.x), mrbx_number(mrb, p.y) };
	return mrb_ary_new_from_values(mrb, 2, xy);
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
// Love::BezierCurve instance methods
//
// Control points are 0-indexed here (Ruby convention), unlike the 1-indexed
// Lua API.
// =========================================================================

static mrb_value bc_getDegree(mrb_state *mrb, mrb_value self)
{
	return mrbx_integer(mrb, (int) mrbx_checktype<BezierCurve>(mrb, self)->getDegree());
}

static mrb_value bc_getControlPointCount(mrb_state *mrb, mrb_value self)
{
	return mrbx_integer(mrb, (int) mrbx_checktype<BezierCurve>(mrb, self)->getControlPointCount());
}

static mrb_value bc_getDerivative(mrb_state *mrb, mrb_value self)
{
	BezierCurve *deriv = new BezierCurve(mrbx_checktype<BezierCurve>(mrb, self)->getDerivative());
	mrb_value out = mrbx_pushtype(mrb, deriv);
	deriv->release();
	return out;
}

static mrb_value bc_getControlPoint(mrb_state *mrb, mrb_value self)
{
	BezierCurve *curve = mrbx_checktype<BezierCurve>(mrb, self);
	mrb_value v[1];
	mrbx_get_kwargs(mrb, {"i"}, 1, v);
	int i = mrbx_checkint(mrb, v[0]);
	mrb_value out = mrb_nil_value();
	bool err = mrbx_catchexcept(mrb, [&]() { out = pushPoint(mrb, curve->getControlPoint(i)); });
	return err ? mrb_nil_value() : out;
}

static mrb_value bc_setControlPoint(mrb_state *mrb, mrb_value self)
{
	BezierCurve *curve = mrbx_checktype<BezierCurve>(mrb, self);
	mrb_value v[3];
	mrbx_get_kwargs(mrb, {"i", "x", "y"}, 3, v);
	int i = mrbx_checkint(mrb, v[0]);
	Vector2 p(mrbx_checkfloat(mrb, v[1]), mrbx_checkfloat(mrb, v[2]));
	mrbx_catchexcept(mrb, [&]() { curve->setControlPoint(i, p); });
	return self;
}

static mrb_value bc_insertControlPoint(mrb_state *mrb, mrb_value self)
{
	BezierCurve *curve = mrbx_checktype<BezierCurve>(mrb, self);
	mrb_value v[3];
	mrbx_get_kwargs(mrb, {"x", "y", "position"}, 2, v);
	Vector2 p(mrbx_checkfloat(mrb, v[0]), mrbx_checkfloat(mrb, v[1]));
	int pos = mrbx_optint(mrb, v[2], -1);
	mrbx_catchexcept(mrb, [&]() { curve->insertControlPoint(p, pos); });
	return self;
}

static mrb_value bc_removeControlPoint(mrb_state *mrb, mrb_value self)
{
	BezierCurve *curve = mrbx_checktype<BezierCurve>(mrb, self);
	mrb_value v[1];
	mrbx_get_kwargs(mrb, {"i"}, 1, v);
	int i = mrbx_checkint(mrb, v[0]);
	mrbx_catchexcept(mrb, [&]() { curve->removeControlPoint(i); });
	return self;
}

static mrb_value bc_translate(mrb_state *mrb, mrb_value self)
{
	BezierCurve *curve = mrbx_checktype<BezierCurve>(mrb, self);
	mrb_value v[2];
	mrbx_get_kwargs(mrb, {"dx", "dy"}, 2, v);
	curve->translate(Vector2(mrbx_checkfloat(mrb, v[0]), mrbx_checkfloat(mrb, v[1])));
	return self;
}

static mrb_value bc_rotate(mrb_state *mrb, mrb_value self)
{
	BezierCurve *curve = mrbx_checktype<BezierCurve>(mrb, self);
	mrb_value v[3];
	mrbx_get_kwargs(mrb, {"angle", "ox", "oy"}, 1, v);
	curve->rotate(mrbx_checknumber(mrb, v[0]), Vector2(mrbx_optfloat(mrb, v[1], 0), mrbx_optfloat(mrb, v[2], 0)));
	return self;
}

static mrb_value bc_scale(mrb_state *mrb, mrb_value self)
{
	BezierCurve *curve = mrbx_checktype<BezierCurve>(mrb, self);
	mrb_value v[3];
	mrbx_get_kwargs(mrb, {"factor", "ox", "oy"}, 1, v);
	curve->scale(mrbx_checknumber(mrb, v[0]), Vector2(mrbx_optfloat(mrb, v[1], 0), mrbx_optfloat(mrb, v[2], 0)));
	return self;
}

static mrb_value bc_evaluate(mrb_state *mrb, mrb_value self)
{
	BezierCurve *curve = mrbx_checktype<BezierCurve>(mrb, self);
	mrb_value v[1];
	mrbx_get_kwargs(mrb, {"t"}, 1, v);
	double t = mrbx_checknumber(mrb, v[0]);
	mrb_value out = mrb_nil_value();
	bool err = mrbx_catchexcept(mrb, [&]() { out = pushPoint(mrb, curve->evaluate(t)); });
	return err ? mrb_nil_value() : out;
}

static mrb_value bc_getSegment(mrb_state *mrb, mrb_value self)
{
	BezierCurve *curve = mrbx_checktype<BezierCurve>(mrb, self);
	mrb_value v[2];
	mrbx_get_kwargs(mrb, {"from", "to"}, 2, v);
	double t1 = mrbx_checknumber(mrb, v[0]);
	double t2 = mrbx_checknumber(mrb, v[1]);
	BezierCurve *seg = nullptr;
	bool err = mrbx_catchexcept(mrb, [&]() { seg = curve->getSegment(t1, t2); });
	if (err) return mrb_nil_value();
	mrb_value out = mrbx_pushtype(mrb, seg);
	seg->release();
	return out;
}

static mrb_value bc_render(mrb_state *mrb, mrb_value self)
{
	BezierCurve *curve = mrbx_checktype<BezierCurve>(mrb, self);
	mrb_value v[1];
	mrbx_get_kwargs(mrb, {"accuracy"}, 0, v);
	int accuracy = mrbx_optint(mrb, v[0], 4);
	std::vector<Vector2> points;
	bool err = mrbx_catchexcept(mrb, [&]() { points = curve->render(accuracy); });
	return err ? mrb_nil_value() : pushVector2Array(mrb, points);
}

static mrb_value bc_renderSegment(mrb_state *mrb, mrb_value self)
{
	BezierCurve *curve = mrbx_checktype<BezierCurve>(mrb, self);
	mrb_value v[3];
	mrbx_get_kwargs(mrb, {"from", "to", "accuracy"}, 2, v);
	double start = mrbx_checknumber(mrb, v[0]);
	double end = mrbx_checknumber(mrb, v[1]);
	int accuracy = mrbx_optint(mrb, v[2], 4);
	std::vector<Vector2> points;
	bool err = mrbx_catchexcept(mrb, [&]() { points = curve->renderSegment(start, end, accuracy); });
	return err ? mrb_nil_value() : pushVector2Array(mrb, points);
}

static const MrbReg bc_functions[] =
{
	{ "get_degree",              bc_getDegree,            MRB_ARGS_NONE() },
	{ "get_derivative",          bc_getDerivative,        MRB_ARGS_NONE() },
	{ "get_control_point",       bc_getControlPoint,      MRB_ARGS_KEY(1, 0) },
	{ "set_control_point",       bc_setControlPoint,      MRB_ARGS_KEY(3, 0) },
	{ "insert_control_point",    bc_insertControlPoint,   MRB_ARGS_KEY(3, 0) },
	{ "remove_control_point",    bc_removeControlPoint,   MRB_ARGS_KEY(1, 0) },
	{ "get_control_point_count", bc_getControlPointCount, MRB_ARGS_NONE() },
	{ "translate",               bc_translate,            MRB_ARGS_KEY(2, 0) },
	{ "rotate",                  bc_rotate,               MRB_ARGS_KEY(3, 0) },
	{ "scale",                   bc_scale,                MRB_ARGS_KEY(3, 0) },
	{ "evaluate",                bc_evaluate,             MRB_ARGS_KEY(1, 0) },
	{ "get_segment",             bc_getSegment,           MRB_ARGS_KEY(2, 0) },
	{ "render",                  bc_render,               MRB_ARGS_KEY(1, 0) },
	{ "render_segment",          bc_renderSegment,        MRB_ARGS_KEY(3, 0) },
	{ nullptr, nullptr, 0 }
};

// =========================================================================
// Love::Transform instance methods
//
// Mutating methods return self so calls can be chained, matching the Lua API.
// =========================================================================

static mrb_value tf_clone(mrb_state *mrb, mrb_value self)
{
	Transform *t = mrbx_checktype<Transform>(mrb, self)->clone();
	mrb_value out = mrbx_pushtype(mrb, t);
	t->release();
	return out;
}

static mrb_value tf_inverse(mrb_state *mrb, mrb_value self)
{
	Transform *t = mrbx_checktype<Transform>(mrb, self)->inverse();
	mrb_value out = mrbx_pushtype(mrb, t);
	t->release();
	return out;
}

static mrb_value tf_apply(mrb_state *mrb, mrb_value self)
{
	Transform *t = mrbx_checktype<Transform>(mrb, self);
	mrb_value v[1];
	mrbx_get_kwargs(mrb, {"other"}, 1, v);
	t->apply(mrbx_checktype<Transform>(mrb, v[0]));
	return self;
}

static mrb_value tf_isAffine2DTransform(mrb_state *mrb, mrb_value self)
{
	return mrbx_boolean(mrb, mrbx_checktype<Transform>(mrb, self)->getMatrix().isAffine2DTransform());
}

static mrb_value tf_translate(mrb_state *mrb, mrb_value self)
{
	Transform *t = mrbx_checktype<Transform>(mrb, self);
	mrb_value v[2];
	mrbx_get_kwargs(mrb, {"x", "y"}, 2, v);
	t->translate(mrbx_checkfloat(mrb, v[0]), mrbx_checkfloat(mrb, v[1]));
	return self;
}

static mrb_value tf_rotate(mrb_state *mrb, mrb_value self)
{
	Transform *t = mrbx_checktype<Transform>(mrb, self);
	mrb_value v[1];
	mrbx_get_kwargs(mrb, {"angle"}, 1, v);
	t->rotate(mrbx_checkfloat(mrb, v[0]));
	return self;
}

static mrb_value tf_scale(mrb_state *mrb, mrb_value self)
{
	Transform *t = mrbx_checktype<Transform>(mrb, self);
	mrb_value v[2];
	mrbx_get_kwargs(mrb, {"sx", "sy"}, 1, v);
	float sx = mrbx_checkfloat(mrb, v[0]);
	t->scale(sx, mrbx_optfloat(mrb, v[1], sx));
	return self;
}

static mrb_value tf_shear(mrb_state *mrb, mrb_value self)
{
	Transform *t = mrbx_checktype<Transform>(mrb, self);
	mrb_value v[2];
	mrbx_get_kwargs(mrb, {"kx", "ky"}, 2, v);
	t->shear(mrbx_checkfloat(mrb, v[0]), mrbx_checkfloat(mrb, v[1]));
	return self;
}

static mrb_value tf_reset(mrb_state *mrb, mrb_value self)
{
	mrbx_checktype<Transform>(mrb, self)->reset();
	return self;
}

static mrb_value tf_setTransformation(mrb_state *mrb, mrb_value self)
{
	Transform *t = mrbx_checktype<Transform>(mrb, self);
	mrb_value v[9];
	mrbx_get_kwargs(mrb, {"x", "y", "angle", "sx", "sy", "ox", "oy", "kx", "ky"}, 0, v);
	float x  = mrbx_optfloat(mrb, v[0], 0.0f);
	float y  = mrbx_optfloat(mrb, v[1], 0.0f);
	float a  = mrbx_optfloat(mrb, v[2], 0.0f);
	float sx = mrbx_optfloat(mrb, v[3], 1.0f);
	float sy = mrbx_optfloat(mrb, v[4], sx);
	float ox = mrbx_optfloat(mrb, v[5], 0.0f);
	float oy = mrbx_optfloat(mrb, v[6], 0.0f);
	float kx = mrbx_optfloat(mrb, v[7], 0.0f);
	float ky = mrbx_optfloat(mrb, v[8], 0.0f);
	t->setTransformation(x, y, a, sx, sy, ox, oy, kx, ky);
	return self;
}

static mrb_value tf_getMatrix(mrb_state *mrb, mrb_value self)
{
	Transform *t = mrbx_checktype<Transform>(mrb, self);
	const float *e = t->getMatrix().getElements();
	// Returned in row-major order (stored column-major).
	mrb_value arr = mrb_ary_new_capa(mrb, 16);
	for (int row = 0; row < 4; row++)
		for (int col = 0; col < 4; col++)
			mrb_ary_push(mrb, arr, mrbx_number(mrb, e[col * 4 + row]));
	return arr;
}

static mrb_value tf_setMatrix(mrb_state *mrb, mrb_value self)
{
	Transform *t = mrbx_checktype<Transform>(mrb, self);
	mrb_value v[2];
	mrbx_get_kwargs(mrb, {"elements", "layout"}, 1, v);

	if (!mrb_array_p(v[0]) || RARRAY_LEN(v[0]) < 16)
		mrb_raise(mrb, E_ARGUMENT_ERROR, "elements: must be an array of 16 numbers");

	std::string layout = mrbx_optstring(mrb, v[1], "row");
	bool columnmajor = layout == "column";

	float in[16];
	for (int i = 0; i < 16; i++)
		in[i] = (float) mrb_as_float(mrb, mrb_ary_ref(mrb, v[0], i));

	float e[16];
	if (columnmajor)
	{
		for (int i = 0; i < 16; i++)
			e[i] = in[i];
	}
	else
	{
		// Convert row-major input to column-major storage.
		for (int col = 0; col < 4; col++)
			for (int row = 0; row < 4; row++)
				e[col * 4 + row] = in[row * 4 + col];
	}

	t->setMatrix(Matrix4(e));
	return self;
}

static mrb_value tf_transformPoint(mrb_state *mrb, mrb_value self)
{
	Transform *t = mrbx_checktype<Transform>(mrb, self);
	mrb_value v[2];
	mrbx_get_kwargs(mrb, {"x", "y"}, 2, v);
	Vector2 p = t->transformPoint(Vector2(mrbx_checkfloat(mrb, v[0]), mrbx_checkfloat(mrb, v[1])));
	return pushPoint(mrb, p);
}

static mrb_value tf_inverseTransformPoint(mrb_state *mrb, mrb_value self)
{
	Transform *t = mrbx_checktype<Transform>(mrb, self);
	mrb_value v[2];
	mrbx_get_kwargs(mrb, {"x", "y"}, 2, v);
	Vector2 p = t->inverseTransformPoint(Vector2(mrbx_checkfloat(mrb, v[0]), mrbx_checkfloat(mrb, v[1])));
	return pushPoint(mrb, p);
}

// Transform * Transform -> Transform (composition). Positional, since it's an
// operator: t1 * t2.
static mrb_value tf_mul(mrb_state *mrb, mrb_value self)
{
	Transform *t1 = mrbx_checktype<Transform>(mrb, self);
	mrb_value other;
	mrb_get_args(mrb, "o", &other);
	Transform *t2 = mrbx_checktype<Transform>(mrb, other);
	Transform *t3 = new Transform(t1->getMatrix() * t2->getMatrix());
	mrb_value out = mrbx_pushtype(mrb, t3);
	t3->release();
	return out;
}

static const MrbReg tf_functions[] =
{
	{ "clone",                   tf_clone,                MRB_ARGS_NONE() },
	{ "inverse",                 tf_inverse,              MRB_ARGS_NONE() },
	{ "apply",                   tf_apply,                MRB_ARGS_KEY(1, 0) },
	{ "is_affine_2d_transform",  tf_isAffine2DTransform,  MRB_ARGS_NONE() },
	{ "translate",               tf_translate,            MRB_ARGS_KEY(2, 0) },
	{ "rotate",                  tf_rotate,               MRB_ARGS_KEY(1, 0) },
	{ "scale",                   tf_scale,                MRB_ARGS_KEY(2, 0) },
	{ "shear",                   tf_shear,                MRB_ARGS_KEY(2, 0) },
	{ "reset",                   tf_reset,                MRB_ARGS_NONE() },
	{ "set_transformation",      tf_setTransformation,    MRB_ARGS_KEY(9, 0) },
	{ "set_matrix",              tf_setMatrix,            MRB_ARGS_KEY(2, 0) },
	{ "get_matrix",              tf_getMatrix,            MRB_ARGS_NONE() },
	{ "transform_point",         tf_transformPoint,       MRB_ARGS_KEY(2, 0) },
	{ "inverse_transform_point", tf_inverseTransformPoint, MRB_ARGS_KEY(2, 0) },
	{ "*",                       tf_mul,                  MRB_ARGS_REQ(1) },
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

static mrb_value w_newBezierCurve(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[1];
	mrbx_get_kwargs(mrb, {"points"}, 1, v);
	std::vector<Vector2> points = checkVector2Array(mrb, v[0]);

	BezierCurve *curve = instance()->newBezierCurve(points);
	mrb_value out = mrbx_pushtype(mrb, curve);
	curve->release();
	return out;
}

static mrb_value w_newTransform(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[9];
	mrbx_get_kwargs(mrb, {"x", "y", "angle", "sx", "sy", "ox", "oy", "kx", "ky"}, 0, v);

	float sx = mrbx_optfloat(mrb, v[3], 1.0f);
	Transform *t = instance()->newTransform(
		mrbx_optfloat(mrb, v[0], 0.0f), mrbx_optfloat(mrb, v[1], 0.0f), mrbx_optfloat(mrb, v[2], 0.0f),
		sx, mrbx_optfloat(mrb, v[4], sx), mrbx_optfloat(mrb, v[5], 0.0f), mrbx_optfloat(mrb, v[6], 0.0f),
		mrbx_optfloat(mrb, v[7], 0.0f), mrbx_optfloat(mrb, v[8], 0.0f));

	mrb_value out = mrbx_pushtype(mrb, t);
	t->release();
	return out;
}

static const MrbReg functions[] =
{
	{ "random",                w_random,             MRB_ARGS_KEY(2, 0) },
	{ "random_normal",         w_randomNormal,       MRB_ARGS_KEY(2, 0) },
	{ "set_random_seed",       w_setRandomSeed,      MRB_ARGS_KEY(1, 0) },
	{ "new_random_generator",  w_newRandomGenerator, MRB_ARGS_KEY(1, 0) },
	{ "new_bezier_curve",      w_newBezierCurve,     MRB_ARGS_KEY(1, 0) },
	{ "new_transform",         w_newTransform,       MRB_ARGS_KEY(9, 0) },
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
	mrbx_register_type(mrb, BezierCurve::type, bc_functions);
	mrbx_register_type(mrb, Transform::type, tf_functions);
}

} // math
} // love
