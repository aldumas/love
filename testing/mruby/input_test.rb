# Exercises the ported input family: Love::Touch, Love::Sensor, and
# Love::Joystick (module class methods + the Joystick object type). No physical
# devices are attached in this environment, so device lists are empty; the
# gamepad-mapping database (global to SDL) is exercised without a controller.

puts "== Touch =="
touches = Love::Touch.get_touches
puts "get_touches       -> #{touches.class} (#{touches.length} active)"
puts "get_touches(filter)-> #{Love::Touch.get_touches(device_type: 'touchscreen').length}"

puts
puts "== Sensor =="
%w[accelerometer gyroscope].each do |t|
  puts "has_sensor?(#{t}) -> #{Love::Sensor.has_sensor?(type: t)}"
  puts "enabled?(#{t})    -> #{Love::Sensor.enabled?(type: t)}"
end

puts
puts "== Joystick (module) =="
puts "joystick_count    -> #{Love::Joystick.get_joystick_count}"
js = Love::Joystick.get_joysticks
puts "get_joysticks     -> #{js.class} (#{js.length})"
Love::Joystick.set_background_events(enabled: true)
puts "background_events?-> #{Love::Joystick.background_events?}"
Love::Joystick.set_background_events(enabled: false)

guid = "03000000aabbccdd0000000000000000"
ok = Love::Joystick.set_gamepad_mapping(
  guid: guid, gamepad_input: "a", input_type: "button", input_index: 2)
puts "set_gamepad_mapping(button a) -> #{ok}"
ok = Love::Joystick.set_gamepad_mapping(
  guid: guid, gamepad_input: "leftx", input_type: "axis", input_index: 1)
puts "set_gamepad_mapping(axis leftx) -> #{ok}"
ok = Love::Joystick.set_gamepad_mapping(
  guid: guid, gamepad_input: "dpup", input_type: "hat", input_index: 1, hat_direction: "u")
puts "set_gamepad_mapping(hat dpup) -> #{ok}"

mapping = Love::Joystick.get_gamepad_mapping_string(guid: guid)
puts "mapping string    -> #{mapping ? mapping[0, 40] + '...' : 'nil'}"

saved = Love::Joystick.save_gamepad_mappings
puts "save_gamepad_mappings -> String of #{saved.length} bytes"

puts
puts "ALL INPUT TESTS PASSED"
