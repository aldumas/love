# Exercises the first physics slice (love.physics / box2d):
#   - Love::Physics.new_world / new_body / shape creators + combos
#   - World stepping, gravity, body/joint/contact counts, get_bodies
#   - Body transforms / velocity / forces / mass / type / flags
#   - Shape friction/restitution/density/sensor, filter category/mask/group,
#     and the concrete CircleShape / PolygonShape / EdgeShape / ChainShape API
#
#   ./love_mrb_harness physics_test.rb
#
# Headless: physics needs no window/graphics.

unless Love.const_defined?(:Physics)
  puts "(Love::Physics not registered)"
  exit 1
end

P = Love::Physics
fail_count = 0

def check(label, got, want, eps = 1e-3)
  ok = (got - want).abs <= eps
  puts "  #{ok ? 'ok ' : 'FAIL'}  #{label}: #{got} (want #{want})"
  ok
end

def assert(label, cond)
  puts "  #{cond ? 'ok ' : 'FAIL'}  #{label}"
  cond
end

puts "=== meter ==="
P.set_meter(scale: 64)
fail_count += 1 unless check("get_meter", P.get_meter, 64)
P.set_meter(scale: 30) # default

puts
puts "=== world + gravity ==="
world = P.new_world(gx: 0, gy: 9.81)
g = world.get_gravity
fail_count += 1 unless check("gravity x", g[:x], 0)
fail_count += 1 unless check("gravity y", g[:y], 9.81)
fail_count += 1 unless assert("not locked", !world.locked?)
fail_count += 1 unless assert("not destroyed", !world.destroyed?)
fail_count += 1 unless check("body count 0", world.get_body_count, 0)

puts
puts "=== falling body ==="
body = P.new_body(world: world, x: 0, y: 0, type: "dynamic")
fail_count += 1 unless assert("type dynamic", body.get_type == "dynamic")
shape = P.new_circle_shape(body: body, radius: 5)
shape.set_density(density: 1.0)
body.reset_mass_data
fail_count += 1 unless assert("has mass", body.get_mass > 0)
fail_count += 1 unless check("body count 1", world.get_body_count, 1)

y0 = body.get_y
30.times { world.update(dt: 1.0 / 60.0) }
y1 = body.get_y
fail_count += 1 unless assert("body fell under gravity (#{y0} -> #{y1})", y1 > y0 + 1.0)
fail_count += 1 unless assert("downward velocity", body.get_linear_velocity[:y] > 0)

puts
puts "=== get_bodies / get_shapes ==="
bodies = world.get_bodies
fail_count += 1 unless assert("get_bodies returns 1", bodies.length == 1)
fail_count += 1 unless assert("same body identity", bodies[0].get_y == body.get_y)
shapes = body.get_shapes
fail_count += 1 unless assert("get_shapes returns 1", shapes.length == 1)
fail_count += 1 unless assert("shape type circle", shapes[0].get_type == "circle")

puts
puts "=== forces / velocity ==="
b2 = P.new_body(world: world, x: 100, y: 100, type: "dynamic")
P.new_circle_shape(body: b2, radius: 5).set_density(density: 1.0)
b2.reset_mass_data
b2.set_gravity_scale(scale: 0) # isolate the impulse from gravity
b2.apply_linear_impulse(x: 10, y: 0)
world.update(dt: 1.0 / 60.0)
fail_count += 1 unless assert("impulse produced +x velocity", b2.get_linear_velocity[:x] > 0)
b2.set_linear_velocity(x: 0, y: 0)
fail_count += 1 unless check("velocity reset", b2.get_linear_velocity[:x], 0)

puts
puts "=== body flags ==="
b2.set_bullet(bullet: true)
fail_count += 1 unless assert("bullet?", b2.bullet?)
b2.set_fixed_rotation(fixed: true)
fail_count += 1 unless assert("fixed_rotation?", b2.fixed_rotation?)
b2.set_awake(awake: false)
fail_count += 1 unless assert("not awake", !b2.awake?)

puts
puts "=== shape properties ==="
shape.set_friction(friction: 0.7)
fail_count += 1 unless check("friction", shape.get_friction, 0.7)
shape.set_restitution(restitution: 0.4)
fail_count += 1 unless check("restitution", shape.get_restitution, 0.4)
shape.set_sensor(sensor: true)
fail_count += 1 unless assert("sensor?", shape.sensor?)
shape.set_sensor(sensor: false)
shape.set_category(categories: [2, 5])
fail_count += 1 unless assert("get_category", shape.get_category == [2, 5])
shape.set_group_index(index: -3)
fail_count += 1 unless check("group index", shape.get_group_index, -3)

puts
puts "=== rectangle / polygon / edge / chain ==="
rbody, rshape = P.new_rectangle_body(world: world, x: 0, y: 0, width: 10, height: 20, type: "static")
fail_count += 1 unless assert("rect shape is polygon", rshape.get_type == "polygon")
fail_count += 1 unless assert("polygon validate", rshape.validate)

estatic = P.new_body(world: world, type: "static")
eshape = P.new_edge_shape(body: estatic, x1: 0, y1: 0, x2: 10, y2: 0)
fail_count += 1 unless assert("edge type", eshape.get_type == "edge")

cstatic = P.new_body(world: world, type: "static")
cshape = P.new_chain_shape(body: cstatic, loop: false, points: [0, 0, 10, 0, 20, 5])
fail_count += 1 unless assert("chain type", cshape.get_type == "chain")
fail_count += 1 unless check("chain vertex count", cshape.get_vertex_count, 3)
pt = cshape.get_point(index: 2)
fail_count += 1 unless check("chain point 2 x", pt[:x], 10)

puts
puts "=== shape geometry queries ==="
# polygon vertex readback: a 10x20 rectangle -> 4 points (8 numbers)
ppts = rshape.get_points
fail_count += 1 unless assert("polygon get_points -> 8 numbers", ppts.length == 8)
# edge endpoints: (0,0)-(10,0)
ep = eshape.get_points
fail_count += 1 unless assert("edge get_points -> 4 numbers", ep.length == 4)
fail_count += 1 unless check("edge p1 x", ep[0], 0)
fail_count += 1 unless check("edge p2 x", ep[2], 10)

# compute_mass on the circle (radius 5, density 1) -> positive mass
cm = shape.compute_mass(density: 1.0)
fail_count += 1 unless assert("compute_mass mass > 0", cm[:mass] > 0)

# compute_aabb of the circle at the identity transform -> a non-empty box
ab = shape.compute_aabb(x: 0, y: 0, r: 0)
fail_count += 1 unless assert("compute_aabb non-empty", ab[:bottom_right_x] > ab[:top_left_x])

# get_mass_data / get_bounding_box read the live fixture
md = shape.get_mass_data
fail_count += 1 unless assert("get_mass_data mass > 0", md[:mass] > 0)
bb = shape.get_bounding_box
fail_count += 1 unless assert("get_bounding_box non-empty", bb[:bottom_right_x] > bb[:top_left_x])

# ray_cast against the circle shape at a transform (independent of body pos):
# a ray straight through the centre hits with a fraction in (0,1)...
hit = shape.ray_cast(x1: -100, y1: 0, x2: 100, y2: 0, max_fraction: 1.0, x: 0, y: 0, r: 0)
fail_count += 1 unless assert("ray_cast hits", !hit.nil?)
frac_ok = hit && hit[:fraction] > 0 && hit[:fraction] < 1
fail_count += 1 unless assert("ray_cast fraction in (0,1)", frac_ok)
# ...and a ray well clear of it returns nil.
miss = shape.ray_cast(x1: -100, y1: 1000, x2: 100, y2: 1000, max_fraction: 1.0, x: 0, y: 0, r: 0)
fail_count += 1 unless assert("ray_cast miss -> nil", miss.nil?)

puts
puts "=== get_distance ==="
# both shapes are active in the world -> a Hash with distance + nearest points.
d = P.get_distance(shape_a: shape, shape_b: rshape)
fail_count += 1 unless assert("get_distance >= 0", d[:distance] >= 0)
fail_count += 1 unless assert("get_distance has x1/y1/x2/y2",
  d.key?(:x1) && d.key?(:y1) && d.key?(:x2) && d.key?(:y2))

puts
puts "=== world spatial queries ==="
# A fresh world with one static circle (radius 10) at (50, 50) makes the
# AABB query and ray casts deterministic.
qworld = P.new_world(gx: 0, gy: 0)
qbody = P.new_body(world: qworld, x: 50, y: 50, type: "static")
qshape = P.new_circle_shape(body: qbody, radius: 10)

# get_shapes_in_area: a box around the circle finds it; a far box finds nothing.
inside = qworld.get_shapes_in_area(x1: 0, y1: 0, x2: 100, y2: 100)
fail_count += 1 unless assert("get_shapes_in_area finds 1", inside.length == 1)
fail_count += 1 unless assert("found shape is the circle", inside[0].get_type == "circle")
outside = qworld.get_shapes_in_area(x1: 200, y1: 200, x2: 300, y2: 300)
fail_count += 1 unless assert("get_shapes_in_area far box is empty", outside.length == 0)

# ray_cast_closest: a vertical ray through the centre hits the circle...
rc = qworld.ray_cast_closest(x1: 50, y1: -100, x2: 50, y2: 100)
fail_count += 1 unless assert("ray_cast_closest hits", !rc.nil?)
fail_count += 1 unless assert("hit shape is the circle", rc && rc[:shape].get_type == "circle")
fail_count += 1 unless assert("hit fraction in (0,1)", rc && rc[:fraction] > 0 && rc[:fraction] < 1)
# ray_cast_any also reports a hit; a ray well clear of the circle misses.
fail_count += 1 unless assert("ray_cast_any hits", !qworld.ray_cast_any(x1: 50, y1: -100, x2: 50, y2: 100).nil?)
fail_count += 1 unless assert("ray_cast_closest miss -> nil", qworld.ray_cast_closest(x1: 200, y1: -100, x2: 200, y2: 100).nil?)
qworld.destroy

puts
puts "=== joints ==="
jworld = P.new_world(gx: 0, gy: 0)
ja = P.new_body(world: jworld, x: 0, y: 0, type: "dynamic")
jb = P.new_body(world: jworld, x: 100, y: 0, type: "dynamic")
P.new_circle_shape(body: ja, radius: 5)
P.new_circle_shape(body: jb, radius: 5)

# DistanceJoint: factory, base methods, and type-specific setters/getters.
dj = P.new_distance_joint(body1: ja, body2: jb, x1: 0, y1: 0, x2: 100, y2: 0)
fail_count += 1 unless assert("distance joint type", dj.get_type == "distance")
fail_count += 1 unless assert("joint valid?", dj.valid?)
fail_count += 1 unless assert("joint not destroyed?", !dj.destroyed?)
# Body identity is not preserved across wrappers yet (deferred #phys-userdata),
# so compare the bodies by position rather than object equality.
fail_count += 1 unless assert("joint body_a at ja", dj.get_body_a.get_x.abs < 1e-3)
fail_count += 1 unless assert("joint body_b at jb", (dj.get_body_b.get_x - 100).abs < 1e-3)
fail_count += 1 unless assert("collide_connected? default false", !dj.collide_connected?)
anchors = dj.get_anchors
fail_count += 1 unless assert("get_anchors x1", (anchors[:x1] - 0).abs < 1e-3)
fail_count += 1 unless assert("get_anchors x2 ~ 100", (anchors[:x2] - 100).abs < 1e-3)
rf = dj.get_reaction_force(dt: 1.0 / 60)
fail_count += 1 unless assert("get_reaction_force is a hash", rf.key?(:x) && rf.key?(:y))
fail_count += 1 unless assert("get_reaction_torque numeric", dj.get_reaction_torque(dt: 1.0 / 60).is_a?(Numeric))
dj.set_length(length: 150)
fail_count += 1 unless assert("distance set/get length", (dj.get_length - 150).abs < 1e-3)
dj.set_damping(damping: 0.5)
fail_count += 1 unless assert("distance set/get damping", (dj.get_damping - 0.5).abs < 1e-3)

# get_joints from both the world and a body sees the joint we just made.
fail_count += 1 unless assert("world get_joints count", jworld.get_joints.length == 1)
fail_count += 1 unless assert("world get_joint_count", jworld.get_joint_count == 1)
fail_count += 1 unless assert("body get_joints count", ja.get_joints.length == 1)
fail_count += 1 unless assert("body get_joints type", ja.get_joints[0].get_type == "distance")

# RevoluteJoint: motor + limits (limits are in degrees, unscaled).
rj = P.new_revolute_joint(body1: ja, body2: jb, x1: 50, y1: 0)
fail_count += 1 unless assert("revolute joint type", rj.get_type == "revolute")
rj.set_motor_enabled(enable: true)
fail_count += 1 unless assert("revolute motor_enabled?", rj.motor_enabled?)
rj.set_motor_speed(speed: 2.0)
fail_count += 1 unless assert("revolute motor speed", (rj.get_motor_speed - 2.0).abs < 1e-3)
rj.set_limits_enabled(enable: true)
rj.set_limits(lower: -1.0, upper: 1.0)
lim = rj.get_limits
fail_count += 1 unless assert("revolute limits lower", (lim[:lower] - -1.0).abs < 1e-3)
fail_count += 1 unless assert("revolute limits upper", (lim[:upper] - 1.0).abs < 1e-3)

# PrismaticJoint: axis readback as a hash, motor force.
pj = P.new_prismatic_joint(body1: ja, body2: jb, x1: 0, y1: 0, ax: 1, ay: 0)
fail_count += 1 unless assert("prismatic joint type", pj.get_type == "prismatic")
ax = pj.get_axis
fail_count += 1 unless assert("prismatic axis x ~ 1", (ax[:x] - 1.0).abs < 1e-3)
fail_count += 1 unless assert("prismatic axis y ~ 0", ax[:y].abs < 1e-3)

# MouseJoint: target round-trips through scaling.
mj = P.new_mouse_joint(body: ja, x: 10, y: 20)
fail_count += 1 unless assert("mouse joint type", mj.get_type == "mouse")
mj.set_target(x: 30, y: 40)
tgt = mj.get_target
fail_count += 1 unless assert("mouse target x", (tgt[:x] - 30).abs < 1e-2)
fail_count += 1 unless assert("mouse target y", (tgt[:y] - 40).abs < 1e-2)

# WeldJoint / RopeJoint / MotorJoint smoke tests.
wj = P.new_weld_joint(body1: ja, body2: jb, x1: 50, y1: 0)
fail_count += 1 unless assert("weld joint type", wj.get_type == "weld")
roj = P.new_rope_joint(body1: ja, body2: jb, x1: 0, y1: 0, x2: 100, y2: 0, max_length: 200)
# RopeJoint is implemented on top of a b2DistanceJoint, so get_type reports
# "distance" (same quirk as the Lua build); its rope methods still work.
fail_count += 1 unless assert("rope joint type", roj.get_type == "distance")
fail_count += 1 unless assert("rope max length", (roj.get_max_length - 200).abs < 1e-3)
moj = P.new_motor_joint(body1: ja, body2: jb)
fail_count += 1 unless assert("motor joint type", moj.get_type == "motor")
moj.set_linear_offset(x: 5, y: 7)
loff = moj.get_linear_offset
fail_count += 1 unless assert("motor linear offset x", (loff[:x] - 5).abs < 1e-2)

# GearJoint binds two joints; Box2D requires each to be anchored to a fixed
# body (body1) and the moving bodies to differ, so build dedicated joints
# between a static ground and each dynamic body.
ground = P.new_body(world: jworld, type: "static")
grj = P.new_revolute_joint(body1: ground, body2: ja, x1: 0, y1: 0)
gpj = P.new_prismatic_joint(body1: ground, body2: jb, x1: 100, y1: 0, ax: 1, ay: 0)
gj = P.new_gear_joint(joint1: grj, joint2: gpj, ratio: 2.0)
fail_count += 1 unless assert("gear joint type", gj.get_type == "gear")
fail_count += 1 unless assert("gear ratio", (gj.get_ratio - 2.0).abs < 1e-3)
fail_count += 1 unless assert("gear joint_a is revolute", gj.get_joint_a.get_type == "revolute")

# Destroying a joint flips destroyed? and drops it from the world list.
dj.destroy
fail_count += 1 unless assert("joint destroyed?", dj.destroyed?)
jworld.destroy

puts
puts "=== destroy ==="
body.destroy
fail_count += 1 unless assert("body destroyed?", body.destroyed?)
world.destroy
fail_count += 1 unless assert("world destroyed?", world.destroyed?)

puts
if fail_count == 0
  puts "ALL PHYSICS TESTS PASSED"
else
  puts "#{fail_count} PHYSICS TEST(S) FAILED"
  exit 1
end
