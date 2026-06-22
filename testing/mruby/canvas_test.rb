# Exercises the graphics canvas / render-target slice on the real backend:
#   Love::Graphics.new_canvas / set_canvas / get_canvas. Renders a scene into
#   an off-screen canvas, then draws that canvas (a Texture) to the screen.
#
#   ./love_mrb_harness canvas_test.rb

unless Love.const_defined?(:Window)
  puts "(no display available -- Love::Window not registered; skipping)"
  exit
end

Love::Window.set_mode(width: 640, height: 480, centered: true)
Love::Window.set_title(title: "graphics canvas")

g = Love::Graphics
unless Love.const_defined?(:Graphics) && g.active?
  puts "(graphics not active -- needs a window)"
  exit
end

puts "=== new_canvas ==="
cv = g.new_canvas(width: 256, height: 256)
puts "  canvas -> #{cv.class}"
puts "  get_dimensions -> #{cv.get_dimensions.inspect}"
puts "  is_a?(Love::Texture) -> #{cv.is_a?(Love::Texture)}" if Love.const_defined?(:Texture)

puts
puts "=== set_canvas / get_canvas ==="
puts "  get_canvas (before) -> #{g.get_canvas.inspect}"
g.set_canvas(canvas: cv)
puts "  get_canvas (active) -> #{g.get_canvas.class}"
g.set_canvas
puts "  get_canvas (reset)  -> #{g.get_canvas.inspect}"

puts
puts "=== render into the canvas once ==="
g.set_canvas(canvas: cv)
g.clear(r: 0.1, g: 0.0, b: 0.2, a: 1.0)
g.set_color(r: 1.0, g: 0.7, b: 0.1)
g.rectangle(mode: "fill", x: 40, y: 40, width: 176, height: 176)
g.set_color(r: 0.2, g: 0.9, b: 1.0)
g.rectangle(mode: "fill", x: 90, y: 90, width: 76, height: 76)
g.set_canvas
puts "  rendered a scene into the canvas"

puts
puts "=== draw the canvas to the screen ==="
g.set_background_color(r: 0.08, g: 0.09, b: 0.12)
frames = 60
frames.times do |i|
  Love::Event.pump
  g.origin
  g.clear
  g.set_color(r: 1.0, g: 1.0, b: 1.0)
  # Draw the canvas spinning about its center.
  g.draw(drawable: cv, x: 320, y: 240, ox: 128, oy: 128, r: i * 0.03)
  g.present
  Love::Timer.sleep(seconds: 0.016)
end

puts "drew #{frames} frames (off-screen canvas composited to the screen)"
