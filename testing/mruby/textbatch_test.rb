# Exercises the graphics TextBatch object type on the real backend:
#   Love::Graphics.new_text_batch and the Love::TextBatch methods
#   (set / setf / add / addf / clear / set_font / get_width / get_height),
#   including the colored-string-segments form of `text:`.
#
#   ./love_mrb_harness textbatch_test.rb

unless Love.const_defined?(:Window)
  puts "(no display available -- Love::Window not registered; skipping)"
  exit
end

Love::Window.set_mode(width: 640, height: 480, centered: true)
Love::Window.set_title(title: "graphics TextBatch")

g = Love::Graphics
unless Love.const_defined?(:Graphics) && g.active?
  puts "(graphics not active -- needs a window)"
  exit
end

font = g.new_font(size: 24)

puts "=== new_text_batch ==="
tb = g.new_text_batch(font: font, text: "Hello, mruby!")
puts "  batch -> #{tb.class}"
puts "  get_font -> #{tb.get_font.class}"
puts "  get_width  -> #{tb.get_width}"
puts "  get_height -> #{tb.get_height}"
puts "  get_dimensions -> #{tb.get_dimensions.inspect}"

puts
puts "=== set / setf ==="
tb.set(text: "replaced text")
puts "  after set -> width #{tb.get_width}"
tb.setf(text: "this is a longer line that should wrap within the limit", wrap: 200, align: "center")
puts "  after setf(wrap:200) -> dimensions #{tb.get_dimensions.inspect}"

puts
puts "=== add / addf (multi-segment batch) ==="
tb.clear
i1 = tb.add(text: "Line A", x: 0, y: 0)
i2 = tb.add(text: "Line B (scaled)", x: 0, y: 40, sx: 1.5, sy: 1.5)
i3 = tb.addf(text: "wrapped paragraph added at an offset", wrap: 180, align: "left", x: 0, y: 100)
puts "  add/addf indices -> #{i1}, #{i2}, #{i3}"

puts
puts "=== colored-string segments ==="
# The `text:` argument accepts the colored-segments Array form: color arrays
# ([r,g,b] or [r,g,b,a]) alternating with Strings; each color applies to the
# strings that follow it. Mirrors love.graphics's colored-string table.
colored = [[1.0, 0.4, 0.4], "red ", [0.4, 1.0, 0.4], "green ", [0.4, 0.6, 1.0, 0.8], "blue"]
ctb = g.new_text_batch(font: font, text: colored)
puts "  new_text_batch(colored) -> #{ctb.class}, width #{ctb.get_width}"
ctb.set(text: [[1.0, 1.0, 0.0], "yellow then ", [1.0, 1.0, 1.0], "white"])
puts "  set(colored) -> width #{ctb.get_width}"
idx = ctb.add(text: [[1.0, 0.5, 0.0], "orange segment"], x: 0, y: 30)
puts "  add(colored) -> index #{idx}"
# A plain String still works through the same path (one white segment).
ctb.set(text: "plain still works")
puts "  set(plain String) -> width #{ctb.get_width}"
ctb.set(text: colored)

puts
puts "=== draw the text batch ==="
g.set_background_color(r: 0.08, g: 0.09, b: 0.13)
frames = 60
frames.times do |i|
  Love::Event.pump
  g.origin
  g.clear
  g.set_color(r: 0.9, g: 0.95, b: 1.0)
  g.draw(drawable: tb, x: 60, y: 60 + 6.0 * Math.sin(i * 0.1))
  g.draw(drawable: ctb, x: 60, y: 180)
  # print also takes the colored-segments form directly.
  g.print(text: [[1.0, 0.8, 0.2], "print() ", [0.7, 0.9, 1.0], "colored"], x: 60, y: 260)
  g.present
  Love::Timer.sleep(seconds: 0.016)
end

puts "drew #{frames} frames (TextBatch with add + addf + colored segments, colored print)"
