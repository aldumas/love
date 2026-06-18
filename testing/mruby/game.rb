# Demo "game" for the mruby-port boot sequence.
#
# Run it through the boot pipeline (arg.rb -> callbacks.rb -> boot.rb):
#
#   ./love_mrb_harness --boot game.rb
#
# This file is loaded by Love.init (like main.lua in a real LÖVE game) and
# defines the standard callbacks as singleton methods on Love. The boot Fiber
# then runs Love.load once and Love.update/Love.draw once per frame.

def Love.conf(t)
  t[:title] = "mruby boot demo"
  puts "[conf]   title set to #{t[:title].inspect}"
end

def Love.load(args, raw)
  @frame = 0
  @elapsed = 0.0
  puts "[load]   game starting (parsed args: #{args.inspect})"
end

def Love.update(dt)
  @frame += 1
  @elapsed += dt
  # Ask the loop to stop after 5 frames (stands in for an event-module quit).
  Love.quit! if @frame >= 5
end

def Love.draw
  puts format("[draw]   frame %d  (t=%.3fs)", @frame, @elapsed)
end

def Love.quit
  puts "[quit]   shutting down after #{@frame} frames"
  false # returning true would veto the quit
end
