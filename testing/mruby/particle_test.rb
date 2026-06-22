# Exercises the graphics ParticleSystem object type on the real backend:
#   Love::Graphics.new_particle_system and the Love::ParticleSystem config +
#   lifecycle API (emission, lifetime, speed, colors, sizes, emit/update/draw).
#
#   ./love_mrb_harness particle_test.rb

unless Love.const_defined?(:Window)
  puts "(no display available -- Love::Window not registered; skipping)"
  exit
end

Love::Window.set_mode(width: 640, height: 480, centered: true)
Love::Window.set_title(title: "graphics ParticleSystem")

g = Love::Graphics
unless Love.const_defined?(:Graphics) && g.active?
  puts "(graphics not active -- needs a window)"
  exit
end

# A small soft-ish dot texture.
idata = Love::Image.new_image_data(width: 8, height: 8, format: "rgba8")
idata.map_pixel(width: 8, height: 8) do |x, y, _r, _gg, _b, _a|
  dx = x - 3.5; dy = y - 3.5
  d = Math.sqrt(dx * dx + dy * dy)
  a = [1.0 - d / 4.0, 0.0].max
  [1.0, 1.0, 1.0, a]
end
tex = g.new_image(file: idata)

puts "=== new_particle_system ==="
ps = g.new_particle_system(texture: tex, size: 512)
puts "  ps -> #{ps.class}"
puts "  get_texture -> #{ps.get_texture.class}"
puts "  get_buffer_size -> #{ps.get_buffer_size}"

puts
puts "=== configure ==="
ps.set_emission_rate(rate: 200)
ps.set_emitter_lifetime(life: -1)            # forever
ps.set_particle_lifetime(min: 0.5, max: 1.5)
ps.set_position(x: 320, y: 360)
ps.set_direction(direction: -Math::PI / 2)   # upward
ps.set_spread(spread: Math::PI / 4)
ps.set_speed(min: 80, max: 180)
ps.set_linear_acceleration(xmin: -20, ymin: 80, xmax: 20, ymax: 120)  # gravity-ish
ps.set_sizes(sizes: [1.0, 1.6, 0.2])
ps.set_size_variation(variation: 0.4)
ps.set_rotation(min: 0, max: 6.28)
ps.set_spin(start: 0.0, end: 3.0)
ps.set_colors(colors: [[1.0, 0.9, 0.3, 1.0], [1.0, 0.4, 0.1, 0.8], [0.4, 0.1, 0.0, 0.0]])
puts "  get_particle_lifetime -> #{ps.get_particle_lifetime.inspect}"
puts "  get_speed             -> #{ps.get_speed.inspect}"
puts "  get_linear_acceleration -> #{ps.get_linear_acceleration.inspect}"
puts "  get_sizes  -> #{ps.get_sizes.inspect}"
puts "  get_colors (count) -> #{ps.get_colors.length}"
puts "  get_emission_rate  -> #{ps.get_emission_rate}"

puts
puts "=== lifecycle ==="
puts "  active? (before start) -> #{ps.active?}"
ps.start
puts "  active? (after start)  -> #{ps.active?}"
ps.emit(count: 50)
puts "  get_count after emit 50 -> #{ps.get_count}"
ps.update(dt: 0.1)
puts "  get_count after update  -> #{ps.get_count}"

puts
puts "=== simulate + draw ==="
g.set_background_color(r: 0.03, g: 0.03, b: 0.06)
g.set_blend_mode(mode: "add")
frames = 90
frames.times do |i|
  Love::Event.pump
  ps.set_position(x: 320 + 120 * Math.sin(i * 0.05), y: 360)
  ps.update(dt: 0.016)
  g.origin
  g.clear
  g.set_color(r: 1.0, g: 1.0, b: 1.0)
  g.draw(drawable: ps)
  g.present
  Love::Timer.sleep(seconds: 0.016)
end
g.set_blend_mode(mode: "alpha")

puts "drew #{frames} frames; final live particle count -> #{ps.get_count}"
