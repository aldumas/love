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

puts
puts "Try editing this file (testing/mruby/sample.rb) and re-running!"
