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
  # A dark blue background, like the classic LÖVE default screen.
  Love::Graphics.set_background_color(r: 0.16, g: 0.18, b: 0.25) if Love.const_defined?(:Graphics)
  @x = 280.0
  @y = 200.0
end

def Love.update(dt)
  @frame += 1
  @elapsed += dt

  # Move the square with the arrow keys (or WASD), polling held keys via
  # love.keyboard.down?. Speed is framerate-independent thanks to dt.
  if Love.const_defined?(:Keyboard)
    k = Love::Keyboard
    speed = 240.0 * dt
    @x -= speed if k.down?(key: ["left", "a"])
    @x += speed if k.down?(key: ["right", "d"])
    @y -= speed if k.down?(key: ["up", "w"])
    @y += speed if k.down?(key: ["down", "s"])
  end

  # Hold the left mouse button to snap the square (centered) to the cursor.
  if Love.const_defined?(:Mouse) && Love::Mouse.down?(button: 1)
    p = Love::Mouse.get_position
    @x = p[:x] - 40
    @y = p[:y] - 40
  end

  # Auto-quit after ~10s so the demo terminates on its own; press escape (see
  # Love.keypressed) to quit sooner. Raise this to play with it longer.
  Love::Event.quit(code: 0) if @frame >= 600
end

def Love.draw
  # With the (lean) graphics module active, draw the player square at @x,@y.
  # callbacks.rb has already cleared to the background color.
  if Love.const_defined?(:Graphics) && Love::Graphics.active?
    g = Love::Graphics
    g.set_color(r: 0.9, g: 0.5, b: 0.2)
    g.rectangle(mode: "fill", x: @x, y: @y, width: 80, height: 80)
  else
    puts format("[draw]   frame %d  (t=%.3fs)  pos=(%.0f,%.0f)", @frame, @elapsed, @x, @y)
  end
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
