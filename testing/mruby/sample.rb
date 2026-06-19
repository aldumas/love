# Sample playground script for the mruby-port branch.
#
# Run it with:  ./love_mrb_harness            (runs this file)
#          or:  ./love_mrb_harness myscript.rb
#
# Only the `timer` module is ported so far, exposed as Love::Timer.
# Methods use snake_case names and KEYWORD arguments (not positional).

puts "Ruby version: #{RUBY_VERSION} (mruby)"
puts "-" * 40

t0 = Love::Timer.get_time
puts "Love::Timer.get_time   -> #{t0.round(4)}  (seconds since start, Float)"
puts "Love::Timer.get_fps    -> #{Love::Timer.get_fps}  (Integer)"
puts "Love::Timer.step       -> #{Love::Timer.step.round(6)}"
puts "Love::Timer.get_delta  -> #{Love::Timer.get_delta.round(6)}"

# Keyword argument call. Positional `Love::Timer.sleep(0.1)` would NOT work --
# this is the whole point of the new convention.
nap = 0.1
puts
puts "Love::Timer.sleep(seconds: #{nap})  ..."
Love::Timer.sleep(seconds: nap)

elapsed = Love::Timer.get_time - t0
puts "elapsed                -> #{elapsed.round(4)}s"
puts "-" * 40

# A tiny game-style loop you can mess with:
puts "ticking 5 frames at ~20fps:"
5.times do |i|
  Love::Timer.step
  Love::Timer.sleep(seconds: 0.05)
  puts "  frame #{i}: delta=#{Love::Timer.get_delta.round(4)}s fps=#{Love::Timer.get_fps}"
end

puts
puts "=" * 40
puts "Love::Math  (keyword args + an object type)"
puts "=" * 40

# Module-level random, backed by a default RandomGenerator.
puts "Love::Math.random                 -> #{Love::Math.random.round(4)}  (Float 0..1)"
puts "Love::Math.random(max: 6)         -> #{Love::Math.random(max: 6)}  (Integer 1..6, a die roll)"
puts "Love::Math.random(min: 10, max: 20) -> #{Love::Math.random(min: 10, max: 20)}"

# A seeded RandomGenerator OBJECT -- this is the new bit vs. the timer slice:
# Love::Math.new_random_generator returns a Love::RandomGenerator instance with
# its own methods, all taking keyword arguments.
rng = Love::Math.new_random_generator(seed: 42)
puts
puts "rng = Love::Math.new_random_generator(seed: 42)   -> #{rng.class}"
rolls = Array.new(5) { rng.random(min: 1, max: 6) }
puts "5 seeded dice rolls               -> #{rolls.inspect}"

# Re-seeding reproduces the sequence.
rng.set_seed(seed: 42)
rolls2 = Array.new(5) { rng.random(min: 1, max: 6) }
puts "after set_seed(seed: 42), again   -> #{rolls2.inspect}  (#{rolls == rolls2 ? 'deterministic :)' : 'mismatch!'})"

puts
puts "Love::Math.simplex_noise(x: 0.5)            -> #{Love::Math.simplex_noise(x: 0.5).round(4)}"
puts "Love::Math.simplex_noise(x: 0.5, y: 0.25)   -> #{Love::Math.simplex_noise(x: 0.5, y: 0.25).round(4)}"
puts "Love::Math.gamma_to_linear(c: 0.5)          -> #{Love::Math.gamma_to_linear(c: 0.5).round(4)}"

square = [0, 0, 10, 0, 10, 10, 0, 10]
puts "Love::Math.is_convex(points: square)        -> #{Love::Math.is_convex(points: square)}"
tris = Love::Math.triangulate(points: square)
puts "Love::Math.triangulate(points: square)      -> #{tris.length} triangles"

puts
puts "-" * 40
puts "Love::BezierCurve and Love::Transform objects"
puts "-" * 40

# A quadratic Bezier curve from three control points (flat [x,y,...] list).
curve = Love::Math.new_bezier_curve(points: [0, 0, 50, 100, 100, 0])
puts "new_bezier_curve(...)             -> #{curve.class}, degree #{curve.get_degree}"
puts "  evaluate(t: 0.5)                -> #{curve.evaluate(t: 0.5).inspect}"
rendered = curve.render(accuracy: 2)
puts "  render(accuracy: 2)            -> #{rendered.length / 2} points"

# Transform objects: chainable mutators returning self, plus the * operator.
t = Love::Math.new_transform.translate(x: 10, y: 20).scale(sx: 2, sy: 2)
puts
puts "new_transform.translate(...).scale(...) -> #{t.class}"
puts "  transform_point(x: 5, y: 5)     -> #{t.transform_point(x: 5, y: 5).inspect}"

a = Love::Math.new_transform.translate(x: 100, y: 0)
b = Love::Math.new_transform.rotate(angle: 0)
puts "  (a * b) composition             -> #{(a * b).class}"
puts "  is_affine_2d_transform          -> #{t.is_affine_2d_transform}"

puts
puts "=" * 40
puts "Love::Filesystem  (real physfs backend)"
puts "=" * 40

# Bootstrap physfs: init with the executable path, then pick a save identity.
# Love::ARG0 is provided by the harness (mirrors main(argv[0])).
Love::Filesystem.init(arg0: Love::ARG0)
Love::Filesystem.set_identity(name: "love_mrb_harness")
puts "save directory          -> #{Love::Filesystem.get_save_directory}"

Love::Filesystem.write(name: "greeting.txt", data: "hello\nfrom mruby\n")
puts "exists(greeting.txt)    -> #{Love::Filesystem.exists(path: 'greeting.txt')}"
puts "get_info                -> #{Love::Filesystem.get_info(path: 'greeting.txt').inspect}"
puts "read                    -> #{Love::Filesystem.read(name: 'greeting.txt').inspect}"
puts "lines                   -> #{Love::Filesystem.lines(name: 'greeting.txt').inspect}"

# A File object with keyword-arg methods.
f = Love::Filesystem.open_file(name: "greeting.txt", mode: "r")
puts
puts "open_file(...)          -> #{f.class} (mode=#{f.get_mode}, size=#{f.get_size})"
puts "  f.read(bytes: 5)      -> #{f.read(bytes: 5).inspect}"
f.close

# Clean up so re-runs start fresh.
Love::Filesystem.remove(name: "greeting.txt")

# --- event ----------------------------------------------------------------
# Push custom events onto the queue, then drain them. poll returns an Array of
# [:name, *args]; the name is a Symbol and the args round-trip through Variant.
puts
puts "=== Love::Event ==="
Love::Event.push(name: "score", args: [42, "ada"])
Love::Event.push(name: "ping")
Love::Event.quit(code: 7)

Love::Event.poll.each do |name, *args|
  puts "  event #{name.inspect} args=#{args.inspect}"
end

# pump drains real OS events; headless there are none, so the queue is empty.
Love::Event.pump
puts "after pump, poll          -> #{Love::Event.poll.inspect}"

# --- window ----------------------------------------------------------------
# Love::Window is only defined when a display is available (the lean backend
# needs SDL video). Guard with const_defined? like the boot loop does.
puts
puts "=== Love::Window ==="
if Love.const_defined?(:Window)
  w = Love::Window
  puts "displays                -> #{w.get_display_count}"
  puts "desktop dimensions      -> #{w.get_desktop_dimensions(display: 1).inspect}"
  puts "system theme            -> #{w.get_system_theme}"

  # set_mode flattens the old settings table into keyword arguments.
  ok = w.set_mode(width: 640, height: 480, resizable: true, centered: true)
  w.set_title(title: "mruby harness window")
  puts "set_mode(640x480)       -> #{ok}"
  puts "is_open                 -> #{w.is_open}"
  puts "get_title               -> #{w.get_title.inspect}"

  mode = w.get_mode
  puts "get_mode                -> #{mode[:width]}x#{mode[:height]} resizable=#{mode[:resizable]} display=#{mode[:display]}"
  puts "get_position            -> #{w.get_position.inspect}"
  puts "has_focus               -> #{w.has_focus}"
  puts "dpi scale               -> #{w.get_dpi_scale}"

  # Pump a few frames so the window actually appears and OS events flow through
  # the (lean) event backend; move/click the window to see events here.
  3.times do
    Love::Event.pump
    Love::Event.poll.each { |name, *a| puts "  window event #{name.inspect} #{a.inspect}" }
    Love::Timer.sleep(seconds: 0.05)
  end
else
  puts "(no display available -- Love::Window not registered)"
end

# --- graphics --------------------------------------------------------------
# A thin slice: clear the screen, draw a couple of rectangles, present. The
# lean backend creates a GL context on the window above, so active? is only
# true once a window exists. Colors are 0..1 floats; origin is top-left.
puts
puts "=== Love::Graphics ==="
if Love.const_defined?(:Graphics) && Love::Graphics.active?
  g = Love::Graphics
  puts "dimensions              -> #{g.get_dimensions.inspect}"
  g.set_background_color(r: 0.16, g: 0.18, b: 0.25)

  # Draw ~30 frames of two rectangles so the window shows something on screen.
  30.times do |i|
    Love::Event.pump
    g.origin
    g.clear
    g.set_color(r: 0.9, g: 0.5, b: 0.2)
    g.rectangle(mode: "fill", x: 40 + i * 4, y: 60, width: 120, height: 90)
    g.set_color(r: 0.4, g: 0.8, b: 1.0)
    g.rectangle(mode: "line", x: 200, y: 200, width: 160, height: 120)
    g.present
    Love::Timer.sleep(seconds: 0.016)
  end
  puts "drew 30 frames (clear + fill/line rectangles + present)"
else
  puts "(graphics not active -- needs a window)"
end

# --- keyboard --------------------------------------------------------------
# State queries via SDL: down? polls currently-held keys (a String or an Array
# of names, true if any is held), and the name<->scancode helpers round-trip
# through SDL. Names match those the event module reports in keypressed.
puts
puts "=== Love::Keyboard ==="
k = Love::Keyboard
puts "down?(key: 'a')                   -> #{k.down?(key: 'a')}  (nothing held in a flat script)"
puts "down?(key: ['lctrl', 'space'])    -> #{k.down?(key: ['lctrl', 'space'])}"
puts "get_scancode_from_key(key: 'a')   -> #{k.get_scancode_from_key(key: 'a').inspect}"
puts "get_key_from_scancode(scancode: 'space') -> #{k.get_key_from_scancode(scancode: 'space').inspect}"
puts "modifier_active?(key: 'capslock') -> #{k.modifier_active?(key: 'capslock')}"
puts "modifier_active?(key: 'shift')    -> #{k.modifier_active?(key: 'shift')}"
puts "has_screen_keyboard?              -> #{k.has_screen_keyboard?}"

puts
puts "Try editing this file (testing/mruby/sample.rb) and re-running!"
