# Exercises the graphics Mesh object type on the real backend:
# Love::Graphics.new_mesh and the Love::Mesh methods (vertices, texture, draw
# mode, vertex map, draw range) for the standard vertex format, plus custom
# vertex formats (get_vertex_format / per-attribute access / attribute enable),
# attached attributes (binding a Buffer), an explicit index buffer, and the
# Data form of set_vertex_map.
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

puts
puts "=== custom vertex format ==="
# A non-standard layout: position (vec2) + a float colour at locations 0 and 2.
fmt = [
  { name: "VertexPosition", format: "floatvec2", location: 0 },
  { name: "VertexColor",    format: "floatvec4", location: 2 },
]
cverts = [
  [-60, -50, 1.0, 0.0, 0.0, 1.0],
  [ 60, -50, 0.0, 1.0, 0.0, 1.0],
  [  0,  70, 0.0, 0.0, 1.0, 1.0],
]
cm = g.new_mesh(format: fmt, vertices: cverts, mode: "triangles")
puts "  vertex count  -> #{cm.get_vertex_count}"
puts "  vertex format -> #{cm.get_vertex_format.map { |m| [m[:name], m[:format], m[:location]] }.inspect}"
puts "  get_vertex(1) -> #{cm.get_vertex(index: 1).inspect}"
cm.set_vertex(index: 2, vertex: [70, -40, 1.0, 1.0, 0.0, 1.0])
puts "  get_vertex(2) after set_vertex -> #{cm.get_vertex(index: 2).inspect}"
cm.set_vertex_attribute(index: 3, attribute: 2, value: [0.5, 0.5, 0.5, 0.5])
puts "  get_vertex_attribute(3, 2) -> #{cm.get_vertex_attribute(index: 3, attribute: 2).inspect}"
puts "  attribute_enabled?(VertexColor) -> #{cm.attribute_enabled?(name: 'VertexColor')}"
cm.set_attribute_enabled(name: "VertexColor", enable: false)
puts "  after disable -> #{cm.attribute_enabled?(name: 'VertexColor')}"
cm.set_attribute_enabled(name: "VertexColor", enable: true)
puts "  empty custom mesh get_vertex(1) -> #{g.new_mesh(format: fmt, count: 2).get_vertex(index: 1).inspect}"

puts
puts "=== attached attribute + index buffer ==="
puts "  own vertex buffer -> #{mesh.get_vertex_buffer.class}"
# A per-vertex colour buffer feeding the standard mesh's VertexColor attribute.
cbuf = g.new_buffer(
  format: [{ name: "VertexColor", format: "floatvec4", location: 2 }],
  data: [[1, 0, 0, 1], [0, 1, 0, 1], [0, 0, 1, 1], [1, 1, 1, 1]],
  usage_flags: ["vertex"])
mesh.attach_attribute(name: "VertexColor", buffer: cbuf)
# getAttachedAttributes lists every attribute (built-ins included); VertexColor
# now points at cbuf. detach reverts it to the mesh's own buffer (still listed).
puts "  attributes -> #{mesh.get_attached_attributes.map { |a| a[:name] }.inspect}"
puts "  detach(VertexColor) -> #{mesh.detach_attribute(name: 'VertexColor')}"
puts "  attribute count after detach -> #{mesh.get_attached_attributes.length}"

# An explicit index buffer (uint16) describing two triangles.
ibuf = g.new_buffer(format: "uint16", data: [0, 1, 2, 0, 2, 3], usage_flags: ["index"])
mesh.set_index_buffer(buffer: ibuf)
puts "  index buffer -> #{mesh.get_index_buffer.class}"
mesh.set_index_buffer
puts "  index buffer after clear -> #{mesh.get_index_buffer.inspect}"

# set_vertex_map from a packed Data of uint16 indices (0-based on the wire).
idata = Love::Data.pack(format: "<HHHHHH", values: [0, 1, 2, 0, 2, 3], container: "data")
mesh.set_vertex_map(map: idata, index_type: "uint16")
puts "  vertex map from Data -> #{mesh.get_vertex_map.inspect}"
