# Exercises the graphics Mesh object type (standard vertex format) on the real
# backend: Love::Graphics.new_mesh and the Love::Mesh methods (vertices,
# texture, draw mode, vertex map, draw range).
#
#   ./love_mrb_harness mesh_test.rb

unless Love.const_defined?(:Window)
  puts "(no display available -- Love::Window not registered; skipping)"
  exit
end

Love::Window.set_mode(width: 640, height: 480, centered: true)
Love::Window.set_title(title: "graphics Mesh")

g = Love::Graphics
unless Love.const_defined?(:Graphics) && g.active?
  puts "(graphics not active -- needs a window)"
  exit
end

# A vertex is [x, y, u, v, r, g, b, a]. A coloured quad (two triangles via map).
verts = [
  [-100, -100, 0, 0, 1.0, 0.2, 0.2, 1.0],
  [ 100, -100, 1, 0, 0.2, 1.0, 0.2, 1.0],
  [ 100,  100, 1, 1, 0.2, 0.2, 1.0, 1.0],
  [-100,  100, 0, 1, 1.0, 1.0, 0.2, 1.0],
]

puts "=== new_mesh ==="
mesh = g.new_mesh(vertices: verts, mode: "fan")
puts "  mesh -> #{mesh.class}"
puts "  get_vertex_count -> #{mesh.get_vertex_count}"
puts "  get_draw_mode    -> #{mesh.get_draw_mode.inspect}"
puts "  get_texture      -> #{mesh.get_texture.inspect}"

puts
puts "=== get_vertex / set_vertex ==="
puts "  vertex 1 -> #{mesh.get_vertex(index: 1).inspect}"
mesh.set_vertex(index: 1, vertex: [-120, -120, 0, 0, 1.0, 0.5, 0.0, 1.0])
puts "  vertex 1 after set -> #{mesh.get_vertex(index: 1).inspect}"

puts
puts "=== vertex map ==="
puts "  get_vertex_map (unset) -> #{mesh.get_vertex_map.inspect}"
mesh.set_vertex_map(map: [1, 2, 3, 1, 3, 4])   # two triangles
puts "  after set -> #{mesh.get_vertex_map.inspect}"
mesh.set_draw_mode(mode: "triangles")
puts "  draw mode now -> #{mesh.get_draw_mode.inspect}"

puts
puts "=== draw range ==="
puts "  get_draw_range (unset) -> #{mesh.get_draw_range.inspect}"
mesh.set_draw_range(start: 1, count: 3)
puts "  after set 1,3 -> #{mesh.get_draw_range.inspect}"
mesh.set_draw_range
puts "  after reset   -> #{mesh.get_draw_range.inspect}"

puts
puts "=== textured mesh + draw ==="
idata = Love::Image.new_image_data(width: 32, height: 32, format: "rgba8")
idata.map_pixel(width: 32, height: 32) do |x, y, _r, _gg, _b, _a|
  ((x / 8) + (y / 8)).even? ? [1.0, 1.0, 1.0, 1.0] : [0.5, 0.5, 0.5, 1.0]
end
mesh.set_texture(texture: g.new_image(file: idata))
puts "  get_texture -> #{mesh.get_texture.class}"

g.set_background_color(r: 0.08, g: 0.09, b: 0.12)
frames = 60
frames.times do |i|
  Love::Event.pump
  g.origin
  g.clear
  g.set_color(r: 1.0, g: 1.0, b: 1.0)
  g.draw(drawable: mesh, x: 320, y: 240, r: i * 0.02, sx: 1.2, sy: 1.2)
  g.present
  Love::Timer.sleep(seconds: 0.016)
end

puts "drew #{frames} frames (textured, vertex-mapped mesh)"
