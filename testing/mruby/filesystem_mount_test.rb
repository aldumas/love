# Exercises the filesystem mounting + symlink slice (#fs-deferred -> done parts):
#   mount_full_path / unmount_full_path, mount_common_path / get_full_common_path,
#   Data/FileData-backed mount + unmount, and set/symlinks_enabled?.
#
# Requires two fixtures under /tmp (a plain dir + a zip archive). Create them:
#   mkdir -p /tmp/lovefs_test && printf 'from full path mount' > /tmp/lovefs_test/hello.txt
#   d=$(mktemp -d); printf 'bytes from a mounted zip' > "$d/inzip.txt"
#   (cd "$d" && zip -q /tmp/lovefs_test/test.zip inzip.txt); rm -rf "$d"
#
#   ./love_mrb_harness filesystem_mount_test.rb

fs = Love::Filesystem
fs.init(arg0: "love")
fs.set_identity(name: "love_fs_mount_test")

fail = 0
def check(label, got, want)
  ok = got == want
  puts "  #{ok ? 'ok ' : 'FAIL'} #{label}: #{got.inspect}#{ok ? '' : " (expected #{want.inspect})"}"
  ok
end

puts "=== symlinks toggle ==="
fs.set_symlinks_enabled(enable: false)
fail += 1 unless check("disabled", fs.symlinks_enabled?, false)
fs.set_symlinks_enabled(enable: true)
fail += 1 unless check("enabled", fs.symlinks_enabled?, true)

puts
puts "=== get_full_common_path ==="
%w[appsavedir userhome userappdata].each do |cp|
  path = fs.get_full_common_path(common_path: cp)
  ok = path.is_a?(String) && !path.empty?
  puts "  #{ok ? 'ok ' : 'FAIL'} #{cp} -> #{path.inspect}"
  fail += 1 unless ok
end
begin
  fs.get_full_common_path(common_path: "not-a-path")
  puts "  FAIL bad common path did not raise"
  fail += 1
rescue => e
  puts "  ok  bad common path raised: #{e.class}"
end

puts
puts "=== mount_full_path: mount a real OS directory read-only ==="
ok = fs.mount_full_path(archive: "/tmp/lovefs_test", mountpoint: "ext", permissions: "read")
fail += 1 unless check("mount_full_path returns true", ok, true)
fail += 1 unless check("read mounted file", fs.read(name: "ext/hello.txt"), "from full path mount")
fail += 1 unless check("get_info sees it", fs.get_info(path: "ext/hello.txt")[:type], "file")
fail += 1 unless check("unmount_full_path", fs.unmount_full_path(archive: "/tmp/lovefs_test"), true)
fail += 1 unless check("gone after unmount", fs.get_info(path: "ext/hello.txt"), nil)

puts
puts "=== Data-based mounting: mount a zip held in a FileData ==="
# Bring the zip's bytes in via a brief full-path mount, then mount them as a
# FileData archive (exercises the love::Data* mount overload).
fs.mount_full_path(archive: "/tmp/lovefs_test", mountpoint: "ext", permissions: "read")
zipbytes = fs.read(name: "ext/test.zip")
fs.unmount_full_path(archive: "/tmp/lovefs_test")
fd = fs.new_file_data(contents: zipbytes, name: "test.zip")
fail += 1 unless check("FileData built", !fd.nil?, true)
fail += 1 unless check("mount FileData archive", fs.mount(data: fd, mountpoint: "z"), true)
fail += 1 unless check("read from zip archive", fs.read(name: "z/inzip.txt"), "bytes from a mounted zip")
fail += 1 unless check("unmount Data", fs.unmount(data: fd), true)
fail += 1 unless check("zip gone after unmount", fs.get_info(path: "z/inzip.txt"), nil)

puts
puts "=== mount_common_path: mount userappdata into the tree ==="
# The save dir (appsavedir) is already auto-mounted by set_identity, so mounting
# it again fails by design (a real path can't be double-mounted). Mount the
# enclosing userappdata instead, then reach the probe through it. The relative
# path is derived from the two common paths so no save-layout is hard-coded.
fs.write(name: "probe.txt", data: "save-area probe")          # writes into the save dir
savefull  = fs.get_full_common_path(common_path: "appsavedir")
appdata   = fs.get_full_common_path(common_path: "userappdata")
rel       = savefull[(appdata.length + 1)..-1]                # e.g. "love/love_fs_mount_test"
ok = fs.mount_common_path(common_path: "userappdata", mountpoint: "appdata", permissions: "read")
fail += 1 unless check("mount_common_path returns true", ok, true)
fail += 1 unless check("read through common-path mount", fs.read(name: "appdata/#{rel}/probe.txt"), "save-area probe")
fail += 1 unless check("unmount_common_path", fs.unmount_common_path(common_path: "userappdata"), true)

puts
if fail == 0
  puts "ALL PASS"
else
  puts "#{fail} CHECK(S) FAILED"
  exit 1
end
