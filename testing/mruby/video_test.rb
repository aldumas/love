# Exercises the graphics Video object type + the love.video theora backend on
# the real backend: Love::Graphics.new_video and the Love::Video methods
# (play / pause / seek / tell / playing? / get_stream / dimensions), plus the
# Love::VideoStream type. Decodes testing/resources/sample.ogv.
#
#   ./love_mrb_harness video_test.rb

unless Love.const_defined?(:Window)
  puts "(no display available -- Love::Window not registered; skipping)"
  exit
end

Love::Filesystem.init(arg0: Love::ARG0)
Love::Filesystem.set_source(path: "/home/adam/src/love/testing/resources")

Love::Window.set_mode(width: 640, height: 480, centered: true)
Love::Window.set_title(title: "graphics Video (theora)")

g = Love::Graphics
unless Love.const_defined?(:Graphics) && g.active?
  puts "(graphics not active -- needs a window)"
  exit
end

puts "=== new_video ==="
video = g.new_video(file: "sample.ogv")
puts "  video -> #{video.class}"
puts "  get_dimensions -> #{video.get_dimensions.inspect}"
puts "  is_a?(Love::Drawable) -> #{video.is_a?(Love::Drawable)}" if Love.const_defined?(:Drawable)

puts
puts "=== underlying stream ==="
stream = video.get_stream
puts "  get_stream -> #{stream.class}"
puts "  stream filename   -> #{stream.get_filename}"
puts "  stream dimensions -> #{stream.get_dimensions.inspect}"
puts "  get_source (no audio wired) -> #{video.get_source.inspect}"

puts
puts "=== playback control ==="
puts "  playing? (before) -> #{video.playing?}"
video.play
puts "  playing? (after play) -> #{video.playing?}"
video.set_filter(min: "linear", mag: "linear")
puts "  get_filter -> #{video.get_filter.inspect}"

puts
puts "=== decode + draw ==="
g.set_background_color(r: 0.05, g: 0.05, b: 0.07)
frames = 90
frames.times do |i|
  Love::Event.pump
  g.origin
  g.clear
  g.set_color(r: 1.0, g: 1.0, b: 1.0)
  # Drawing the video advances its decode/frame swap internally.
  g.draw(drawable: video, x: 0, y: 0)
  g.present
  Love::Timer.sleep(seconds: 0.016)
end

t = video.tell
puts "  tell after ~#{frames} frames -> #{t.round(3)}s"
video.pause
puts "  playing? (after pause) -> #{video.playing?}"
video.seek(offset: 0.0)
puts "  sought to 0; tell -> #{video.tell.round(3)}s"

puts "drew #{frames} frames of decoded theora video"
