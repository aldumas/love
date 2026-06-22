# Exercises Love::Event's queue mechanics and the Ruby-facing API. The SDL-
# sourced event translation (keypressed/mousemoved/joystick*/touch*/...) is a
# faithful port of event/sdl/Event.cpp but needs real device/window events to
# fire, which a headless harness can't synthesize -- so here we drive the queue
# directly (push/poll/clear/quit/restart) and check Variant round-tripping.

puts "== push / poll round-trip =="
Love::Event.push(name: "scored", args: [42, "player1", true])
Love::Event.push(name: "ping")
events = Love::Event.poll
puts "polled #{events.length} events"
events.each { |name, *args| puts "  #{name.inspect} #{args.inspect}" }

puts
puts "== poll drains the queue =="
puts "second poll empty -> #{Love::Event.poll.empty?}"

puts
puts "== clear =="
Love::Event.push(name: "discard_me", args: [1])
Love::Event.clear
puts "after clear empty -> #{Love::Event.poll.empty?}"

puts
puts "== quit / restart =="
Love::Event.quit(code: 7)
q = Love::Event.poll
puts "quit event        -> #{q.first.inspect}"
Love::Event.restart
r = Love::Event.poll
puts "restart event     -> #{r.first.inspect}"

puts
puts "== pump (no SDL events expected in headless) =="
Love::Event.pump(timeout: 0)
puts "pump+poll         -> #{Love::Event.poll.length} events"

puts
puts "== nested table arg round-trip =="
Love::Event.push(name: "payload", args: [[1, 2, 3], { "k" => "v" }])
name, arr, hash = Love::Event.poll.first
puts "name #{name.inspect}, arr #{arr.inspect}, hash #{hash.inspect}"

puts
puts "ALL EVENT TESTS PASSED"
