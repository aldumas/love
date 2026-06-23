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

// mruby port of wrap_Physics.cpp + wrap_World/Body/Shape/CircleShape/
// PolygonShape/EdgeShape/ChainShape. First physics slice: create a world, add
// bodies with shapes, step the simulation and read back transforms. Keyword
// arguments, snake_case, `?`-suffixed predicates, multi-value returns as Hashes.
//
// Deferred to later slices (see PORTING.md): collision callbacks, contacts,
// joints, world/shape ray-casts and AABB queries, shape mass/AABB queries,
// polygon/edge vertex readback, and arbitrary user data. The engine .cpp files
// guard those Lua-only sections behind LOVE_MRUBY.

#include "common/config.h"
#include "common/mrb_runtime.h"

#include "Physics.h"
#include "World.h"
#include "Body.h"
#include "Shape.h"
#include "CircleShape.h"
#include "PolygonShape.h"
#include "EdgeShape.h"
#include "ChainShape.h"

#include <bitset>
#include <vector>

namespace love
{
namespace physics
{
namespace box2d
{

#define instance() (Module::getInstance<Physics>(Module::M_PHYSICS))

// --- shared helpers ------------------------------------------------------

static mrb_value pushXY(mrb_state *mrb, float x, float y)
{
	mrb_value h = mrb_hash_new(mrb);
	mrb_hash_set(mrb, h, mrb_symbol_value(mrb_intern_lit(mrb, "x")), mrbx_number(mrb, x));
	mrb_hash_set(mrb, h, mrb_symbol_value(mrb_intern_lit(mrb, "y")), mrbx_number(mrb, y));
	return h;
}

static Body::Type checkBodyType(mrb_state *mrb, mrb_value v)
{
	std::string s = mrbx_checkstring(mrb, v);
	Body::Type t = Body::BODY_STATIC;
	if (!Body::getConstant(s.c_str(), t))
		mrb_raisef(mrb, E_ARGUMENT_ERROR, "Invalid body type: %s", s.c_str());
	return t;
}

static Body *optBody(mrb_state *mrb, mrb_value v)
{
	if (mrb_undef_p(v) || mrb_nil_p(v))
		return nullptr;
	return mrbx_checktype<Body>(mrb, v);
}

// Reads a flat Ruby array [x0,y0,x1,y1,...] into a Vector2 list.
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

// Pushes a Shape with its concrete Ruby class so type-specific methods resolve.
static mrb_value pushShape(mrb_state *mrb, Shape *shape)
{
	if (shape == nullptr)
		return mrb_nil_value();

	switch (shape->getType())
	{
	case Shape::SHAPE_CIRCLE:  return mrbx_pushtype(mrb, CircleShape::type,  shape);
	case Shape::SHAPE_POLYGON: return mrbx_pushtype(mrb, PolygonShape::type, shape);
	case Shape::SHAPE_EDGE:    return mrbx_pushtype(mrb, EdgeShape::type,    shape);
	case Shape::SHAPE_CHAIN:   return mrbx_pushtype(mrb, ChainShape::type,   shape);
	default:                   return mrbx_pushtype(mrb, Shape::type,        shape);
	}
}

// A 16-bit category bitfield <-> a Ruby array of 1..16 indices.
static uint16 bitsFromArray(mrb_state *mrb, mrb_value arr)
{
	if (!mrb_array_p(arr))
		mrb_raise(mrb, E_TYPE_ERROR, "expected an array of category indices (1..16)");
	std::bitset<16> b;
	mrb_int n = RARRAY_LEN(arr);
	for (mrb_int i = 0; i < n; i++)
	{
		int v = mrbx_checkint(mrb, mrb_ary_ref(mrb, arr, i));
		if (v < 1 || v > 16)
			mrb_raise(mrb, E_ARGUMENT_ERROR, "category values must be in range 1-16");
		b.set((size_t)(v - 1), true);
	}
	return (uint16) b.to_ulong();
}

static mrb_value arrayFromBits(mrb_state *mrb, uint16 bits)
{
	std::bitset<16> b((int) bits);
	mrb_value arr = mrb_ary_new(mrb);
	for (int i = 0; i < 16; i++)
		if (b.test(i))
			mrb_ary_push(mrb, arr, mrbx_integer(mrb, i + 1));
	return arr;
}

// =========================================================================
// Love::Shape (base) instance methods — inherited by every concrete shape.
// =========================================================================

static mrb_value shape_getType(mrb_state *mrb, mrb_value self)
{
	Shape *s = mrbx_checktype<Shape>(mrb, self);
	const char *str = nullptr;
	Shape::getConstant(s->getType(), str);
	return str ? mrbx_string(mrb, str) : mrb_nil_value();
}

static mrb_value shape_getRadius(mrb_state *mrb, mrb_value self)
{
	return mrbx_number(mrb, mrbx_checktype<Shape>(mrb, self)->getRadius());
}

static mrb_value shape_getChildCount(mrb_state *mrb, mrb_value self)
{
	return mrbx_integer(mrb, mrbx_checktype<Shape>(mrb, self)->getChildCount());
}

static mrb_value shape_setFriction(mrb_state *mrb, mrb_value self)
{
	mrb_value v[1];
	mrbx_get_kwargs(mrb, {"friction"}, 1, v);
	Shape *s = mrbx_checktype<Shape>(mrb, self);
	mrbx_catchexcept(mrb, [&]() { s->setFriction(mrbx_checkfloat(mrb, v[0])); });
	return self;
}

static mrb_value shape_getFriction(mrb_state *mrb, mrb_value self)
{
	Shape *s = mrbx_checktype<Shape>(mrb, self);
	float r = 0;
	mrbx_catchexcept(mrb, [&]() { r = s->getFriction(); });
	return mrbx_number(mrb, r);
}

static mrb_value shape_setRestitution(mrb_state *mrb, mrb_value self)
{
	mrb_value v[1];
	mrbx_get_kwargs(mrb, {"restitution"}, 1, v);
	Shape *s = mrbx_checktype<Shape>(mrb, self);
	mrbx_catchexcept(mrb, [&]() { s->setRestitution(mrbx_checkfloat(mrb, v[0])); });
	return self;
}

static mrb_value shape_getRestitution(mrb_state *mrb, mrb_value self)
{
	Shape *s = mrbx_checktype<Shape>(mrb, self);
	float r = 0;
	mrbx_catchexcept(mrb, [&]() { r = s->getRestitution(); });
	return mrbx_number(mrb, r);
}

static mrb_value shape_setDensity(mrb_state *mrb, mrb_value self)
{
	mrb_value v[1];
	mrbx_get_kwargs(mrb, {"density"}, 1, v);
	Shape *s = mrbx_checktype<Shape>(mrb, self);
	mrbx_catchexcept(mrb, [&]() { s->setDensity(mrbx_checkfloat(mrb, v[0])); });
	return self;
}

static mrb_value shape_getDensity(mrb_state *mrb, mrb_value self)
{
	Shape *s = mrbx_checktype<Shape>(mrb, self);
	float r = 0;
	mrbx_catchexcept(mrb, [&]() { r = s->getDensity(); });
	return mrbx_number(mrb, r);
}

static mrb_value shape_setSensor(mrb_state *mrb, mrb_value self)
{
	mrb_value v[1];
	mrbx_get_kwargs(mrb, {"sensor"}, 1, v);
	Shape *s = mrbx_checktype<Shape>(mrb, self);
	mrbx_catchexcept(mrb, [&]() { s->setSensor(mrbx_checkboolean(mrb, v[0])); });
	return self;
}

static mrb_value shape_isSensor(mrb_state *mrb, mrb_value self)
{
	Shape *s = mrbx_checktype<Shape>(mrb, self);
	bool r = false;
	mrbx_catchexcept(mrb, [&]() { r = s->isSensor(); });
	return mrbx_boolean(mrb, r);
}

static mrb_value shape_getBody(mrb_state *mrb, mrb_value self)
{
	Body *b = mrbx_checktype<Shape>(mrb, self)->getBody();
	return b ? mrbx_pushtype(mrb, b) : mrb_nil_value();
}

static mrb_value shape_testPoint(mrb_state *mrb, mrb_value self)
{
	mrb_value v[5];
	mrbx_get_kwargs(mrb, {"x", "y", "r", "px", "py"}, 2, v);
	Shape *s = mrbx_checktype<Shape>(mrb, self);
	float x = mrbx_checkfloat(mrb, v[0]);
	float y = mrbx_checkfloat(mrb, v[1]);
	bool r = false;
	if (!mrb_undef_p(v[2]))
	{
		float angle = mrbx_checkfloat(mrb, v[2]);
		float px = mrbx_checkfloat(mrb, v[3]);
		float py = mrbx_checkfloat(mrb, v[4]);
		mrbx_catchexcept(mrb, [&]() { r = s->testPoint(x, y, angle, px, py); });
	}
	else
		mrbx_catchexcept(mrb, [&]() { r = s->testPoint(x, y); });
	return mrbx_boolean(mrb, r);
}

static mrb_value shape_setGroupIndex(mrb_state *mrb, mrb_value self)
{
	mrb_value v[1];
	mrbx_get_kwargs(mrb, {"index"}, 1, v);
	Shape *s = mrbx_checktype<Shape>(mrb, self);
	mrbx_catchexcept(mrb, [&]() { s->setGroupIndex(mrbx_checkint(mrb, v[0])); });
	return self;
}

static mrb_value shape_getGroupIndex(mrb_state *mrb, mrb_value self)
{
	Shape *s = mrbx_checktype<Shape>(mrb, self);
	int r = 0;
	mrbx_catchexcept(mrb, [&]() { r = s->getGroupIndex(); });
	return mrbx_integer(mrb, r);
}

// category/mask reimplement Shape::{set,get}{Category,Mask} (#phys-shape-filter)
// over the public get/setFilterData(int*) API. v = {categoryBits, maskBits, group}.
static mrb_value shape_setCategory(mrb_state *mrb, mrb_value self)
{
	mrb_value v[1];
	mrbx_get_kwargs(mrb, {"categories"}, 1, v);
	Shape *s = mrbx_checktype<Shape>(mrb, self);
	uint16 bits = bitsFromArray(mrb, v[0]);
	mrbx_catchexcept(mrb, [&]() {
		int f[3];
		s->getFilterData(f);
		f[0] = (int) bits;
		s->setFilterData(f);
	});
	return self;
}

static mrb_value shape_getCategory(mrb_state *mrb, mrb_value self)
{
	Shape *s = mrbx_checktype<Shape>(mrb, self);
	int f[3] = {0, 0, 0};
	mrbx_catchexcept(mrb, [&]() { s->getFilterData(f); });
	return arrayFromBits(mrb, (uint16) f[0]);
}

static mrb_value shape_setMask(mrb_state *mrb, mrb_value self)
{
	mrb_value v[1];
	mrbx_get_kwargs(mrb, {"categories"}, 1, v);
	Shape *s = mrbx_checktype<Shape>(mrb, self);
	uint16 bits = bitsFromArray(mrb, v[0]);
	mrbx_catchexcept(mrb, [&]() {
		int f[3];
		s->getFilterData(f);
		f[1] = (int) (uint16) ~bits; // mask stores the complement, as in Shape::setMask
		s->setFilterData(f);
	});
	return self;
}

static mrb_value shape_getMask(mrb_state *mrb, mrb_value self)
{
	Shape *s = mrbx_checktype<Shape>(mrb, self);
	int f[3] = {0, 0, 0};
	mrbx_catchexcept(mrb, [&]() { s->getFilterData(f); });
	return arrayFromBits(mrb, (uint16) ~((uint16) f[1]));
}

// pushes a {distance:, point...} / AABB-style Hash with the given symbol keys.
static mrb_value pushAABB(mrb_state *mrb, const b2AABB &box)
{
	b2AABB b = Physics::scaleUp(box);
	mrb_value h = mrb_hash_new(mrb);
	mrb_hash_set(mrb, h, mrb_symbol_value(mrb_intern_lit(mrb, "top_left_x")),     mrbx_number(mrb, b.lowerBound.x));
	mrb_hash_set(mrb, h, mrb_symbol_value(mrb_intern_lit(mrb, "top_left_y")),     mrbx_number(mrb, b.lowerBound.y));
	mrb_hash_set(mrb, h, mrb_symbol_value(mrb_intern_lit(mrb, "bottom_right_x")), mrbx_number(mrb, b.upperBound.x));
	mrb_hash_set(mrb, h, mrb_symbol_value(mrb_intern_lit(mrb, "bottom_right_y")), mrbx_number(mrb, b.upperBound.y));
	return h;
}

// #phys-shape-query: rayCast / computeAABB / computeMass / getBoundingBox /
// getMassData reimplemented over the b2Shape/b2Fixture exposed by Shape, since
// the wrapper isn't a friend of the engine class. Faithful to Shape.cpp's
// scaling and 1-based child indexing.
static mrb_value shape_rayCast(mrb_state *mrb, mrb_value self)
{
	mrb_value v[9];
	mrbx_get_kwargs(mrb, {"x1", "y1", "x2", "y2", "max_fraction", "x", "y", "r", "child_index"}, 5, v);
	Shape *s = mrbx_checktype<Shape>(mrb, self);

	b2RayCastInput input;
	b2RayCastOutput output;
	input.p1.Set(Physics::scaleDown(mrbx_checkfloat(mrb, v[0])), Physics::scaleDown(mrbx_checkfloat(mrb, v[1])));
	input.p2.Set(Physics::scaleDown(mrbx_checkfloat(mrb, v[2])), Physics::scaleDown(mrbx_checkfloat(mrb, v[3])));
	input.maxFraction = mrbx_checkfloat(mrb, v[4]);

	bool hit = false;
	bool err = mrbx_catchexcept(mrb, [&]() {
		// With x/y/r given, ray-cast against the bare shape at a transform;
		// otherwise against the live fixture (which must be in a world).
		if (!mrb_undef_p(v[5]) && !mrb_undef_p(v[6]) && !mrb_undef_p(v[7]))
		{
			s->throwIfShapeNotValid();
			float x = Physics::scaleDown(mrbx_checkfloat(mrb, v[5]));
			float y = Physics::scaleDown(mrbx_checkfloat(mrb, v[6]));
			float r = mrbx_checkfloat(mrb, v[7]);
			int childIndex = mrbx_optint(mrb, v[8], 1) - 1;
			b2Transform transform(b2Vec2(x, y), b2Rot(r));
			hit = s->getBox2DShape()->RayCast(&output, input, transform, childIndex);
		}
		else
		{
			s->throwIfFixtureNotValid();
			int childIndex = mrbx_optint(mrb, v[8], 1) - 1;
			hit = s->getFixture()->RayCast(&output, input, childIndex);
		}
	});
	if (err || !hit)
		return mrb_nil_value();

	mrb_value h = mrb_hash_new(mrb);
	mrb_hash_set(mrb, h, mrb_symbol_value(mrb_intern_lit(mrb, "normal_x")), mrbx_number(mrb, output.normal.x));
	mrb_hash_set(mrb, h, mrb_symbol_value(mrb_intern_lit(mrb, "normal_y")), mrbx_number(mrb, output.normal.y));
	mrb_hash_set(mrb, h, mrb_symbol_value(mrb_intern_lit(mrb, "fraction")), mrbx_number(mrb, output.fraction));
	return h;
}

static mrb_value shape_computeAABB(mrb_state *mrb, mrb_value self)
{
	mrb_value v[4];
	mrbx_get_kwargs(mrb, {"x", "y", "r", "child_index"}, 3, v);
	Shape *s = mrbx_checktype<Shape>(mrb, self);
	float x = Physics::scaleDown(mrbx_checkfloat(mrb, v[0]));
	float y = Physics::scaleDown(mrbx_checkfloat(mrb, v[1]));
	float r = mrbx_checkfloat(mrb, v[2]);
	int childIndex = mrbx_optint(mrb, v[3], 1) - 1;
	b2AABB box;
	bool err = mrbx_catchexcept(mrb, [&]() {
		s->throwIfShapeNotValid();
		b2Transform transform(b2Vec2(x, y), b2Rot(r));
		s->getBox2DShape()->ComputeAABB(&box, transform, childIndex);
	});
	return err ? mrb_nil_value() : pushAABB(mrb, box);
}

static mrb_value shape_computeMass(mrb_state *mrb, mrb_value self)
{
	mrb_value v[1];
	mrbx_get_kwargs(mrb, {"density"}, 1, v);
	Shape *s = mrbx_checktype<Shape>(mrb, self);
	float density = mrbx_checkfloat(mrb, v[0]);
	b2MassData data;
	bool err = mrbx_catchexcept(mrb, [&]() {
		s->throwIfShapeNotValid();
		s->getBox2DShape()->ComputeMass(&data, density);
	});
	if (err) return mrb_nil_value();
	b2Vec2 center = Physics::scaleUp(data.center);
	mrb_value h = pushXY(mrb, center.x, center.y);
	mrb_hash_set(mrb, h, mrb_symbol_value(mrb_intern_lit(mrb, "mass")), mrbx_number(mrb, data.mass));
	mrb_hash_set(mrb, h, mrb_symbol_value(mrb_intern_lit(mrb, "inertia")),
		mrbx_number(mrb, Physics::scaleUp(Physics::scaleUp(data.I))));
	return h;
}

static mrb_value shape_getBoundingBox(mrb_state *mrb, mrb_value self)
{
	mrb_value v[1];
	mrbx_get_kwargs(mrb, {"child_index"}, 0, v);
	Shape *s = mrbx_checktype<Shape>(mrb, self);
	int childIndex = mrbx_optint(mrb, v[0], 1) - 1;
	b2AABB box;
	bool err = mrbx_catchexcept(mrb, [&]() {
		s->throwIfFixtureNotValid();
		box = s->getFixture()->GetAABB(childIndex);
	});
	return err ? mrb_nil_value() : pushAABB(mrb, box);
}

static mrb_value shape_getMassData(mrb_state *mrb, mrb_value self)
{
	Shape *s = mrbx_checktype<Shape>(mrb, self);
	b2MassData data;
	bool err = mrbx_catchexcept(mrb, [&]() {
		s->throwIfFixtureNotValid();
		s->getFixture()->GetMassData(&data);
	});
	if (err) return mrb_nil_value();
	b2Vec2 center = Physics::scaleUp(data.center);
	mrb_value h = pushXY(mrb, center.x, center.y);
	mrb_hash_set(mrb, h, mrb_symbol_value(mrb_intern_lit(mrb, "mass")), mrbx_number(mrb, data.mass));
	// getMassData (unlike computeMass) leaves I unscaled, faithful to Shape.cpp.
	mrb_hash_set(mrb, h, mrb_symbol_value(mrb_intern_lit(mrb, "inertia")), mrbx_number(mrb, data.I));
	return h;
}

static mrb_value shape_isValid(mrb_state *mrb, mrb_value self)
{
	return mrbx_boolean(mrb, mrbx_checktype<Shape>(mrb, self)->isValid());
}

static mrb_value shape_destroy(mrb_state *mrb, mrb_value self)
{
	Shape *s = mrbx_checktype<Shape>(mrb, self);
	mrbx_catchexcept(mrb, [&]() { s->destroy(); });
	return mrb_nil_value();
}

static const MrbReg shape_functions[] =
{
	{ "get_type",        shape_getType,       MRB_ARGS_NONE() },
	{ "get_radius",      shape_getRadius,     MRB_ARGS_NONE() },
	{ "get_child_count", shape_getChildCount, MRB_ARGS_NONE() },
	{ "set_friction",    shape_setFriction,   MRB_ARGS_KEY(1, 0) },
	{ "get_friction",    shape_getFriction,   MRB_ARGS_NONE() },
	{ "set_restitution", shape_setRestitution, MRB_ARGS_KEY(1, 0) },
	{ "get_restitution", shape_getRestitution, MRB_ARGS_NONE() },
	{ "set_density",     shape_setDensity,    MRB_ARGS_KEY(1, 0) },
	{ "get_density",     shape_getDensity,    MRB_ARGS_NONE() },
	{ "set_sensor",      shape_setSensor,     MRB_ARGS_KEY(1, 0) },
	{ "sensor?",         shape_isSensor,      MRB_ARGS_NONE() },
	{ "get_body",        shape_getBody,       MRB_ARGS_NONE() },
	{ "test_point",      shape_testPoint,     MRB_ARGS_KEY(5, 0) },
	{ "set_group_index", shape_setGroupIndex, MRB_ARGS_KEY(1, 0) },
	{ "get_group_index", shape_getGroupIndex, MRB_ARGS_NONE() },
	{ "set_category",    shape_setCategory,   MRB_ARGS_KEY(1, 0) },
	{ "get_category",    shape_getCategory,   MRB_ARGS_NONE() },
	{ "set_mask",        shape_setMask,       MRB_ARGS_KEY(1, 0) },
	{ "get_mask",        shape_getMask,       MRB_ARGS_NONE() },
	{ "ray_cast",        shape_rayCast,       MRB_ARGS_KEY(9, 0) },
	{ "compute_aabb",    shape_computeAABB,   MRB_ARGS_KEY(4, 0) },
	{ "compute_mass",    shape_computeMass,   MRB_ARGS_KEY(1, 0) },
	{ "get_bounding_box", shape_getBoundingBox, MRB_ARGS_KEY(1, 0) },
	{ "get_mass_data",   shape_getMassData,   MRB_ARGS_NONE() },
	{ "valid?",          shape_isValid,       MRB_ARGS_NONE() },
	{ "destroy",         shape_destroy,       MRB_ARGS_NONE() },
	{ nullptr, nullptr, 0 }
};

// =========================================================================
// Love::CircleShape
// =========================================================================

static mrb_value circle_setRadius(mrb_state *mrb, mrb_value self)
{
	mrb_value v[1];
	mrbx_get_kwargs(mrb, {"radius"}, 1, v);
	mrbx_checktype<CircleShape>(mrb, self)->setRadius(mrbx_checkfloat(mrb, v[0]));
	return self;
}

static mrb_value circle_getPoint(mrb_state *mrb, mrb_value self)
{
	float x = 0, y = 0;
	mrbx_checktype<CircleShape>(mrb, self)->getPoint(x, y);
	return pushXY(mrb, x, y);
}

static mrb_value circle_setPoint(mrb_state *mrb, mrb_value self)
{
	mrb_value v[2];
	mrbx_get_kwargs(mrb, {"x", "y"}, 2, v);
	mrbx_checktype<CircleShape>(mrb, self)->setPoint(mrbx_checkfloat(mrb, v[0]), mrbx_checkfloat(mrb, v[1]));
	return self;
}

static const MrbReg circle_functions[] =
{
	{ "set_radius", circle_setRadius, MRB_ARGS_KEY(1, 0) },
	{ "get_point",  circle_getPoint,  MRB_ARGS_NONE() },
	{ "set_point",  circle_setPoint,  MRB_ARGS_KEY(2, 0) },
	{ nullptr, nullptr, 0 }
};

// =========================================================================
// Love::PolygonShape
// =========================================================================

static mrb_value polygon_validate(mrb_state *mrb, mrb_value self)
{
	return mrbx_boolean(mrb, mrbx_checktype<PolygonShape>(mrb, self)->validate());
}

// #phys-shape-points: transformed vertex readback as a flat [x0,y0,x1,y1,...]
// array, reimplemented over the b2PolygonShape exposed by Shape.
static mrb_value polygon_getPoints(mrb_state *mrb, mrb_value self)
{
	PolygonShape *s = mrbx_checktype<PolygonShape>(mrb, self);
	mrb_value arr = mrb_nil_value();
	mrbx_catchexcept(mrb, [&]() {
		s->throwIfShapeNotValid();
		b2PolygonShape *p = (b2PolygonShape *) s->getBox2DShape();
		arr = mrb_ary_new_capa(mrb, p->m_count * 2);
		for (int i = 0; i < p->m_count; i++)
		{
			b2Vec2 v = Physics::scaleUp(p->m_vertices[i]);
			mrb_ary_push(mrb, arr, mrbx_number(mrb, v.x));
			mrb_ary_push(mrb, arr, mrbx_number(mrb, v.y));
		}
	});
	return arr;
}

static const MrbReg polygon_functions[] =
{
	{ "validate",   polygon_validate,  MRB_ARGS_NONE() },
	{ "get_points", polygon_getPoints, MRB_ARGS_NONE() },
	{ nullptr, nullptr, 0 }
};

// =========================================================================
// Love::EdgeShape
// =========================================================================

static mrb_value edge_setNextVertex(mrb_state *mrb, mrb_value self)
{
	mrb_value v[2];
	mrbx_get_kwargs(mrb, {"x", "y"}, 2, v);
	mrbx_checktype<EdgeShape>(mrb, self)->setNextVertex(mrbx_checkfloat(mrb, v[0]), mrbx_checkfloat(mrb, v[1]));
	return self;
}

static mrb_value edge_getNextVertex(mrb_state *mrb, mrb_value self)
{
	b2Vec2 v = mrbx_checktype<EdgeShape>(mrb, self)->getNextVertex();
	return pushXY(mrb, v.x, v.y);
}

static mrb_value edge_setPreviousVertex(mrb_state *mrb, mrb_value self)
{
	mrb_value v[2];
	mrbx_get_kwargs(mrb, {"x", "y"}, 2, v);
	mrbx_checktype<EdgeShape>(mrb, self)->setPreviousVertex(mrbx_checkfloat(mrb, v[0]), mrbx_checkfloat(mrb, v[1]));
	return self;
}

static mrb_value edge_getPreviousVertex(mrb_state *mrb, mrb_value self)
{
	b2Vec2 v = mrbx_checktype<EdgeShape>(mrb, self)->getPreviousVertex();
	return pushXY(mrb, v.x, v.y);
}

// #phys-shape-points: the edge's two transformed endpoints as [x1,y1,x2,y2].
static mrb_value edge_getPoints(mrb_state *mrb, mrb_value self)
{
	EdgeShape *s = mrbx_checktype<EdgeShape>(mrb, self);
	mrb_value arr = mrb_nil_value();
	mrbx_catchexcept(mrb, [&]() {
		s->throwIfShapeNotValid();
		b2EdgeShape *e = (b2EdgeShape *) s->getBox2DShape();
		b2Vec2 v1 = Physics::scaleUp(e->m_vertex1);
		b2Vec2 v2 = Physics::scaleUp(e->m_vertex2);
		arr = mrb_ary_new_capa(mrb, 4);
		mrb_ary_push(mrb, arr, mrbx_number(mrb, v1.x));
		mrb_ary_push(mrb, arr, mrbx_number(mrb, v1.y));
		mrb_ary_push(mrb, arr, mrbx_number(mrb, v2.x));
		mrb_ary_push(mrb, arr, mrbx_number(mrb, v2.y));
	});
	return arr;
}

static const MrbReg edge_functions[] =
{
	{ "set_next_vertex",     edge_setNextVertex,     MRB_ARGS_KEY(2, 0) },
	{ "get_next_vertex",     edge_getNextVertex,     MRB_ARGS_NONE() },
	{ "set_previous_vertex", edge_setPreviousVertex, MRB_ARGS_KEY(2, 0) },
	{ "get_previous_vertex", edge_getPreviousVertex, MRB_ARGS_NONE() },
	{ "get_points",          edge_getPoints,         MRB_ARGS_NONE() },
	{ nullptr, nullptr, 0 }
};

// =========================================================================
// Love::ChainShape
// =========================================================================

static mrb_value chain_setNextVertex(mrb_state *mrb, mrb_value self)
{
	mrb_value v[2];
	mrbx_get_kwargs(mrb, {"x", "y"}, 2, v);
	ChainShape *c = mrbx_checktype<ChainShape>(mrb, self);
	mrbx_catchexcept(mrb, [&]() { c->setNextVertex(mrbx_checkfloat(mrb, v[0]), mrbx_checkfloat(mrb, v[1])); });
	return self;
}

static mrb_value chain_setPreviousVertex(mrb_state *mrb, mrb_value self)
{
	mrb_value v[2];
	mrbx_get_kwargs(mrb, {"x", "y"}, 2, v);
	ChainShape *c = mrbx_checktype<ChainShape>(mrb, self);
	mrbx_catchexcept(mrb, [&]() { c->setPreviousVertex(mrbx_checkfloat(mrb, v[0]), mrbx_checkfloat(mrb, v[1])); });
	return self;
}

static mrb_value chain_getNextVertex(mrb_state *mrb, mrb_value self)
{
	b2Vec2 v = mrbx_checktype<ChainShape>(mrb, self)->getNextVertex();
	return pushXY(mrb, v.x, v.y);
}

static mrb_value chain_getPreviousVertex(mrb_state *mrb, mrb_value self)
{
	b2Vec2 v = mrbx_checktype<ChainShape>(mrb, self)->getPreviousVertex();
	return pushXY(mrb, v.x, v.y);
}

static mrb_value chain_getVertexCount(mrb_state *mrb, mrb_value self)
{
	return mrbx_integer(mrb, mrbx_checktype<ChainShape>(mrb, self)->getVertexCount());
}

static mrb_value chain_getPoint(mrb_state *mrb, mrb_value self)
{
	mrb_value v[1];
	mrbx_get_kwargs(mrb, {"index"}, 1, v);
	ChainShape *c = mrbx_checktype<ChainShape>(mrb, self);
	// 1-based index, as elsewhere in the LÖVE API.
	int i = mrbx_checkint(mrb, v[0]) - 1;
	b2Vec2 p;
	bool err = mrbx_catchexcept(mrb, [&]() { p = c->getPoint(i); });
	return err ? mrb_nil_value() : pushXY(mrb, p.x, p.y);
}

static mrb_value chain_getPoints(mrb_state *mrb, mrb_value self)
{
	ChainShape *c = mrbx_checktype<ChainShape>(mrb, self);
	int count = c->getVertexCount();
	const b2Vec2 *pts = c->getPoints();
	mrb_value arr = mrb_ary_new_capa(mrb, count * 2);
	for (int i = 0; i < count; i++)
	{
		b2Vec2 v = Physics::scaleUp(pts[i]);
		mrb_ary_push(mrb, arr, mrbx_number(mrb, v.x));
		mrb_ary_push(mrb, arr, mrbx_number(mrb, v.y));
	}
	return arr;
}

static const MrbReg chain_functions[] =
{
	{ "set_next_vertex",     chain_setNextVertex,     MRB_ARGS_KEY(2, 0) },
	{ "set_previous_vertex", chain_setPreviousVertex, MRB_ARGS_KEY(2, 0) },
	{ "get_next_vertex",     chain_getNextVertex,     MRB_ARGS_NONE() },
	{ "get_previous_vertex", chain_getPreviousVertex, MRB_ARGS_NONE() },
	{ "get_vertex_count",    chain_getVertexCount,    MRB_ARGS_NONE() },
	{ "get_point",           chain_getPoint,          MRB_ARGS_KEY(1, 0) },
	{ "get_points",          chain_getPoints,         MRB_ARGS_NONE() },
	{ nullptr, nullptr, 0 }
};

// =========================================================================
// Love::Body
// =========================================================================

#define BODY (mrbx_checktype<Body>(mrb, self))

static mrb_value body_getX(mrb_state *mrb, mrb_value self) { return mrbx_number(mrb, BODY->getX()); }
static mrb_value body_getY(mrb_state *mrb, mrb_value self) { return mrbx_number(mrb, BODY->getY()); }
static mrb_value body_getAngle(mrb_state *mrb, mrb_value self) { return mrbx_number(mrb, BODY->getAngle()); }
static mrb_value body_getMass(mrb_state *mrb, mrb_value self) { return mrbx_number(mrb, BODY->getMass()); }
static mrb_value body_getInertia(mrb_state *mrb, mrb_value self) { return mrbx_number(mrb, BODY->getInertia()); }
static mrb_value body_getAngularVelocity(mrb_state *mrb, mrb_value self) { return mrbx_number(mrb, BODY->getAngularVelocity()); }
static mrb_value body_getAngularDamping(mrb_state *mrb, mrb_value self) { return mrbx_number(mrb, BODY->getAngularDamping()); }
static mrb_value body_getLinearDamping(mrb_state *mrb, mrb_value self) { return mrbx_number(mrb, BODY->getLinearDamping()); }
static mrb_value body_getGravityScale(mrb_state *mrb, mrb_value self) { return mrbx_number(mrb, BODY->getGravityScale()); }

static mrb_value body_getPosition(mrb_state *mrb, mrb_value self)
{
	float x, y; BODY->getPosition(x, y); return pushXY(mrb, x, y);
}
static mrb_value body_getLinearVelocity(mrb_state *mrb, mrb_value self)
{
	float x, y; BODY->getLinearVelocity(x, y); return pushXY(mrb, x, y);
}
static mrb_value body_getWorldCenter(mrb_state *mrb, mrb_value self)
{
	float x, y; BODY->getWorldCenter(x, y); return pushXY(mrb, x, y);
}
static mrb_value body_getLocalCenter(mrb_state *mrb, mrb_value self)
{
	float x, y; BODY->getLocalCenter(x, y); return pushXY(mrb, x, y);
}

static mrb_value body_getType(mrb_state *mrb, mrb_value self)
{
	const char *str = nullptr;
	Body::getConstant(BODY->getType(), str);
	return str ? mrbx_string(mrb, str) : mrb_nil_value();
}

static mrb_value body_setType(mrb_state *mrb, mrb_value self)
{
	mrb_value v[1];
	mrbx_get_kwargs(mrb, {"type"}, 1, v);
	Body *b = BODY;
	Body::Type t = checkBodyType(mrb, v[0]);
	mrbx_catchexcept(mrb, [&]() { b->setType(t); });
	return self;
}

static mrb_value body_getMassData(mrb_state *mrb, mrb_value self)
{
	Body *b = BODY;
	b2MassData data;
	b->body->GetMassData(&data);
	b2Vec2 center = Physics::scaleUp(data.center);
	mrb_value h = pushXY(mrb, center.x, center.y);
	mrb_hash_set(mrb, h, mrb_symbol_value(mrb_intern_lit(mrb, "mass")), mrbx_number(mrb, data.mass));
	mrb_hash_set(mrb, h, mrb_symbol_value(mrb_intern_lit(mrb, "inertia")),
		mrbx_number(mrb, Physics::scaleUp(Physics::scaleUp(data.I))));
	return h;
}

static mrb_value body_setX(mrb_state *mrb, mrb_value self)
{
	mrb_value v[1]; mrbx_get_kwargs(mrb, {"x"}, 1, v);
	Body *b = BODY; mrbx_catchexcept(mrb, [&]() { b->setX(mrbx_checkfloat(mrb, v[0])); });
	return self;
}
static mrb_value body_setY(mrb_state *mrb, mrb_value self)
{
	mrb_value v[1]; mrbx_get_kwargs(mrb, {"y"}, 1, v);
	Body *b = BODY; mrbx_catchexcept(mrb, [&]() { b->setY(mrbx_checkfloat(mrb, v[0])); });
	return self;
}
static mrb_value body_setPosition(mrb_state *mrb, mrb_value self)
{
	mrb_value v[2]; mrbx_get_kwargs(mrb, {"x", "y"}, 2, v);
	Body *b = BODY; mrbx_catchexcept(mrb, [&]() { b->setPosition(mrbx_checkfloat(mrb, v[0]), mrbx_checkfloat(mrb, v[1])); });
	return self;
}
static mrb_value body_setAngle(mrb_state *mrb, mrb_value self)
{
	mrb_value v[1]; mrbx_get_kwargs(mrb, {"angle"}, 1, v);
	Body *b = BODY; mrbx_catchexcept(mrb, [&]() { b->setAngle(mrbx_checkfloat(mrb, v[0])); });
	return self;
}
static mrb_value body_setLinearVelocity(mrb_state *mrb, mrb_value self)
{
	mrb_value v[2]; mrbx_get_kwargs(mrb, {"x", "y"}, 2, v);
	BODY->setLinearVelocity(mrbx_checkfloat(mrb, v[0]), mrbx_checkfloat(mrb, v[1]));
	return self;
}
static mrb_value body_setAngularVelocity(mrb_state *mrb, mrb_value self)
{
	mrb_value v[1]; mrbx_get_kwargs(mrb, {"r"}, 1, v);
	BODY->setAngularVelocity(mrbx_checkfloat(mrb, v[0]));
	return self;
}
static mrb_value body_setAngularDamping(mrb_state *mrb, mrb_value self)
{
	mrb_value v[1]; mrbx_get_kwargs(mrb, {"damping"}, 1, v);
	BODY->setAngularDamping(mrbx_checkfloat(mrb, v[0]));
	return self;
}
static mrb_value body_setLinearDamping(mrb_state *mrb, mrb_value self)
{
	mrb_value v[1]; mrbx_get_kwargs(mrb, {"damping"}, 1, v);
	BODY->setLinearDamping(mrbx_checkfloat(mrb, v[0]));
	return self;
}
static mrb_value body_setGravityScale(mrb_state *mrb, mrb_value self)
{
	mrb_value v[1]; mrbx_get_kwargs(mrb, {"scale"}, 1, v);
	BODY->setGravityScale(mrbx_checkfloat(mrb, v[0]));
	return self;
}
static mrb_value body_setMass(mrb_state *mrb, mrb_value self)
{
	mrb_value v[1]; mrbx_get_kwargs(mrb, {"mass"}, 1, v);
	BODY->setMass(mrbx_checkfloat(mrb, v[0]));
	return self;
}
static mrb_value body_setInertia(mrb_state *mrb, mrb_value self)
{
	mrb_value v[1]; mrbx_get_kwargs(mrb, {"inertia"}, 1, v);
	BODY->setInertia(mrbx_checkfloat(mrb, v[0]));
	return self;
}
static mrb_value body_setMassData(mrb_state *mrb, mrb_value self)
{
	mrb_value v[4]; mrbx_get_kwargs(mrb, {"x", "y", "mass", "inertia"}, 4, v);
	BODY->setMassData(mrbx_checkfloat(mrb, v[0]), mrbx_checkfloat(mrb, v[1]),
		mrbx_checkfloat(mrb, v[2]), mrbx_checkfloat(mrb, v[3]));
	return self;
}
static mrb_value body_resetMassData(mrb_state *mrb, mrb_value self)
{
	BODY->resetMassData();
	return self;
}

static mrb_value body_applyForce(mrb_state *mrb, mrb_value self)
{
	mrb_value v[5];
	mrbx_get_kwargs(mrb, {"x", "y", "rx", "ry", "wake"}, 2, v);
	Body *b = BODY;
	float fx = mrbx_checkfloat(mrb, v[0]);
	float fy = mrbx_checkfloat(mrb, v[1]);
	bool wake = mrbx_optboolean(mrb, v[4], true);
	if (!mrb_undef_p(v[2]) && !mrb_undef_p(v[3]))
		b->applyForce(fx, fy, mrbx_checkfloat(mrb, v[2]), mrbx_checkfloat(mrb, v[3]), wake);
	else
		b->applyForce(fx, fy, wake);
	return self;
}

static mrb_value body_applyLinearImpulse(mrb_state *mrb, mrb_value self)
{
	mrb_value v[5];
	mrbx_get_kwargs(mrb, {"x", "y", "rx", "ry", "wake"}, 2, v);
	Body *b = BODY;
	float jx = mrbx_checkfloat(mrb, v[0]);
	float jy = mrbx_checkfloat(mrb, v[1]);
	bool wake = mrbx_optboolean(mrb, v[4], true);
	if (!mrb_undef_p(v[2]) && !mrb_undef_p(v[3]))
		b->applyLinearImpulse(jx, jy, mrbx_checkfloat(mrb, v[2]), mrbx_checkfloat(mrb, v[3]), wake);
	else
		b->applyLinearImpulse(jx, jy, wake);
	return self;
}

static mrb_value body_applyAngularImpulse(mrb_state *mrb, mrb_value self)
{
	mrb_value v[2];
	mrbx_get_kwargs(mrb, {"impulse", "wake"}, 1, v);
	BODY->applyAngularImpulse(mrbx_checkfloat(mrb, v[0]), mrbx_optboolean(mrb, v[1], true));
	return self;
}

static mrb_value body_applyTorque(mrb_state *mrb, mrb_value self)
{
	mrb_value v[2];
	mrbx_get_kwargs(mrb, {"torque", "wake"}, 1, v);
	BODY->applyTorque(mrbx_checkfloat(mrb, v[0]), mrbx_optboolean(mrb, v[1], true));
	return self;
}

// local<->world transforms ------------------------------------------------

static mrb_value body_getWorldPoint(mrb_state *mrb, mrb_value self)
{
	mrb_value v[2]; mrbx_get_kwargs(mrb, {"x", "y"}, 2, v);
	float ox, oy; BODY->getWorldPoint(mrbx_checkfloat(mrb, v[0]), mrbx_checkfloat(mrb, v[1]), ox, oy);
	return pushXY(mrb, ox, oy);
}
static mrb_value body_getWorldVector(mrb_state *mrb, mrb_value self)
{
	mrb_value v[2]; mrbx_get_kwargs(mrb, {"x", "y"}, 2, v);
	float ox, oy; BODY->getWorldVector(mrbx_checkfloat(mrb, v[0]), mrbx_checkfloat(mrb, v[1]), ox, oy);
	return pushXY(mrb, ox, oy);
}
static mrb_value body_getLocalPoint(mrb_state *mrb, mrb_value self)
{
	mrb_value v[2]; mrbx_get_kwargs(mrb, {"x", "y"}, 2, v);
	float ox, oy; BODY->getLocalPoint(mrbx_checkfloat(mrb, v[0]), mrbx_checkfloat(mrb, v[1]), ox, oy);
	return pushXY(mrb, ox, oy);
}
static mrb_value body_getLocalVector(mrb_state *mrb, mrb_value self)
{
	mrb_value v[2]; mrbx_get_kwargs(mrb, {"x", "y"}, 2, v);
	float ox, oy; BODY->getLocalVector(mrbx_checkfloat(mrb, v[0]), mrbx_checkfloat(mrb, v[1]), ox, oy);
	return pushXY(mrb, ox, oy);
}
static mrb_value body_getLinearVelocityFromWorldPoint(mrb_state *mrb, mrb_value self)
{
	mrb_value v[2]; mrbx_get_kwargs(mrb, {"x", "y"}, 2, v);
	float ox, oy; BODY->getLinearVelocityFromWorldPoint(mrbx_checkfloat(mrb, v[0]), mrbx_checkfloat(mrb, v[1]), ox, oy);
	return pushXY(mrb, ox, oy);
}
static mrb_value body_getLinearVelocityFromLocalPoint(mrb_state *mrb, mrb_value self)
{
	mrb_value v[2]; mrbx_get_kwargs(mrb, {"x", "y"}, 2, v);
	float ox, oy; BODY->getLinearVelocityFromLocalPoint(mrbx_checkfloat(mrb, v[0]), mrbx_checkfloat(mrb, v[1]), ox, oy);
	return pushXY(mrb, ox, oy);
}

// #phys-body-helpers: variadic point transform, here a flat array -> flat array.
static mrb_value body_getWorldPoints(mrb_state *mrb, mrb_value self)
{
	mrb_value v[1]; mrbx_get_kwargs(mrb, {"points"}, 1, v);
	Body *b = BODY;
	std::vector<Vector2> pts = checkVector2Array(mrb, v[0]);
	mrb_value out = mrb_ary_new_capa(mrb, (mrb_int) pts.size() * 2);
	for (const Vector2 &p : pts)
	{
		float ox, oy; b->getWorldPoint(p.x, p.y, ox, oy);
		mrb_ary_push(mrb, out, mrbx_number(mrb, ox));
		mrb_ary_push(mrb, out, mrbx_number(mrb, oy));
	}
	return out;
}
static mrb_value body_getLocalPoints(mrb_state *mrb, mrb_value self)
{
	mrb_value v[1]; mrbx_get_kwargs(mrb, {"points"}, 1, v);
	Body *b = BODY;
	std::vector<Vector2> pts = checkVector2Array(mrb, v[0]);
	mrb_value out = mrb_ary_new_capa(mrb, (mrb_int) pts.size() * 2);
	for (const Vector2 &p : pts)
	{
		float ox, oy; b->getLocalPoint(p.x, p.y, ox, oy);
		mrb_ary_push(mrb, out, mrbx_number(mrb, ox));
		mrb_ary_push(mrb, out, mrbx_number(mrb, oy));
	}
	return out;
}

// flags -------------------------------------------------------------------

static mrb_value body_isBullet(mrb_state *mrb, mrb_value self) { return mrbx_boolean(mrb, BODY->isBullet()); }
static mrb_value body_setBullet(mrb_state *mrb, mrb_value self)
{
	mrb_value v[1]; mrbx_get_kwargs(mrb, {"bullet"}, 1, v);
	BODY->setBullet(mrbx_checkboolean(mrb, v[0])); return self;
}
static mrb_value body_isEnabled(mrb_state *mrb, mrb_value self) { return mrbx_boolean(mrb, BODY->isEnabled()); }
static mrb_value body_setEnabled(mrb_state *mrb, mrb_value self)
{
	mrb_value v[1]; mrbx_get_kwargs(mrb, {"enabled"}, 1, v);
	Body *b = BODY; mrbx_catchexcept(mrb, [&]() { b->setEnabled(mrbx_checkboolean(mrb, v[0])); });
	return self;
}
static mrb_value body_isAwake(mrb_state *mrb, mrb_value self) { return mrbx_boolean(mrb, BODY->isAwake()); }
static mrb_value body_setAwake(mrb_state *mrb, mrb_value self)
{
	mrb_value v[1]; mrbx_get_kwargs(mrb, {"awake"}, 1, v);
	BODY->setAwake(mrbx_checkboolean(mrb, v[0])); return self;
}
static mrb_value body_isSleepingAllowed(mrb_state *mrb, mrb_value self) { return mrbx_boolean(mrb, BODY->isSleepingAllowed()); }
static mrb_value body_setSleepingAllowed(mrb_state *mrb, mrb_value self)
{
	mrb_value v[1]; mrbx_get_kwargs(mrb, {"allowed"}, 1, v);
	BODY->setSleepingAllowed(mrbx_checkboolean(mrb, v[0])); return self;
}
static mrb_value body_isFixedRotation(mrb_state *mrb, mrb_value self) { return mrbx_boolean(mrb, BODY->isFixedRotation()); }
static mrb_value body_setFixedRotation(mrb_state *mrb, mrb_value self)
{
	mrb_value v[1]; mrbx_get_kwargs(mrb, {"fixed"}, 1, v);
	Body *b = BODY; mrbx_catchexcept(mrb, [&]() { b->setFixedRotation(mrbx_checkboolean(mrb, v[0])); });
	return self;
}
static mrb_value body_isTouching(mrb_state *mrb, mrb_value self)
{
	mrb_value v[1]; mrbx_get_kwargs(mrb, {"other"}, 1, v);
	return mrbx_boolean(mrb, BODY->isTouching(mrbx_checktype<Body>(mrb, v[0])));
}

static mrb_value body_getWorld(mrb_state *mrb, mrb_value self)
{
	World *w = BODY->getWorld();
	return w ? mrbx_pushtype(mrb, w) : mrb_nil_value();
}
static mrb_value body_getShape(mrb_state *mrb, mrb_value self)
{
	return pushShape(mrb, BODY->getShape());
}
// #phys-body-helpers: getShapes over body->GetFixtureList().
static mrb_value body_getShapes(mrb_state *mrb, mrb_value self)
{
	Body *b = BODY;
	mrb_value arr = mrb_ary_new(mrb);
	for (b2Fixture *f = b->body->GetFixtureList(); f != nullptr; f = f->GetNext())
	{
		Shape *s = (Shape *)(f->GetUserData().pointer);
		if (s) mrb_ary_push(mrb, arr, pushShape(mrb, s));
	}
	return arr;
}
static mrb_value body_isDestroyed(mrb_state *mrb, mrb_value self)
{
	return mrbx_boolean(mrb, BODY->body == nullptr);
}
static mrb_value body_destroy(mrb_state *mrb, mrb_value self)
{
	Body *b = BODY; mrbx_catchexcept(mrb, [&]() { b->destroy(); });
	return mrb_nil_value();
}

#undef BODY

static const MrbReg body_functions[] =
{
	{ "get_x",                  body_getX,                  MRB_ARGS_NONE() },
	{ "get_y",                  body_getY,                  MRB_ARGS_NONE() },
	{ "get_angle",              body_getAngle,              MRB_ARGS_NONE() },
	{ "get_position",           body_getPosition,           MRB_ARGS_NONE() },
	{ "get_linear_velocity",    body_getLinearVelocity,     MRB_ARGS_NONE() },
	{ "get_world_center",       body_getWorldCenter,        MRB_ARGS_NONE() },
	{ "get_local_center",       body_getLocalCenter,        MRB_ARGS_NONE() },
	{ "get_angular_velocity",   body_getAngularVelocity,    MRB_ARGS_NONE() },
	{ "get_mass",               body_getMass,               MRB_ARGS_NONE() },
	{ "get_inertia",            body_getInertia,            MRB_ARGS_NONE() },
	{ "get_mass_data",          body_getMassData,           MRB_ARGS_NONE() },
	{ "get_angular_damping",    body_getAngularDamping,     MRB_ARGS_NONE() },
	{ "get_linear_damping",     body_getLinearDamping,      MRB_ARGS_NONE() },
	{ "get_gravity_scale",      body_getGravityScale,       MRB_ARGS_NONE() },
	{ "get_type",               body_getType,               MRB_ARGS_NONE() },
	{ "set_type",               body_setType,               MRB_ARGS_KEY(1, 0) },
	{ "set_x",                  body_setX,                  MRB_ARGS_KEY(1, 0) },
	{ "set_y",                  body_setY,                  MRB_ARGS_KEY(1, 0) },
	{ "set_position",           body_setPosition,           MRB_ARGS_KEY(2, 0) },
	{ "set_angle",              body_setAngle,              MRB_ARGS_KEY(1, 0) },
	{ "set_linear_velocity",    body_setLinearVelocity,     MRB_ARGS_KEY(2, 0) },
	{ "set_angular_velocity",   body_setAngularVelocity,    MRB_ARGS_KEY(1, 0) },
	{ "set_angular_damping",    body_setAngularDamping,     MRB_ARGS_KEY(1, 0) },
	{ "set_linear_damping",     body_setLinearDamping,      MRB_ARGS_KEY(1, 0) },
	{ "set_gravity_scale",      body_setGravityScale,       MRB_ARGS_KEY(1, 0) },
	{ "set_mass",               body_setMass,               MRB_ARGS_KEY(1, 0) },
	{ "set_inertia",            body_setInertia,            MRB_ARGS_KEY(1, 0) },
	{ "set_mass_data",          body_setMassData,           MRB_ARGS_KEY(4, 0) },
	{ "reset_mass_data",        body_resetMassData,         MRB_ARGS_NONE() },
	{ "apply_force",            body_applyForce,            MRB_ARGS_KEY(5, 0) },
	{ "apply_linear_impulse",   body_applyLinearImpulse,    MRB_ARGS_KEY(5, 0) },
	{ "apply_angular_impulse",  body_applyAngularImpulse,   MRB_ARGS_KEY(2, 0) },
	{ "apply_torque",           body_applyTorque,           MRB_ARGS_KEY(2, 0) },
	{ "get_world_point",        body_getWorldPoint,         MRB_ARGS_KEY(2, 0) },
	{ "get_world_vector",       body_getWorldVector,        MRB_ARGS_KEY(2, 0) },
	{ "get_local_point",        body_getLocalPoint,         MRB_ARGS_KEY(2, 0) },
	{ "get_local_vector",       body_getLocalVector,        MRB_ARGS_KEY(2, 0) },
	{ "get_world_points",       body_getWorldPoints,        MRB_ARGS_KEY(1, 0) },
	{ "get_local_points",       body_getLocalPoints,        MRB_ARGS_KEY(1, 0) },
	{ "get_linear_velocity_from_world_point", body_getLinearVelocityFromWorldPoint, MRB_ARGS_KEY(2, 0) },
	{ "get_linear_velocity_from_local_point", body_getLinearVelocityFromLocalPoint, MRB_ARGS_KEY(2, 0) },
	{ "bullet?",                body_isBullet,              MRB_ARGS_NONE() },
	{ "set_bullet",             body_setBullet,             MRB_ARGS_KEY(1, 0) },
	{ "enabled?",               body_isEnabled,             MRB_ARGS_NONE() },
	{ "set_enabled",            body_setEnabled,            MRB_ARGS_KEY(1, 0) },
	{ "awake?",                 body_isAwake,               MRB_ARGS_NONE() },
	{ "set_awake",              body_setAwake,              MRB_ARGS_KEY(1, 0) },
	{ "sleeping_allowed?",      body_isSleepingAllowed,     MRB_ARGS_NONE() },
	{ "set_sleeping_allowed",   body_setSleepingAllowed,    MRB_ARGS_KEY(1, 0) },
	{ "fixed_rotation?",        body_isFixedRotation,       MRB_ARGS_NONE() },
	{ "set_fixed_rotation",     body_setFixedRotation,      MRB_ARGS_KEY(1, 0) },
	{ "touching?",              body_isTouching,            MRB_ARGS_KEY(1, 0) },
	{ "get_world",              body_getWorld,              MRB_ARGS_NONE() },
	{ "get_shape",              body_getShape,              MRB_ARGS_NONE() },
	{ "get_shapes",             body_getShapes,             MRB_ARGS_NONE() },
	{ "destroyed?",             body_isDestroyed,           MRB_ARGS_NONE() },
	{ "destroy",                body_destroy,               MRB_ARGS_NONE() },
	{ nullptr, nullptr, 0 }
};

// =========================================================================
// Love::World
// =========================================================================

#define WORLD (mrbx_checktype<World>(mrb, self))

// #phys-query: an AABB-query callback that just collects the overlapping
// shapes into a vector (the Lua CollectCallback built a Lua table). Applies the
// same category-mask filter as the engine's CollectCallback.
namespace
{
class ShapeCollector : public b2QueryCallback
{
public:
	ShapeCollector(uint16 mask) : categoryMask(mask) {}
	bool ReportFixture(b2Fixture *f) override
	{
		if (categoryMask != 0xFFFF && (categoryMask & f->GetFilterData().categoryBits) == 0)
			return true;
		Shape *s = (Shape *)(f->GetUserData().pointer);
		if (s) shapes.push_back(s);
		return true;
	}
	std::vector<Shape *> shapes;
private:
	uint16 categoryMask;
};
} // anonymous

// #phys-raycast: a single ray hit -> { shape:, x:, y:, normal_x:, normal_y:,
// fraction: } (the engine pushed the shape + five numbers).
static mrb_value pushRayHit(mrb_state *mrb, const World::RayCastOneCallback &rc)
{
	Shape *s = (Shape *)(rc.hitFixture->GetUserData().pointer);
	b2Vec2 hp = Physics::scaleUp(rc.hitPoint);
	mrb_value h = mrb_hash_new(mrb);
	mrb_hash_set(mrb, h, mrb_symbol_value(mrb_intern_lit(mrb, "shape")),    pushShape(mrb, s));
	mrb_hash_set(mrb, h, mrb_symbol_value(mrb_intern_lit(mrb, "x")),        mrbx_number(mrb, hp.x));
	mrb_hash_set(mrb, h, mrb_symbol_value(mrb_intern_lit(mrb, "y")),        mrbx_number(mrb, hp.y));
	mrb_hash_set(mrb, h, mrb_symbol_value(mrb_intern_lit(mrb, "normal_x")), mrbx_number(mrb, rc.hitNormal.x));
	mrb_hash_set(mrb, h, mrb_symbol_value(mrb_intern_lit(mrb, "normal_y")), mrbx_number(mrb, rc.hitNormal.y));
	mrb_hash_set(mrb, h, mrb_symbol_value(mrb_intern_lit(mrb, "fraction")), mrbx_number(mrb, rc.hitFraction));
	return h;
}

static mrb_value world_update(mrb_state *mrb, mrb_value self)
{
	mrb_value v[3];
	mrbx_get_kwargs(mrb, {"dt", "velocity_iterations", "position_iterations"}, 1, v);
	World *w = WORLD;
	float dt = mrbx_checkfloat(mrb, v[0]);
	int vi = mrbx_optint(mrb, v[1], 8);
	int pi = mrbx_optint(mrb, v[2], 3);
	mrbx_catchexcept(mrb, [&]() { w->update(dt, vi, pi); });
	return mrb_nil_value();
}

static mrb_value world_getGravity(mrb_state *mrb, mrb_value self)
{
	b2Vec2 v = Physics::scaleUp(WORLD->getBox2DWorld()->GetGravity());
	return pushXY(mrb, v.x, v.y);
}

static mrb_value world_setGravity(mrb_state *mrb, mrb_value self)
{
	mrb_value v[2]; mrbx_get_kwargs(mrb, {"x", "y"}, 2, v);
	WORLD->setGravity(mrbx_checkfloat(mrb, v[0]), mrbx_checkfloat(mrb, v[1]));
	return self;
}

static mrb_value world_translateOrigin(mrb_state *mrb, mrb_value self)
{
	mrb_value v[2]; mrbx_get_kwargs(mrb, {"x", "y"}, 2, v);
	World *w = WORLD;
	mrbx_catchexcept(mrb, [&]() { w->translateOrigin(mrbx_checkfloat(mrb, v[0]), mrbx_checkfloat(mrb, v[1])); });
	return self;
}

static mrb_value world_setSleepingAllowed(mrb_state *mrb, mrb_value self)
{
	mrb_value v[1]; mrbx_get_kwargs(mrb, {"allowed"}, 1, v);
	WORLD->setSleepingAllowed(mrbx_checkboolean(mrb, v[0]));
	return self;
}
static mrb_value world_isSleepingAllowed(mrb_state *mrb, mrb_value self)
{
	return mrbx_boolean(mrb, WORLD->isSleepingAllowed());
}
static mrb_value world_isLocked(mrb_state *mrb, mrb_value self)
{
	return mrbx_boolean(mrb, WORLD->isLocked());
}
static mrb_value world_getBodyCount(mrb_state *mrb, mrb_value self)
{
	return mrbx_integer(mrb, WORLD->getBodyCount());
}
static mrb_value world_getJointCount(mrb_state *mrb, mrb_value self)
{
	return mrbx_integer(mrb, WORLD->getJointCount());
}
static mrb_value world_getContactCount(mrb_state *mrb, mrb_value self)
{
	return mrbx_integer(mrb, WORLD->getContactCount());
}

// #phys-world-helpers: getBodies over the raw b2World body list.
static mrb_value world_getBodies(mrb_state *mrb, mrb_value self)
{
	World *w = WORLD;
	mrb_value arr = mrb_ary_new(mrb);
	b2Body *ground = w->getGroundBody();
	for (b2Body *b = w->getBox2DWorld()->GetBodyList(); b != nullptr; b = b->GetNext())
	{
		if (b == ground)
			continue;
		Body *body = (Body *)(b->GetUserData().pointer);
		if (body) mrb_ary_push(mrb, arr, mrbx_pushtype(mrb, body));
	}
	return arr;
}

// #phys-query: getShapesInArea reimplemented over getBox2DWorld()->QueryAABB +
// the ShapeCollector above. `categories:` is an optional array of 1..16 (default
// all categories). queryShapesInArea (Lua user callback) stays deferred.
static mrb_value world_getShapesInArea(mrb_state *mrb, mrb_value self)
{
	mrb_value v[5];
	mrbx_get_kwargs(mrb, {"x1", "y1", "x2", "y2", "categories"}, 4, v);
	World *w = WORLD;
	float lx = mrbx_checkfloat(mrb, v[0]);
	float ly = mrbx_checkfloat(mrb, v[1]);
	float ux = mrbx_checkfloat(mrb, v[2]);
	float uy = mrbx_checkfloat(mrb, v[3]);
	uint16 mask = mrb_undef_p(v[4]) ? 0xFFFF : bitsFromArray(mrb, v[4]);
	b2AABB box;
	box.lowerBound = Physics::scaleDown(b2Vec2(lx, ly));
	box.upperBound = Physics::scaleDown(b2Vec2(ux, uy));
	ShapeCollector collector(mask);
	mrbx_catchexcept(mrb, [&]() { w->getBox2DWorld()->QueryAABB(&collector, box); });
	mrb_value arr = mrb_ary_new_capa(mrb, (mrb_int) collector.shapes.size());
	for (Shape *s : collector.shapes)
		mrb_ary_push(mrb, arr, pushShape(mrb, s));
	return arr;
}

// #phys-raycast: rayCastAny / rayCastClosest, sharing the engine's
// RayCastOneCallback. Returns the ray-hit Hash or nil. The full rayCast (a Lua
// callback invoked per fixture hit) stays deferred.
static mrb_value worldRayCastOne(mrb_state *mrb, mrb_value self, bool any)
{
	mrb_value v[5];
	mrbx_get_kwargs(mrb, {"x1", "y1", "x2", "y2", "categories"}, 4, v);
	World *w = WORLD;
	float x1 = mrbx_checkfloat(mrb, v[0]);
	float y1 = mrbx_checkfloat(mrb, v[1]);
	float x2 = mrbx_checkfloat(mrb, v[2]);
	float y2 = mrbx_checkfloat(mrb, v[3]);
	uint16 mask = mrb_undef_p(v[4]) ? 0xFFFF : bitsFromArray(mrb, v[4]);
	b2Vec2 p1 = Physics::scaleDown(b2Vec2(x1, y1));
	b2Vec2 p2 = Physics::scaleDown(b2Vec2(x2, y2));
	World::RayCastOneCallback rc(mask, any);
	mrbx_catchexcept(mrb, [&]() { w->getBox2DWorld()->RayCast(&rc, p1, p2); });
	return rc.hitFixture ? pushRayHit(mrb, rc) : mrb_nil_value();
}

static mrb_value world_rayCastAny(mrb_state *mrb, mrb_value self)
{
	return worldRayCastOne(mrb, self, true);
}

static mrb_value world_rayCastClosest(mrb_state *mrb, mrb_value self)
{
	return worldRayCastOne(mrb, self, false);
}

static mrb_value world_isDestroyed(mrb_state *mrb, mrb_value self)
{
	return mrbx_boolean(mrb, !WORLD->isValid());
}
static mrb_value world_destroy(mrb_state *mrb, mrb_value self)
{
	World *w = WORLD;
	mrbx_catchexcept(mrb, [&]() { w->destroy(); });
	return mrb_nil_value();
}

#undef WORLD

static const MrbReg world_functions[] =
{
	{ "update",              world_update,             MRB_ARGS_KEY(3, 0) },
	{ "get_gravity",         world_getGravity,         MRB_ARGS_NONE() },
	{ "set_gravity",         world_setGravity,         MRB_ARGS_KEY(2, 0) },
	{ "translate_origin",    world_translateOrigin,    MRB_ARGS_KEY(2, 0) },
	{ "set_sleeping_allowed", world_setSleepingAllowed, MRB_ARGS_KEY(1, 0) },
	{ "sleeping_allowed?",   world_isSleepingAllowed,  MRB_ARGS_NONE() },
	{ "locked?",             world_isLocked,           MRB_ARGS_NONE() },
	{ "get_body_count",      world_getBodyCount,       MRB_ARGS_NONE() },
	{ "get_joint_count",     world_getJointCount,      MRB_ARGS_NONE() },
	{ "get_contact_count",   world_getContactCount,    MRB_ARGS_NONE() },
	{ "get_bodies",          world_getBodies,          MRB_ARGS_NONE() },
	{ "get_shapes_in_area",  world_getShapesInArea,    MRB_ARGS_KEY(5, 0) },
	{ "ray_cast_any",        world_rayCastAny,         MRB_ARGS_KEY(5, 0) },
	{ "ray_cast_closest",    world_rayCastClosest,     MRB_ARGS_KEY(5, 0) },
	{ "destroyed?",          world_isDestroyed,        MRB_ARGS_NONE() },
	{ "destroy",             world_destroy,            MRB_ARGS_NONE() },
	{ nullptr, nullptr, 0 }
};

// =========================================================================
// Love::Physics module functions
// =========================================================================

static mrb_value w_setMeter(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[1]; mrbx_get_kwargs(mrb, {"scale"}, 1, v);
	mrbx_catchexcept(mrb, [&]() { Physics::setMeter(mrbx_checkfloat(mrb, v[0])); });
	return mrb_nil_value();
}

static mrb_value w_getMeter(mrb_state *mrb, mrb_value self)
{
	(void) self;
	return mrbx_number(mrb, Physics::getMeter());
}

static mrb_value w_newWorld(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[3];
	mrbx_get_kwargs(mrb, {"gx", "gy", "sleep"}, 0, v);
	float gx = mrbx_optfloat(mrb, v[0], 0.0f);
	float gy = mrbx_optfloat(mrb, v[1], 0.0f);
	bool sleep = mrbx_optboolean(mrb, v[2], true);
	World *world = nullptr;
	bool err = mrbx_catchexcept(mrb, [&]() { world = instance()->newWorld(gx, gy, sleep); });
	if (err) return mrb_nil_value();
	mrb_value out = mrbx_pushtype(mrb, world);
	world->release();
	return out;
}

static mrb_value w_newBody(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[4];
	mrbx_get_kwargs(mrb, {"world", "x", "y", "type"}, 1, v);
	World *world = mrbx_checktype<World>(mrb, v[0]);
	float x = mrbx_optfloat(mrb, v[1], 0.0f);
	float y = mrbx_optfloat(mrb, v[2], 0.0f);
	Body::Type t = mrb_undef_p(v[3]) ? Body::BODY_STATIC : checkBodyType(mrb, v[3]);
	Body *body = nullptr;
	bool err = mrbx_catchexcept(mrb, [&]() { body = instance()->newBody(world, x, y, t); });
	if (err) return mrb_nil_value();
	mrb_value out = mrbx_pushtype(mrb, body);
	body->release();
	return out;
}

static mrb_value w_newCircleShape(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[4];
	mrbx_get_kwargs(mrb, {"body", "radius", "x", "y"}, 1, v);
	Body *body = optBody(mrb, v[0]);
	float radius = mrbx_checkfloat(mrb, v[1]);
	float x = mrbx_optfloat(mrb, v[2], 0.0f);
	float y = mrbx_optfloat(mrb, v[3], 0.0f);
	CircleShape *shape = nullptr;
	bool err = mrbx_catchexcept(mrb, [&]() { shape = instance()->newCircleShape(body, x, y, radius); });
	if (err) return mrb_nil_value();
	mrb_value out = mrbx_pushtype(mrb, shape);
	shape->release();
	return out;
}

static mrb_value w_newRectangleShape(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[6];
	mrbx_get_kwargs(mrb, {"body", "width", "height", "x", "y", "angle"}, 2, v);
	Body *body = optBody(mrb, v[0]);
	float w = mrbx_checkfloat(mrb, v[1]);
	float h = mrbx_checkfloat(mrb, v[2]);
	float x = mrbx_optfloat(mrb, v[3], 0.0f);
	float y = mrbx_optfloat(mrb, v[4], 0.0f);
	float angle = mrbx_optfloat(mrb, v[5], 0.0f);
	PolygonShape *shape = nullptr;
	bool err = mrbx_catchexcept(mrb, [&]() { shape = instance()->newRectangleShape(body, x, y, w, h, angle); });
	if (err) return mrb_nil_value();
	mrb_value out = mrbx_pushtype(mrb, shape);
	shape->release();
	return out;
}

static mrb_value w_newPolygonShape(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[2];
	mrbx_get_kwargs(mrb, {"points", "body"}, 1, v);
	std::vector<Vector2> pts = checkVector2Array(mrb, v[0]);
	Body *body = optBody(mrb, v[1]);
	PolygonShape *shape = nullptr;
	bool err = mrbx_catchexcept(mrb, [&]() { shape = instance()->newPolygonShape(body, pts.data(), (int) pts.size()); });
	if (err) return mrb_nil_value();
	mrb_value out = mrbx_pushtype(mrb, shape);
	shape->release();
	return out;
}

static mrb_value w_newEdgeShape(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[5];
	mrbx_get_kwargs(mrb, {"x1", "y1", "x2", "y2", "body"}, 4, v);
	float x1 = mrbx_checkfloat(mrb, v[0]);
	float y1 = mrbx_checkfloat(mrb, v[1]);
	float x2 = mrbx_checkfloat(mrb, v[2]);
	float y2 = mrbx_checkfloat(mrb, v[3]);
	Body *body = optBody(mrb, v[4]);
	EdgeShape *shape = nullptr;
	bool err = mrbx_catchexcept(mrb, [&]() { shape = instance()->newEdgeShape(body, x1, y1, x2, y2); });
	if (err) return mrb_nil_value();
	mrb_value out = mrbx_pushtype(mrb, shape);
	shape->release();
	return out;
}

static mrb_value w_newChainShape(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[3];
	mrbx_get_kwargs(mrb, {"points", "loop", "body"}, 1, v);
	std::vector<Vector2> pts = checkVector2Array(mrb, v[0]);
	bool loop = mrbx_optboolean(mrb, v[1], false);
	Body *body = optBody(mrb, v[2]);
	ChainShape *shape = nullptr;
	bool err = mrbx_catchexcept(mrb, [&]() { shape = instance()->newChainShape(body, loop, pts.data(), (int) pts.size()); });
	if (err) return mrb_nil_value();
	mrb_value out = mrbx_pushtype(mrb, shape);
	shape->release();
	return out;
}

// body+shape combo creators: return [body, shape].
static mrb_value w_newCircleBody(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[5];
	mrbx_get_kwargs(mrb, {"world", "x", "y", "radius", "type"}, 4, v);
	World *world = mrbx_checktype<World>(mrb, v[0]);
	float x = mrbx_checkfloat(mrb, v[1]);
	float y = mrbx_checkfloat(mrb, v[2]);
	float radius = mrbx_checkfloat(mrb, v[3]);
	Body::Type t = mrb_undef_p(v[4]) ? Body::BODY_STATIC : checkBodyType(mrb, v[4]);
	Body *body = nullptr; CircleShape *shape = nullptr;
	bool err = mrbx_catchexcept(mrb, [&]() { body = instance()->newCircleBody(world, t, x, y, radius, shape); });
	if (err) return mrb_nil_value();
	mrb_value pair[2] = { mrbx_pushtype(mrb, body), mrbx_pushtype(mrb, shape) };
	body->release(); shape->release();
	return mrb_ary_new_from_values(mrb, 2, pair);
}

static mrb_value w_newRectangleBody(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[7];
	mrbx_get_kwargs(mrb, {"world", "x", "y", "width", "height", "angle", "type"}, 5, v);
	World *world = mrbx_checktype<World>(mrb, v[0]);
	float x = mrbx_checkfloat(mrb, v[1]);
	float y = mrbx_checkfloat(mrb, v[2]);
	float w = mrbx_checkfloat(mrb, v[3]);
	float h = mrbx_checkfloat(mrb, v[4]);
	float angle = mrbx_optfloat(mrb, v[5], 0.0f);
	Body::Type t = mrb_undef_p(v[6]) ? Body::BODY_STATIC : checkBodyType(mrb, v[6]);
	Body *body = nullptr; PolygonShape *shape = nullptr;
	bool err = mrbx_catchexcept(mrb, [&]() { body = instance()->newRectangleBody(world, t, x, y, w, h, angle, shape); });
	if (err) return mrb_nil_value();
	mrb_value pair[2] = { mrbx_pushtype(mrb, body), mrbx_pushtype(mrb, shape) };
	body->release(); shape->release();
	return mrb_ary_new_from_values(mrb, 2, pair);
}

static mrb_value w_newPolygonBody(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[3];
	mrbx_get_kwargs(mrb, {"world", "points", "type"}, 2, v);
	World *world = mrbx_checktype<World>(mrb, v[0]);
	std::vector<Vector2> pts = checkVector2Array(mrb, v[1]);
	Body::Type t = mrb_undef_p(v[2]) ? Body::BODY_STATIC : checkBodyType(mrb, v[2]);
	Body *body = nullptr; PolygonShape *shape = nullptr;
	bool err = mrbx_catchexcept(mrb, [&]() { body = instance()->newPolygonBody(world, t, pts.data(), (int) pts.size(), shape); });
	if (err) return mrb_nil_value();
	mrb_value pair[2] = { mrbx_pushtype(mrb, body), mrbx_pushtype(mrb, shape) };
	body->release(); shape->release();
	return mrb_ary_new_from_values(mrb, 2, pair);
}

static mrb_value w_newEdgeBody(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[6];
	mrbx_get_kwargs(mrb, {"world", "x1", "y1", "x2", "y2", "type"}, 5, v);
	World *world = mrbx_checktype<World>(mrb, v[0]);
	float x1 = mrbx_checkfloat(mrb, v[1]);
	float y1 = mrbx_checkfloat(mrb, v[2]);
	float x2 = mrbx_checkfloat(mrb, v[3]);
	float y2 = mrbx_checkfloat(mrb, v[4]);
	Body::Type t = mrb_undef_p(v[5]) ? Body::BODY_STATIC : checkBodyType(mrb, v[5]);
	Body *body = nullptr; EdgeShape *shape = nullptr;
	bool err = mrbx_catchexcept(mrb, [&]() { body = instance()->newEdgeBody(world, t, x1, y1, x2, y2, shape); });
	if (err) return mrb_nil_value();
	mrb_value pair[2] = { mrbx_pushtype(mrb, body), mrbx_pushtype(mrb, shape) };
	body->release(); shape->release();
	return mrb_ary_new_from_values(mrb, 2, pair);
}

static mrb_value w_newChainBody(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[4];
	mrbx_get_kwargs(mrb, {"world", "points", "loop", "type"}, 2, v);
	World *world = mrbx_checktype<World>(mrb, v[0]);
	std::vector<Vector2> pts = checkVector2Array(mrb, v[1]);
	bool loop = mrbx_optboolean(mrb, v[2], false);
	Body::Type t = mrb_undef_p(v[3]) ? Body::BODY_STATIC : checkBodyType(mrb, v[3]);
	Body *body = nullptr; ChainShape *shape = nullptr;
	bool err = mrbx_catchexcept(mrb, [&]() { body = instance()->newChainBody(world, t, loop, pts.data(), (int) pts.size(), shape); });
	if (err) return mrb_nil_value();
	mrb_value pair[2] = { mrbx_pushtype(mrb, body), mrbx_pushtype(mrb, shape) };
	body->release(); shape->release();
	return mrb_ary_new_from_values(mrb, 2, pair);
}

// #phys-distance: the distance between two active shapes, reimplemented over
// the b2Fixture exposed by Shape (Physics::getDistance pushed Lua values).
static mrb_value w_getDistance(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[2];
	mrbx_get_kwargs(mrb, {"shape_a", "shape_b"}, 2, v);
	Shape *a = mrbx_checktype<Shape>(mrb, v[0]);
	Shape *b = mrbx_checktype<Shape>(mrb, v[1]);

	b2DistanceProxy pA, pB;
	b2DistanceInput i;
	b2DistanceOutput o;
	b2SimplexCache c;
	c.count = 0;

	bool err = mrbx_catchexcept(mrb, [&]() {
		if (!a->isValid() || !b->isValid())
			throw love::Exception("The given Shape is not active in the physics World.");
		pA.Set(a->getFixture()->GetShape(), 0);
		pB.Set(b->getFixture()->GetShape(), 0);
		i.proxyA = pA;
		i.proxyB = pB;
		i.transformA = a->getFixture()->GetBody()->GetTransform();
		i.transformB = b->getFixture()->GetBody()->GetTransform();
		i.useRadii = true;
		b2Distance(&o, &c, &i);
	});
	if (err) return mrb_nil_value();

	mrb_value h = mrb_hash_new(mrb);
	mrb_hash_set(mrb, h, mrb_symbol_value(mrb_intern_lit(mrb, "distance")), mrbx_number(mrb, Physics::scaleUp(o.distance)));
	mrb_hash_set(mrb, h, mrb_symbol_value(mrb_intern_lit(mrb, "x1")), mrbx_number(mrb, Physics::scaleUp(o.pointA.x)));
	mrb_hash_set(mrb, h, mrb_symbol_value(mrb_intern_lit(mrb, "y1")), mrbx_number(mrb, Physics::scaleUp(o.pointA.y)));
	mrb_hash_set(mrb, h, mrb_symbol_value(mrb_intern_lit(mrb, "x2")), mrbx_number(mrb, Physics::scaleUp(o.pointB.x)));
	mrb_hash_set(mrb, h, mrb_symbol_value(mrb_intern_lit(mrb, "y2")), mrbx_number(mrb, Physics::scaleUp(o.pointB.y)));
	return h;
}

static const MrbReg functions[] =
{
	{ "set_meter",            w_setMeter,           MRB_ARGS_KEY(1, 0) },
	{ "get_meter",            w_getMeter,           MRB_ARGS_NONE() },
	{ "get_distance",         w_getDistance,        MRB_ARGS_KEY(2, 0) },
	{ "new_world",            w_newWorld,           MRB_ARGS_KEY(3, 0) },
	{ "new_body",             w_newBody,            MRB_ARGS_KEY(4, 0) },
	{ "new_circle_shape",     w_newCircleShape,     MRB_ARGS_KEY(4, 0) },
	{ "new_rectangle_shape",  w_newRectangleShape,  MRB_ARGS_KEY(6, 0) },
	{ "new_polygon_shape",    w_newPolygonShape,    MRB_ARGS_KEY(2, 0) },
	{ "new_edge_shape",       w_newEdgeShape,       MRB_ARGS_KEY(5, 0) },
	{ "new_chain_shape",      w_newChainShape,      MRB_ARGS_KEY(3, 0) },
	{ "new_circle_body",      w_newCircleBody,      MRB_ARGS_KEY(5, 0) },
	{ "new_rectangle_body",   w_newRectangleBody,   MRB_ARGS_KEY(7, 0) },
	{ "new_polygon_body",     w_newPolygonBody,     MRB_ARGS_KEY(3, 0) },
	{ "new_edge_body",        w_newEdgeBody,        MRB_ARGS_KEY(6, 0) },
	{ "new_chain_body",       w_newChainBody,       MRB_ARGS_KEY(4, 0) },
	{ nullptr, nullptr, 0 }
};

// Equivalent of luaopen_love_physics: creates the module, registers
// Love::Physics and the World / Body / Shape object types.
extern "C" void mrb_love_physics_init(mrb_state *mrb)
{
	Physics *inst = instance();
	if (inst == nullptr)
		inst = new Physics();
	else
		inst->retain();

	WrappedModule w;
	w.module = inst;
	w.name = "Physics";
	w.type = &Module::type;
	w.functions = functions;

	mrbx_register_module(mrb, w);

	mrbx_register_type(mrb, World::type, world_functions);
	mrbx_register_type(mrb, Body::type, body_functions);
	// Register the base Shape before the concrete types so the concrete Ruby
	// classes inherit its methods through the love::Type-mirrored hierarchy.
	mrbx_register_type(mrb, Shape::type, shape_functions);
	mrbx_register_type(mrb, CircleShape::type, circle_functions);
	mrbx_register_type(mrb, PolygonShape::type, polygon_functions);
	mrbx_register_type(mrb, EdgeShape::type, edge_functions);
	mrbx_register_type(mrb, ChainShape::type, chain_functions);
}

} // box2d
} // physics
} // love
