# Exercises the graphics SpriteBatch object type on the real backend:
#   Love::Graphics.new_sprite_batch and the Love::SpriteBatch methods
#   (add / set / clear / set_color / get_count / draw range), drawn batched.
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
