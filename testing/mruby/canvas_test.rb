# Exercises the graphics canvas / render-target slice on the real backend:
#   Love::Graphics.new_canvas / set_canvas / get_canvas, including layered
#   (array) and mipmapped render targets with the slice:/mipmap: set_canvas
#   variants. Renders a scene into
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

puts
puts "=== array render target + per-layer set_canvas ==="
acv = g.new_canvas(width: 128, height: 128, type: "array", layers: 3)
puts "  type -> #{acv.get_texture_type.inspect}, layer_count -> #{acv.get_layer_count}"
# Render a distinct solid colour into each layer (slice is 1-based).
[[1.0, 0.3, 0.3], [0.3, 1.0, 0.3], [0.3, 0.4, 1.0]].each_with_index do |c, i|
  g.set_canvas(canvas: acv, slice: i + 1)
  g.clear(r: c[0], g: c[1], b: c[2], a: 1.0)
end
# get_canvas reports the slice/mipmap for a layered target (Hash form).
g.set_canvas(canvas: acv, slice: 2)
gc = g.get_canvas
puts "  get_canvas (layer 2) -> #{gc.class}, slice #{gc[:slice]}, mipmap #{gc[:mipmap]}"
g.set_canvas
puts "  after reset -> #{g.get_canvas.inspect}"

puts
puts "=== mipmapped render target + per-mip set_canvas ==="
mcv = g.new_canvas(width: 128, height: 128, mipmaps: true)
puts "  mipmap_count -> #{mcv.get_mipmap_count}"
g.set_canvas(canvas: mcv, mipmap: 2)
g.clear(r: 0.9, g: 0.5, b: 0.1, a: 1.0)
g.set_canvas
puts "  rendered into mip level 2 OK"

puts
puts "=== draw the rendered array layers to the screen ==="
sb = g.new_sprite_batch(texture: acv, size: 8)
sb.add_layer(layer: 1, x: 80,  y: 180)
sb.add_layer(layer: 2, x: 240, y: 180)
sb.add_layer(layer: 3, x: 400, y: 180)
30.times do
  Love::Event.pump
  g.origin
  g.clear
  g.set_color(r: 1.0, g: 1.0, b: 1.0)
  g.draw(drawable: sb)
  g.present
  Love::Timer.sleep(seconds: 0.016)
end
puts "drew the three rendered array layers"
