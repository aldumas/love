# Exercises the graphics object types on the real renderer backend:
#   Love::Graphics.new_image / draw / new_quad, plus the Love::Texture and
#   Love::Quad object types.
#
#   ./love_mrb_harness graphics_test.rb
#
# Builds an ImageData checkerboard, uploads it as a Texture, and draws it (with
# transforms and via a Quad sub-region) through the real batched renderer.

unless Love.const_defined?(:Window)
  puts "(no display available -- Love::Window not registered; skipping)"
  exit
end

Love::Window.set_mode(width: 640, height: 480, centered: true)
Love::Window.set_title(title: "graphics object types")

g = Love::Graphics
unless Love.const_defined?(:Graphics) && g.active?
  puts "(graphics not active -- needs a window)"
  exit
end

puts "=== object types ==="

# A 64x64 checkerboard ImageData (orange / blue squares), then upload it.
idata = Love::Image.new_image_data(width: 64, height: 64, format: "rgba8")
idata.map_pixel(width: 64, height: 64) do |x, y, _r, _gg, _b, _a|
  ((x / 8) + (y / 8)).even? ? [0.95, 0.55, 0.2, 1.0] : [0.3, 0.55, 0.9, 1.0]
end

img = g.new_image(file: idata)
puts "new_image -> #{img.class}"
puts "  get_dimensions       -> #{img.get_dimensions.inspect}"
puts "  get_pixel_dimensions -> #{img.get_pixel_dimensions.inspect}"
puts "  get_dpi_scale        -> #{img.get_dpi_scale}"
puts "  is_compressed        -> #{img.is_compressed}"
puts "  is_a?(Love::Drawable) -> #{img.is_a?(Love::Drawable)}" if Love.const_defined?(:Drawable)

img.set_filter(min: "nearest", mag: "nearest")
puts "  filter after set     -> #{img.get_filter.inspect}"

quad = g.new_quad(x: 0, y: 0, width: 32, height: 32, texture: img)
puts "new_quad -> #{quad.class}"
puts "  get_viewport         -> #{quad.get_viewport.inspect}"

puts
puts "=== drawing ==="
g.set_background_color(r: 0.10, g: 0.11, b: 0.15)

frames = 60
frames.times do |i|
  Love::Event.pump
  g.origin
  g.clear

  g.set_color(r: 1.0, g: 1.0, b: 1.0)
  # Whole image, scaled 2x, sliding right.
  g.draw(drawable: img, x: 60 + i * 2, y: 80, sx: 2.0, sy: 2.0)
  # Rotating about its center, drawn from a 32x32 quad sub-region.
  g.draw(drawable: img, quad: quad, x: 440, y: 240, ox: 16, oy: 16, r: i * 0.08, sx: 3.0, sy: 3.0)

  g.present
  Love::Timer.sleep(seconds: 0.016)
end

puts "drew #{frames} frames (new_image + draw whole + draw quad with transforms)"
