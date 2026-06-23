# Exercises the #fs-loader slice: the require path accessors,
# Love::Filesystem.load (compile-to-Proc), and the global require backed by the
# love virtual filesystem (Ruby semantics: runs once, returns true/false).
#
# Writes its fixtures into the save dir (a real writable mount), so no external
# files are needed:
#   ./love_mrb_harness loader_test.rb

fs = Love::Filesystem
fs.init(arg0: "love")
fs.set_identity(name: "love_loader_test")

fail = 0
def check(label, got, want)
  ok = got == want
  puts "  #{ok ? 'ok ' : 'FAIL'} #{label}: #{got.inspect}#{ok ? '' : " (expected #{want.inspect})"}"
  ok
end

puts "=== require path defaults to Ruby extensions ==="
fail += 1 unless check("default require path", fs.get_require_path, ["?.rb", "?/init.rb"])

fs.set_require_path(paths: ["?.rb", "lib/?.rb"])
fail += 1 unless check("set_require_path round-trips", fs.get_require_path, ["?.rb", "lib/?.rb"])
fs.set_require_path(paths: ["?.rb", "?/init.rb"])  # restore for the require tests

puts
puts "=== load: compile a chunk and call it ==="
fs.write(name: "chunk.rb", data: "40 + 2")
chunk = fs.load(name: "chunk.rb")
fail += 1 unless check("load returns something callable", chunk.respond_to?(:call), true)
fail += 1 unless check("calling the chunk runs it", chunk.call, 42)
# A chunk's side effects reach the top level.
fs.write(name: "sideeffect.rb", data: "$loader_side_effect = :ran; 7")
fail += 1 unless check("chunk return value", fs.load(name: "sideeffect.rb").call, 7)
fail += 1 unless check("chunk side effect visible", $loader_side_effect, :ran)
# Syntax errors raise.
fs.write(name: "broken.rb", data: "def oops(")
begin
  fs.load(name: "broken.rb")
  puts "  FAIL syntax error did not raise"
  fail += 1
rescue SyntaxError => e   # a ScriptError, not caught by a bare rescue
  puts "  ok  syntax error raised: #{e.class}"
end

puts
puts "=== require: runs a file once, over the virtual filesystem ==="
fs.write(name: "greeter.rb", data: <<~RB)
  $require_run_count = ($require_run_count || 0) + 1
  class Greeter
    def hello; "hi from required file"; end
  end
RB
fail += 1 unless check("first require returns true", require("greeter"), true)
fail += 1 unless check("the required class is defined", Greeter.new.hello, "hi from required file")
fail += 1 unless check("body ran exactly once", $require_run_count, 1)
fail += 1 unless check("second require returns false (load-once)", require("greeter"), false)
fail += 1 unless check("body still ran only once", $require_run_count, 1)
fail += 1 unless check("$LOADED_FEATURES records it", $LOADED_FEATURES.include?("greeter.rb"), true)

# require resolves package-style "?/init.rb" too.
fs.create_directory(name: "pkg")
fs.write(name: "pkg/init.rb", data: "PKG_OK = true")
fail += 1 unless check("require finds pkg/init.rb", require("pkg"), true)
fail += 1 unless check("init.rb ran", PKG_OK, true)

# A missing module raises.
begin
  require("does_not_exist_anywhere")
  puts "  FAIL missing require did not raise"
  fail += 1
rescue => e
  puts "  ok  missing require raised: #{e.class}"
end

# A file that raises propagates and is NOT recorded as loaded (can retry).
fs.write(name: "raiser.rb", data: "raise 'boom from required file'")
begin
  require("raiser")
  puts "  FAIL raising require did not propagate"
  fail += 1
rescue => e
  puts "  ok  raising require propagated: #{e.class}"
end
fail += 1 unless check("failed require not recorded", $LOADED_FEATURES.include?("raiser.rb"), false)

puts
if fail == 0
  puts "ALL PASS"
else
  puts "#{fail} CHECK(S) FAILED"
end
