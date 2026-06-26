# Row-8 shutdown-ordering probe: boots the full engine, then on the first run
# requests a restart (Love::Event.restart -> boot returns "restart" -> the C
# host tears down the whole mrb_state via mrbx_close_state and re-opens a fresh
# one IN THE SAME PROCESS), and on the second run quits normally. This drives a
# real teardown+re-init cycle so a sanitizer can catch shutdown-ordering UAFs /
# double-frees / leaks across the module-singleton lifecycle.
#
# Run it like any game:  ./build-mrb-<kind>/love testing/mruby/restart_probe.rb
# It uses a temp marker file (via mruby-io's IO) to tell the two boots apart;
# delete it (or let the second boot consume it) between independent runs.
MARKER = "/tmp/love_mrb_restart_probe.marker"

def Love.conf(t)
  t[:title] = "restart probe"
  t[:window] = { width: 320, height: 240 }
end

def Love.load(args, raw)
  @frame = 0
  @restarting = (begin; IO.read(MARKER); true; rescue StandardError; false; end)
  puts "[probe] boot; restarting=#{@restarting}"
  if Love.const_defined?(:Graphics)
    Love::Graphics.set_background_color(r: 0.1, g: 0.2, b: 0.3)
  end
end

def Love.update(dt)
  @frame += 1
  return if @frame < 4
  if @restarting
    puts "[probe] second boot -> quit 0"
    Love::Event.quit(code: 0)
  else
    IO.open(IO.sysopen(MARKER, "w"), "w") { |io| io.write("1") }
    puts "[probe] first boot -> restart"
    Love::Event.restart
  end
end

def Love.draw
  if Love.const_defined?(:Graphics)
    g = Love::Graphics
    g.set_color(r: 1.0, g: 1.0, b: 1.0)
    g.rectangle(mode: "fill", x: 120, y: 90, width: 80, height: 60)
  end
end
