# Exercises the graphics Font object type + text rendering on the real backend:
#   Love::Graphics.new_font / set_font / get_font / print / printf and the
#   Love::Font metrics.
#
#   ./love_mrb_harness font_test.rb

unless Love.const_defined?(:Window)
  puts "(no display available -- skipping)"
  exit
end

Love::Window.set_mode(width: 640, height: 480, centered: true)
Love::Window.set_title(title: "graphics fonts")

g = Love::Graphics
unless Love.const_defined?(:Graphics) && g.active?
  puts "(graphics not active -- needs a window)"
  exit
end

puts "=== font object ==="
f = g.new_font(size: 24)
puts "new_font(size: 24) -> #{f.class}"
puts "  get_height           -> #{f.get_height}"
puts "  get_width(text:)     -> #{f.get_width(text: 'Hello')}"
puts "  ascent/descent/base  -> #{f.get_ascent} / #{f.get_descent} / #{f.get_baseline}"
puts "  line_height          -> #{f.get_line_height}"
puts "  has_glyphs(text: AB) -> #{f.has_glyphs(text: 'AB')}"
wrap = f.get_wrap(text: 'The quick brown fox jumps over the lazy dog', width: 120)
puts "  get_wrap(width: 120) -> width=#{wrap[:width].round(1)} lines=#{wrap[:lines].inspect}"

puts
puts "=== set_font / get_font ==="
g.set_font(font: f)
puts "after set_font, get_font -> #{g.get_font.class}"
g.set_font(font: nil)             # clear -> falls back to the embedded default
dfont = g.get_font
puts "default font (cleared)   -> #{dfont.class} height=#{dfont.get_height}"

puts
puts "=== drawing text ==="
g.set_background_color(r: 0.10, g: 0.11, b: 0.15)
frames = 40
frames.times do |i|
  Love::Event.pump
  g.origin
  g.clear

  g.set_color(r: 1.0, g: 1.0, b: 1.0)
  g.print(text: 'Hello, mruby LOVE!', x: 40, y: 40, sx: 2.0, sy: 2.0, font: f)

  g.set_color(r: 0.6, g: 0.9, b: 1.0)
  g.printf(text: 'Word-wrapped, centered text rendered through the real glyph atlas and batched renderer.',
           x: 40, y: 200, limit: 320, align: 'center')

  g.present
  Love::Timer.sleep(seconds: 0.016)
end
puts "drew #{frames} frames with print + printf"
