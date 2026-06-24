# Exercises the graphics stencil/depth render state on the real backend:
#   Love::Graphics.set_stencil_mode / get_stencil_mode, the low-level
#   set_stencil_state / get_stencil_state, and set_depth_mode / get_depth_mode.
#   Uses a canvas with a temporary stencil buffer (set_canvas(stencil: true)) to
#   mask drawing.
#
#   ./love_mrb_harness stencil_test.rb

unless Love.const_defined?(:Window)
  puts "(no display available -- Love::Window not registered; skipping)"
  exit
end

Love::Window.set_mode(width: 640, height: 480, centered: true, stencil: true, depth: true)
Love::Window.set_title(title: "graphics stencil/depth")

g = Love::Graphics
unless Love.const_defined?(:Graphics) && g.active?
  puts "(graphics not active -- needs a window)"
  exit
end

puts "=== stencil mode state ==="
puts "  initial   -> #{g.get_stencil_mode.inspect}"   # expect {mode:"off", value:0}
g.set_stencil_mode(mode: "draw", value: 1)
puts "  set draw  -> #{g.get_stencil_mode.inspect}"
g.set_stencil_mode(mode: "test", value: 1)
puts "  set test  -> #{g.get_stencil_mode.inspect}"
g.set_stencil_mode
puts "  reset     -> #{g.get_stencil_mode.inspect}"   # expect {mode:"off", value:0}

puts
puts "=== low-level stencil state ==="
puts "  initial -> #{g.get_stencil_state.inspect}"
g.set_stencil_state(action: "replace", compare: "always", value: 1)
puts "  set replace/always/1 -> #{g.get_stencil_state.inspect}"
g.set_stencil_state(action: "keep", compare: "equal", value: 1, read_mask: 0xFF, write_mask: 0x00)
puts "  set keep/equal masks -> #{g.get_stencil_state.inspect}"
g.set_stencil_state
puts "  reset -> #{g.get_stencil_state.inspect}"   # expect keep/always, masks all-ones

puts
puts "=== depth mode state ==="
puts "  initial      -> #{g.get_depth_mode.inspect}"
g.set_depth_mode(compare: "lequal", write: true)
puts "  set lequal/w -> #{g.get_depth_mode.inspect}"
g.set_depth_mode
puts "  reset        -> #{g.get_depth_mode.inspect}"

puts
puts "=== stencil-masked drawing into a canvas ==="
cv = g.new_canvas(width: 320, height: 320)

g.set_canvas(canvas: cv, stencil: true)
g.clear(r: 0.1, g: 0.1, b: 0.15, a: 1.0)

# 1) Write a circular-ish mask into the stencil buffer (a square here).
g.set_stencil_mode(mode: "draw", value: 1)
g.set_color(r: 1.0, g: 1.0, b: 1.0)
g.rectangle(mode: "fill", x: 80, y: 80, width: 160, height: 160)

# 2) Only draw where the stencil == 1.
g.set_stencil_mode(mode: "test", value: 1)
g.set_color(r: 1.0, g: 0.6, b: 0.1)
g.rectangle(mode: "fill", x: 0, y: 0, width: 320, height: 320)   # clipped to the mask

g.set_stencil_mode  # off
g.set_canvas
puts "  rendered a stencil-masked fill into the canvas"

puts
puts "=== composite to the screen ==="
g.set_background_color(r: 0.08, g: 0.09, b: 0.12)
frames = 60
frames.times do |i|
  Love::Event.pump
  g.origin
  g.clear
  g.set_color(r: 1.0, g: 1.0, b: 1.0)
  g.draw(drawable: cv, x: 320, y: 240, ox: 160, oy: 160, r: i * 0.02)
  g.present
  Love::Timer.sleep(seconds: 0.016)
end

puts "drew #{frames} frames (stencil-masked canvas content composited to screen)"
