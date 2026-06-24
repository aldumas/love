# Exercises the graphics texture-creation family on the real backend:
#   Love::Graphics.new_image / new_array_image / new_volume_image /
#   new_cube_image and the Texture query getters (get_texture_type / get_depth /
#   get_layer_count / get_mipmap_count).
#
#   ./love_mrb_harness texture_test.rb

unless Love.const_defined?(:Window)
  puts "(no display available -- Love::Window not registered; skipping)"
  exit
end

Love::Window.set_mode(width: 640, height: 480, centered: true)
Love::Window.set_title(title: "graphics textures")

g = Love::Graphics
unless Love.const_defined?(:Graphics) && g.active?
  puts "(graphics not active -- needs a window)"
  exit
end

# A solid-colour square ImageData (square so it is valid for any texture type).
solid = lambda do |r, gg, b, size = 16|
  d = Love::Image.new_image_data(width: size, height: size, format: "rgba8")
  d.map_pixel(width: size, height: size) { |_x, _y, _r, _gg, _b, _a| [r, gg, b, 1.0] }
  d
end

puts "=== new_image (2d) ==="
img = g.new_image(file: solid.call(1.0, 1.0, 1.0))
puts "  texture_type  -> #{img.get_texture_type.inspect}"
puts "  dimensions    -> #{img.get_dimensions.inspect}"
puts "  layer_count   -> #{img.get_layer_count}"
puts "  depth         -> #{img.get_depth}"
puts "  mipmap_count  -> #{img.get_mipmap_count}"

puts
puts "=== new_array_image ==="
arr = g.new_array_image(layers: [solid.call(1.0, 0.2, 0.2), solid.call(0.2, 1.0, 0.2), solid.call(0.2, 0.4, 1.0)])
puts "  texture_type -> #{arr.get_texture_type.inspect}"
puts "  layer_count  -> #{arr.get_layer_count}"
puts "  dimensions   -> #{arr.get_dimensions.inspect}"

puts
puts "=== new_volume_image ==="
vol = g.new_volume_image(layers: [solid.call(1, 0, 0), solid.call(0, 1, 0), solid.call(0, 0, 1), solid.call(1, 1, 0)])
puts "  texture_type -> #{vol.get_texture_type.inspect}"
puts "  depth        -> #{vol.get_depth}"

puts
puts "=== new_cube_image ==="
faces = Array.new(6) { |i| solid.call((i % 3) / 2.0, ((i + 1) % 3) / 2.0, ((i + 2) % 3) / 2.0) }
cube = g.new_cube_image(faces: faces)
puts "  texture_type -> #{cube.get_texture_type.inspect}"
puts "  layer_count  -> #{cube.get_layer_count}"

# Wrong face count is an error (faithful to the engine's 6-faces requirement).
begin
  g.new_cube_image(faces: [solid.call(1, 0, 0), solid.call(0, 1, 0)])
  puts "  ERROR: a 2-face cube should have raised"
rescue => e
  puts "  2-face cube raises -> #{e.class}"
end

puts
puts "=== draw the array texture's layer 0 via a sprite batch ==="
batch = g.new_sprite_batch(texture: arr, size: 8)
batch.add_layer(layer: 2, x: 220, y: 180, ox: 8, oy: 8, sx: 8.0, sy: 8.0)
g.set_background_color(r: 0.08, g: 0.09, b: 0.12)
30.times do
  Love::Event.pump
  g.origin
  g.clear
  g.set_color(r: 1.0, g: 1.0, b: 1.0)
  g.draw(drawable: batch)
  g.present
  Love::Timer.sleep(seconds: 0.016)
end

puts "created 2d/array/volume/cube textures; drew an array-texture layer"
