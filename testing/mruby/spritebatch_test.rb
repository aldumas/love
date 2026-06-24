# Exercises the graphics SpriteBatch object type on the real backend:
#   Love::Graphics.new_sprite_batch and the Love::SpriteBatch methods
#   (add / set / clear / set_color / get_count / draw range), drawn batched,
#   plus array-texture layer sprites (add_layer / set_layer over an array
#   texture from new_array_image) and attach_attribute.
#
#   ./love_mrb_harness spritebatch_test.rb

unless Love.const_defined?(:Window)
  puts "(no display available -- Love::Window not registered; skipping)"
  exit
end

Love::Window.set_mode(width: 640, height: 480, centered: true)
Love::Window.set_title(title: "graphics SpriteBatch")

g = Love::Graphics
unless Love.const_defined?(:Graphics) && g.active?
  puts "(graphics not active -- needs a window)"
  exit
end

# A 16x16 checker texture for the batch.
idata = Love::Image.new_image_data(width: 16, height: 16, format: "rgba8")
idata.map_pixel(width: 16, height: 16) do |x, y, _r, _gg, _b, _a|
  ((x / 4) + (y / 4)).even? ? [1.0, 0.85, 0.2, 1.0] : [0.9, 0.4, 0.1, 1.0]
end
tex = g.new_image(file: idata)

puts "=== new_sprite_batch ==="
batch = g.new_sprite_batch(texture: tex, size: 256)
puts "  batch -> #{batch.class}"
puts "  get_texture -> #{batch.get_texture.class}"
puts "  get_buffer_size -> #{batch.get_buffer_size}"
puts "  get_count (empty) -> #{batch.get_count}"

puts
puts "=== add / set / count ==="
q = g.new_quad(x: 0, y: 0, width: 16, height: 16, texture: tex)
i1 = batch.add(quad: q, x: 10, y: 10)
i2 = batch.add(quad: q, x: 40, y: 10, sx: 2.0, sy: 2.0)
puts "  add returned indices -> #{i1}, #{i2}"
puts "  get_count -> #{batch.get_count}"
batch.set(index: i1, quad: q, x: 10, y: 10, r: 0.3)
puts "  set sprite #{i1} (rotated) OK"

batch.set_color(r: 1.0, g: 1.0, b: 1.0, a: 1.0)
puts "  get_color -> #{batch.get_color.inspect}"

puts
puts "=== draw range ==="
puts "  get_draw_range (unset) -> #{batch.get_draw_range.inspect}"
batch.set_draw_range(start: 1, count: 1)
puts "  after set 1,1 -> #{batch.get_draw_range.inspect}"
batch.set_draw_range
puts "  after reset   -> #{batch.get_draw_range.inspect}"

puts
puts "=== build a grid and draw the batch ==="
batch.clear
n = 0
(0...10).each do |row|
  (0...16).each do |col|
    batch.add(quad: q, x: 40 + col * 34, y: 40 + row * 40, ox: 8, oy: 8,
              r: (row + col) * 0.1, sx: 1.5, sy: 1.5)
    n += 1
  end
end
puts "  added #{n} sprites; get_count -> #{batch.get_count}"

g.set_background_color(r: 0.07, g: 0.08, b: 0.11)
frames = 60
frames.times do |i|
  Love::Event.pump
  g.origin
  g.clear
  g.set_color(r: 1.0, g: 1.0, b: 1.0)
  # One draw call for the whole batch, gently bobbing.
  g.draw(drawable: batch, x: 0, y: 10.0 * Math.sin(i * 0.1))
  g.present
  Love::Timer.sleep(seconds: 0.016)
end

puts "drew #{frames} frames (#{n} sprites in one batched draw call)"

puts
puts "=== array-texture layers (add_layer / set_layer) ==="
# Build a 2D array texture from three solid-colour 16x16 layers.
make_layer = lambda do |r, gg, b|
  d = Love::Image.new_image_data(width: 16, height: 16, format: "rgba8")
  d.map_pixel(width: 16, height: 16) { |_x, _y, _r, _gg, _b, _a| [r, gg, b, 1.0] }
  d
end
arr_tex = g.new_array_image(layers: [
  make_layer.call(1.0, 0.3, 0.3),
  make_layer.call(0.3, 1.0, 0.3),
  make_layer.call(0.3, 0.5, 1.0),
])
puts "  array texture -> #{arr_tex.class}"
abatch = g.new_sprite_batch(texture: arr_tex, size: 64)
li1 = abatch.add_layer(layer: 1, x: 120, y: 240, ox: 8, oy: 8, sx: 3.0, sy: 3.0)
li2 = abatch.add_layer(layer: 2, x: 220, y: 240, ox: 8, oy: 8, sx: 3.0, sy: 3.0)
li3 = abatch.add_layer(layer: 3, x: 320, y: 240, ox: 8, oy: 8, sx: 3.0, sy: 3.0)
puts "  add_layer indices -> #{li1}, #{li2}, #{li3}"
puts "  get_count -> #{abatch.get_count}"
abatch.set_layer(index: li1, layer: 3, x: 120, y: 240, ox: 8, oy: 8, sx: 3.0, sy: 3.0)
puts "  set_layer on sprite #{li1} (now layer 3) OK"

# add_layer on a non-array-texture batch is an error (faithful engine guard).
begin
  batch.add_layer(layer: 1, x: 0, y: 0)
  puts "  ERROR: add_layer on a 2D-texture batch should have raised"
rescue => e
  puts "  add_layer on 2D-texture batch raises -> #{e.class}"
end

puts
puts "=== attach_attribute ==="
# Per-sprite attribute buffer; needs >= sprites*4 vertices (4 per sprite).
tintbuf = g.new_buffer(
  format: [{ name: "Tint", format: "floatvec4", location: 5 }],
  data: [[1.0, 1.0, 1.0, 1.0]] * (abatch.get_count * 4),
  usage_flags: ["vertex"])
abatch.attach_attribute(name: "Tint", buffer: tintbuf)
puts "  attach_attribute(Tint) OK"

g.set_background_color(r: 0.07, g: 0.08, b: 0.11)
30.times do
  Love::Event.pump
  g.origin
  g.clear
  g.set_color(r: 1.0, g: 1.0, b: 1.0)
  g.draw(drawable: abatch)
  g.present
  Love::Timer.sleep(seconds: 0.016)
end
puts "drew the array-texture-layer batch"
