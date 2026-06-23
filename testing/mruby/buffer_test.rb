# Exercises the graphics Buffer object type and GraphicsReadback on the real
# OpenGL backend: Love::Graphics.new_buffer / readback_buffer(_async) /
# readback_texture(_async) and the Love::Buffer / Love::GraphicsReadback methods.
#
#   ./love_mrb_harness buffer_test.rb

unless Love.const_defined?(:Window)
  puts "(no display available -- Love::Window not registered; skipping)"
  exit
end

Love::Window.set_mode(width: 320, height: 240, centered: true)
Love::Window.set_title(title: "graphics Buffer")

g = Love::Graphics
unless Love.const_defined?(:Graphics) && g.active?
  puts "(graphics not active -- needs a window)"
  exit
end

# A vertex format: vec2 position + unorm8 colour. Two members, 6 components.
format = [
  { name: "Position", format: "floatvec2",  location: 0 },
  { name: "VertexColor", format: "unorm8vec4", location: 1 },
]

puts "=== new_buffer (flat array) ==="
flat = [
  0.0, 0.0,  1.0, 0.0, 0.0, 1.0,
  1.0, 0.0,  0.0, 1.0, 0.0, 1.0,
  0.5, 1.0,  0.0, 0.0, 1.0, 1.0,
]
buf = g.new_buffer(format: format, data: flat, usage_flags: ["vertex"], debug_name: "test-vbo")
puts "  buf -> #{buf.class}"
puts "  get_element_count  -> #{buf.get_element_count}"
puts "  get_element_stride -> #{buf.get_element_stride}"
puts "  get_size           -> #{buf.get_size}"
puts "  get_debug_name     -> #{buf.get_debug_name.inspect}"
puts "  buffer_type?(vertex) -> #{buf.buffer_type?(type: 'vertex')}"
puts "  buffer_type?(index)  -> #{buf.buffer_type?(type: 'index')}"
puts "  get_format:"
buf.get_format.each { |m| puts "    #{m.inspect}" }

puts
puts "=== new_buffer (count: empty) + set_array_data ==="
buf2 = g.new_buffer(format: format, count: 3, usage_flags: ["vertex"])
puts "  element_count -> #{buf2.get_element_count}"
# array-of-arrays variant
buf2.set_array_data(data: [
  [-1, -1, 1, 1, 1, 1],
  [ 1, -1, 1, 1, 1, 1],
  [ 1,  1, 1, 1, 1, 1],
])
puts "  set_array_data (table-of-tables) ok"
# flat variant into a sub-range
buf2.set_array_data(data: [0.0, 0.0, 0.5, 0.5, 0.5, 1.0], dest_index: 2)
puts "  set_array_data (flat, dest_index 2) ok"
buf2.clear
puts "  clear ok"

puts
puts "=== readback_buffer (sync) ==="
bytes = g.readback_buffer(buffer: buf)
puts "  -> #{bytes.class}, size #{bytes.get_size} (expect #{buf.get_size})"

puts
puts "=== readback_buffer_async ==="
rb = g.readback_buffer_async(buffer: buf)
puts "  -> #{rb.class}"
rb.wait
puts "  complete? -> #{rb.complete?}   error? -> #{rb.error?}"
d = rb.get_buffer_data
puts "  get_buffer_data -> #{d.class}, size #{d.get_size}"

puts
puts "=== readback_texture (canvas -> ImageData) ==="
canvas = g.new_canvas(width: 8, height: 8)
g.set_canvas(canvas: canvas)
g.clear(r: 1.0, g: 0.0, b: 0.0, a: 1.0)
g.set_canvas
img = g.readback_texture(texture: canvas)
puts "  -> #{img.class}, #{img.get_width}x#{img.get_height}"
puts "  pixel(0,0) -> #{img.get_pixel(x: 0, y: 0).inspect}"

puts
puts "=== readback_texture_async ==="
rt = g.readback_texture_async(texture: canvas)
rt.wait
puts "  complete? -> #{rt.complete?}   error? -> #{rt.error?}"
img2 = rt.get_image_data
puts "  get_image_data -> #{img2.class}, #{img2.get_width}x#{img2.get_height}"

puts
puts "all buffer/readback checks passed"
