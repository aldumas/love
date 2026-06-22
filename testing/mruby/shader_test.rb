# Exercises the graphics shader slice on the real backend:
#   Love::Graphics.new_shader / set_shader / get_shader and the Love::Shader
#   object type (send / send_color / has_uniform? / get_warnings).
#
#   ./love_mrb_harness shader_test.rb

unless Love.const_defined?(:Window)
  puts "(no display available -- Love::Window not registered; skipping)"
  exit
end

Love::Window.set_mode(width: 640, height: 480, centered: true)
Love::Window.set_title(title: "graphics shaders")

g = Love::Graphics
unless Love.const_defined?(:Graphics) && g.active?
  puts "(graphics not active -- needs a window)"
  exit
end

PIXEL = <<~GLSL
  uniform float u_time;
  uniform vec4  u_tint;
  uniform vec2  u_resolution;
  vec4 effect(vec4 color, Image tex, vec2 tc, vec2 sc) {
    vec2 uv = sc / u_resolution;
    float w = 0.5 + 0.5 * sin(u_time + uv.x * 6.2831);
    return vec4(uv.x, uv.y, w, 1.0) * u_tint;
  }
GLSL

puts "=== new_shader ==="
shader = g.new_shader(pixel: PIXEL)
puts "  compiled -> #{shader.class}"
warn = shader.get_warnings
puts "  warnings -> #{warn.empty? ? '(none)' : warn}"

puts
puts "=== has_uniform? / send ==="
puts "  has_uniform?(u_time)  -> #{shader.has_uniform?(name: 'u_time')}"
puts "  has_uniform?(u_tint)  -> #{shader.has_uniform?(name: 'u_tint')}"
puts "  has_uniform?(nope)    -> #{shader.has_uniform?(name: 'does_not_exist')}"

shader.send(name: "u_resolution", value: [640.0, 480.0])  # vec2
shader.send(name: "u_time", value: 0.0)                    # float scalar
shader.send_color(name: "u_tint", value: [1.0, 1.0, 1.0, 1.0])  # vec4 color
puts "  sent u_resolution / u_time / u_tint OK"

puts
puts "=== set_shader / get_shader ==="
puts "  get_shader (before) -> #{g.get_shader.inspect}"
g.set_shader(shader: shader)
puts "  get_shader (after)  -> #{g.get_shader.class}"

puts
puts "=== drawing with the shader ==="
g.set_background_color(r: 0.05, g: 0.05, b: 0.08)

# A 1x1 white image to stretch across the screen (the effect ignores texels).
white = Love::Image.new_image_data(width: 1, height: 1, format: "rgba8")
white.set_pixel(x: 0, y: 0, color: [1.0, 1.0, 1.0, 1.0])
tex = g.new_image(file: white)

frames = 60
frames.times do |i|
  Love::Event.pump
  g.origin
  g.clear
  shader.send(name: "u_time", value: i * 0.08)
  g.set_color(r: 1.0, g: 1.0, b: 1.0)
  g.draw(drawable: tex, x: 0, y: 0, sx: 640.0, sy: 480.0)
  g.present
  Love::Timer.sleep(seconds: 0.016)
end

g.set_shader   # back to default
puts "  reset to default; get_shader -> #{g.get_shader.inspect}"
puts "drew #{frames} frames through a custom pixel shader"
