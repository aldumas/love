# Exercises the graphics coordinate-system transform stack on the real backend:
#   push / pop / translate / rotate / scale / shear,
#   apply_transform / replace_transform (a Love::Transform),
#   transform_point / inverse_transform_point.
#
#   ./love_mrb_harness transform_test.rb

unless Love.const_defined?(:Window)
  puts "(no display available -- Love::Window not registered; skipping)"
  exit
end

Love::Window.set_mode(width: 640, height: 480, centered: true)
Love::Window.set_title(title: "graphics transforms")

g = Love::Graphics
unless Love.const_defined?(:Graphics) && g.active?
  puts "(graphics not active -- needs a window)"
  exit
end

puts "=== transform_point round-trip ==="
g.origin
g.translate(x: 100, y: 50)
g.scale(x: 2.0)            # y defaults to x
p1 = g.transform_point(x: 10, y: 10)
puts "  translate(100,50) + scale(2): (10,10) -> #{p1.inspect}"   # expect {x:120, y:70}
back = g.inverse_transform_point(x: p1[:x], y: p1[:y])
puts "  inverse -> #{back.inspect}"                               # expect ~{x:10, y:10}

puts
puts "=== push/pop isolation ==="
g.origin
g.push                     # type defaults to "transform"
g.translate(x: 200, y: 0)
inside = g.transform_point(x: 0, y: 0)
g.pop
outside = g.transform_point(x: 0, y: 0)
puts "  inside push  -> #{inside.inspect}"   # expect {x:200, y:0}
puts "  after pop    -> #{outside.inspect}"  # expect {x:0, y:0}

puts
puts "=== apply_transform / replace_transform ==="
t = Love::Math.new_transform
t.translate(x: 5, y: 7)
g.origin
g.apply_transform(transform: t)
puts "  applied a Love::Transform (#{t.class})"
g.replace_transform(transform: t)
puts "  replaced with a Love::Transform"

puts
puts "=== drawing with nested transforms ==="
g.set_background_color(r: 0.10, g: 0.11, b: 0.15)
frames = 60
frames.times do |i|
  Love::Event.pump
  g.origin
  g.clear

  g.push
  g.translate(x: 320, y: 240)
  g.rotate(angle: i * 0.05)
  g.scale(x: 1.0 + 0.3 * Math.sin(i * 0.1))
  g.shear(kx: 0.1, ky: 0.0)
  g.set_color(r: 1.0, g: 0.8, b: 0.2)
  g.rectangle(mode: "fill", x: -40, y: -40, width: 80, height: 80)
  g.pop

  g.present
  Love::Timer.sleep(seconds: 0.016)
end

puts "drew #{frames} frames (nested push/translate/rotate/scale/shear)"
