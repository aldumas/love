# Smoke-tests the mouse module now that it runs on the REAL
# love::mouse::sdl::Mouse backend: position, buttons, visibility, grab,
# relative mode, and the cursor object family.
#
#   ./love_mrb_harness mouse_test.rb

unless Love.const_defined?(:Window)
  puts "(no display available -- Love::Window not registered; skipping)"
  exit
end

Love::Window.set_mode(width: 480, height: 320, centered: true)
Love::Window.set_title(title: "mouse (real backend)")
Love::Event.pump

m = Love::Mouse

puts "=== position ==="
puts "  get_position -> #{m.get_position.inspect}"
puts "  get_x / get_y -> #{m.get_x}, #{m.get_y}"
m.set_position(x: 100, y: 80)
Love::Event.pump
puts "  after set_position(100,80) -> #{m.get_position.inspect}"
puts "  get_global_position -> #{m.get_global_position.inspect}"

puts
puts "=== buttons / visibility ==="
puts "  down?(button: 1)      -> #{m.down?(button: 1)}"
puts "  down?(button: [1,2])  -> #{m.down?(button: [1, 2])}"
puts "  visible? -> #{m.visible?}"
m.set_visible(visible: false)
puts "  after set_visible(false) -> visible? #{m.visible?}"
m.set_visible(visible: true)
puts "  cursor_supported? -> #{m.cursor_supported?}"

puts
puts "=== grab / relative mode ==="
m.set_grabbed(grabbed: true)
puts "  grabbed? (after grab) -> #{m.grabbed?}"
m.set_grabbed(grabbed: false)
puts "  grabbed? (after release) -> #{m.grabbed?}"
ok = m.set_relative_mode(enable: true)
puts "  set_relative_mode(true) -> #{ok}; relative_mode? -> #{m.relative_mode?}"
m.set_relative_mode(enable: false)

puts
puts "=== cursor objects ==="
sys = m.get_system_cursor(type: "hand")
puts "  get_system_cursor(hand) -> #{sys.class} type=#{sys.get_type.inspect}"
m.set_cursor(cursor: sys)
puts "  get_cursor -> #{m.get_cursor.class}"

idata = Love::Image.new_image_data(width: 16, height: 16, format: "rgba8")
idata.map_pixel(width: 16, height: 16) { |x, y, *_| [1.0, 0.3, 0.3, ((x + y).even? ? 1.0 : 0.0)] }
img_cursor = m.new_cursor(image_data: idata, hotx: 0, hoty: 0)
puts "  new_cursor -> #{img_cursor.class} type=#{img_cursor.get_type.inspect}"
m.set_cursor(cursor: img_cursor)
puts "  get_cursor (image) -> #{m.get_cursor.class}"
m.set_cursor   # reset to default

puts
puts "OK -- mouse module exercised on the real sdl::Mouse backend"
