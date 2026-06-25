# Exercises the filesystem mounting + symlink slice (#fs-deferred -> done parts):
#   mount_full_path / unmount_full_path, mount_common_path / get_full_common_path,
#   Data/FileData-backed mount + unmount, and set/symlinks_enabled?.
#
# Self-contained: it provisions its own fixtures, so it needs nothing pre-existing
# on disk. The full-path mount writes a file into a subdir of the save area (a real
# OS path it then mounts), and the Data mount decodes an embedded base64 zip. Run:
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
puts "=== platform settings: fused + android save external ==="
# setFused is a one-shot latch in the physfs backend (the boot pipeline sets it
# exactly once), so only the first call takes effect — and the save dir was
# already mounted by set_identity above, so latching it now is harmless here.
fail += 1 unless check("fused? false initially", fs.fused?, false)
fs.set_fused(fused: true)
fail += 1 unless check("fused? true after set", fs.fused?, true)
fs.set_fused(fused: false)   # latched: ignored, stays true
fail += 1 unless check("fused? latched (still true)", fs.fused?, true)
# Android save-external is a no-op off Android, so the getter stays false here,
# but the setter must accept the kwarg (and default to false) without raising.
fs.set_android_save_external(external: false)
fail += 1 unless check("android_save_external? queryable", fs.android_save_external?, false)
fs.set_android_save_external   # default external: false

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
# Provision a real OS directory to mount: make a fresh subdir of the save area and
# write a file into it, then resolve that subdir's absolute path. It's distinct from
# the auto-mounted appsavedir itself, so mount_full_path accepts it.
fs.create_directory(name: "mountsrc")
fs.write(name: "mountsrc/hello.txt", data: "from full path mount")
srcdir = "#{fs.get_full_common_path(common_path: "appsavedir")}/mountsrc"
ok = fs.mount_full_path(archive: srcdir, mountpoint: "ext", permissions: "read")
fail += 1 unless check("mount_full_path returns true", ok, true)
fail += 1 unless check("read mounted file", fs.read(name: "ext/hello.txt"), "from full path mount")
fail += 1 unless check("get_info sees it", fs.get_info(path: "ext/hello.txt")[:type], "file")
fail += 1 unless check("unmount_full_path", fs.unmount_full_path(archive: srcdir), true)
fail += 1 unless check("gone after unmount", fs.get_info(path: "ext/hello.txt"), nil)

puts
puts "=== Data-based mounting: mount a zip held in a FileData ==="
# An embedded zip (one entry inzip.txt -> "bytes from a mounted zip"), base64-decoded
# to bytes and mounted as a FileData archive (exercises the love::Data* mount
# overload) -- no on-disk archive needed.
zip_b64 = "UEsDBAoAAAAAANJ+2Vx2YKqDGAAAABgAAAAJAAAAaW56aXAudHh0Ynl0ZXMgZnJvbSBhIG1vdW50ZWQgemlwUEsBAh4DCgAAAAAA0n7ZXHZgqoMYAAAAGAAAAAkAAAAAAAAAAQAAALSBAAAAAGluemlwLnR4dFBLBQYAAAAAAQABADcAAAA/AAAAAAA="
zipbytes = Love::Data.decode(format: "base64", string: zip_b64, container: "string")
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
