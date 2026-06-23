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
