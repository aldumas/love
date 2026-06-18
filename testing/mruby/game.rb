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
  t[:window] = { width: 640, height: 480, resizable: true }
  puts "[conf]   title set to #{t[:title].inspect}, window #{t[:window].inspect}"
end

def Love.load(args, raw)
  @frame = 0
  @elapsed = 0.0
  puts "[load]   game starting (parsed args: #{args.inspect})"
  if Love.const_defined?(:Window)
    m = Love::Window.get_mode
    puts "[load]   window open=#{Love::Window.is_open} #{m[:width]}x#{m[:height]} title=#{Love::Window.get_title.inspect}"
  end
end

def Love.update(dt)
  @frame += 1
  @elapsed += dt
  # Push a real quit event through love.event after 5 frames. The run loop pumps
  # and polls it, then routes :quit through Love.quit (which may veto).
  Love::Event.quit(code: 0) if @frame >= 5
end

def Love.draw
  puts format("[draw]   frame %d  (t=%.3fs)", @frame, @elapsed)
end

# Input callbacks: with love.event ported, these fire when the queue carries
# matching events. (Headless there's no window, so they're quiet here, but the
# wiring is exercised the moment window lands.)
def Love.keypressed(key, scancode, isrepeat)
  puts "[key]    #{key} pressed"
  Love::Event.quit(code: 0) if key == "escape"
end

def Love.quit
  puts "[quit]   shutting down after #{@frame} frames"
  false # returning true would veto the quit
end
