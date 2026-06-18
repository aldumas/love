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
puts "Try editing this file (testing/mruby/sample.rb) and re-running!"
