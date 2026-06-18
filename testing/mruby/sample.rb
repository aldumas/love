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
puts "Try editing this file (testing/mruby/sample.rb) and re-running!"
