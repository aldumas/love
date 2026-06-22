# Exercises the graphics render-state slice on the real backend:
#   set/get blend_mode, scissor (set/get/intersect), color_mask,
#   line_width/style/join, point_size, wireframe.
#
#   ./love_mrb_harness state_test.rb

unless Love.const_defined?(:Window)
  puts "(no display available -- Love::Window not registered; skipping)"
  exit
end

Love::Window.set_mode(width: 640, height: 480, centered: true)
Love::Window.set_title(title: "graphics render state")

g = Love::Graphics
unless Love.const_defined?(:Graphics) && g.active?
  puts "(graphics not active -- needs a window)"
  exit
end

puts "=== blend mode ==="
g.set_blend_mode(mode: "add")
puts "  set add -> #{g.get_blend_mode.inspect}"
g.set_blend_mode(mode: "alpha", alpha_mode: "premultiplied")
puts "  set alpha/premultiplied -> #{g.get_blend_mode.inspect}"
g.set_blend_mode(mode: "alpha")   # back to default alphamultiply

puts
puts "=== scissor ==="
puts "  initial get_scissor -> #{g.get_scissor.inspect}"   # expect nil
g.set_scissor(x: 10, y: 20, width: 100, height: 50)
puts "  after set    -> #{g.get_scissor.inspect}"
g.intersect_scissor(x: 50, y: 30, width: 200, height: 200)
puts "  after isect  -> #{g.get_scissor.inspect}"           # expect {x:50,y:30,w:60,h:40}
g.set_scissor
puts "  after clear  -> #{g.get_scissor.inspect}"           # expect nil

puts
puts "=== color mask ==="
g.set_color_mask(r: false, a: false)
puts "  set r/a off  -> #{g.get_color_mask.inspect}"        # expect r:false,g:true,b:true,a:false
g.set_color_mask
puts "  reset all    -> #{g.get_color_mask.inspect}"        # expect all true

puts
puts "=== line / point ==="
g.set_line_width(width: 4.5)
g.set_line_style(style: "rough")
g.set_line_join(join: "bevel")
g.set_point_size(size: 3.0)
puts "  line_width=#{g.get_line_width} style=#{g.get_line_style.inspect} join=#{g.get_line_join.inspect} point_size=#{g.get_point_size}"

puts
puts "=== wireframe ==="
puts "  initial wireframe? -> #{g.wireframe?}"
g.set_wireframe(enable: true)
puts "  after enable       -> #{g.wireframe?}"
g.set_wireframe(enable: false)

puts
puts "=== drawing with state (scissor + line width) ==="
g.set_background_color(r: 0.10, g: 0.11, b: 0.15)
g.set_line_width(width: 6.0)
frames = 60
frames.times do |i|
  Love::Event.pump
  g.origin
  g.clear

  # Clip drawing to a moving window, then stroke a big rectangle through it.
  g.set_scissor(x: 80 + i * 3, y: 120, width: 240, height: 240)
  g.set_color(r: 0.3, g: 0.9, b: 0.5)
  g.rectangle(mode: "line", x: 100, y: 100, width: 440, height: 300)
  g.set_scissor

  g.present
  Love::Timer.sleep(seconds: 0.016)
end

puts "drew #{frames} frames (scissor-clipped line rectangle)"
