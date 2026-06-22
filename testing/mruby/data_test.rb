# Exercises love.data's binary pack/unpack slice (#data-pack): the native
# reimplementation of Lua 5.3's string.pack / string.unpack / getPackedSize.
#
#   ./love_mrb_harness data_test.rb

d = Love::Data

fail = 0
def check(label, got, want)
  ok = got == want
  puts "  #{ok ? 'ok ' : 'FAIL'} #{label}: #{got.inspect}#{ok ? '' : " (expected #{want.inspect})"}"
  ok
end

# Tiny helper: turn a packed String into a hex dump for byte-level assertions.
def hex(s)
  s.bytes.map { |b| "%02x" % b }.join
end

puts "=== get_packed_size ==="
fail += 1 unless check("'<i4 i4'", d.get_packed_size(format: "<i4i4"), 8)
fail += 1 unless check("'>i2 i8'", d.get_packed_size(format: ">i2i8"), 10)
fail += 1 unless check("'<f d'",   d.get_packed_size(format: "<fd"),   12)
# Alignment: 'c1' then aligned 'i4' under maxalign 4 (!4) inserts 3 pad bytes.
fail += 1 unless check("'!4 c1 i4'", d.get_packed_size(format: "!4c1i4"), 8)

puts
puts "=== integer pack: explicit endianness & width ==="
fail += 1 unless check("<i4 0x01020304", hex(d.pack(format: "<i4", values: [0x01020304])), "04030201")
fail += 1 unless check(">i4 0x01020304", hex(d.pack(format: ">i4", values: [0x01020304])), "01020304")
fail += 1 unless check("<i2 -1",         hex(d.pack(format: "<i2", values: [-1])),         "ffff")
fail += 1 unless check(">I8 1",          hex(d.pack(format: ">I8", values: [1])),          "0000000000000001")

puts
puts "=== round-trip: mixed integers, float, double, fixed string ==="
fmt = "<i4 I2 f d c3"
packed = d.pack(format: fmt, values: [-12345, 60000, 1.5, 3.5, "hey"])
res = d.unpack(format: fmt, string: packed)
fail += 1 unless check("values", res[:values], [-12345, 60000, 1.5, 3.5, "hey"])
# offset is 1-based, one past the consumed bytes (== packed size + 1).
fail += 1 unless check("offset", res[:offset], d.get_packed_size(format: fmt) + 1)

puts
puts "=== length-prefixed string (s) and zero-terminated string (z) ==="
ps = d.pack(format: ">s2", values: ["LÖVE"])              # 2-byte big-endian length prefix
ru = d.unpack(format: ">s2", string: ps)
fail += 1 unless check("s2 round-trip", ru[:values], ["LÖVE"])
pz = d.pack(format: "z", values: ["hi"])
fail += 1 unless check("z packs NUL terminator", hex(pz), "6869" + "00")
rz = d.unpack(format: "z", string: pz)
fail += 1 unless check("z round-trip", rz[:values], ["hi"])

puts
puts "=== sequential unpack via offset chaining ==="
buf = d.pack(format: "<i4i4i4", values: [10, 20, 30])
a = d.unpack(format: "<i4", string: buf, offset: 1)
b = d.unpack(format: "<i4", string: buf, offset: a[:offset])
c = d.unpack(format: "<i4", string: buf, offset: b[:offset])
fail += 1 unless check("chained ints", [a[:values][0], b[:values][0], c[:values][0]], [10, 20, 30])

puts
puts "=== pack into an existing ByteData at an offset ==="
bd = d.new_byte_data(size: 16)
d.pack(format: "<i4", values: [0x12345678], data: bd, offset: 4)
# Read those 4 bytes back out of the ByteData via unpack on its bytes.
back = d.unpack(format: "<i4", data: bd, offset: 5)   # 1-based: byte 5 == offset 4
fail += 1 unless check("byte-data offset pack", back[:values][0], 0x12345678)

puts
puts "=== container: 'data' yields a ByteData ==="
obj = d.pack(format: "<i2", values: [7], container: "data")
fail += 1 unless check("is a Data", obj.is_a?(Love::ByteData), true)
fail += 1 unless check("data bytes", hex(obj.get_string), "0700")

puts
puts "=== error cases ==="
begin
  d.get_packed_size(format: "z")          # variable-length -> error
  puts "  FAIL z get_packed_size did not raise"
  fail += 1
rescue => e
  puts "  ok  z get_packed_size raised: #{e.class}: #{e.message}"
end
begin
  d.pack(format: "<i1", values: [9999])   # 9999 doesn't fit in 1 signed byte
  puts "  FAIL overflow did not raise"
  fail += 1
rescue => e
  puts "  ok  overflow raised: #{e.class}: #{e.message}"
end

puts
puts "=== get_pointer / get_ffi_pointer (#data-ffi-atomic) ==="
pd = d.new_byte_data(size: 8)
fail += 1 unless check("get_pointer non-nil", !pd.get_pointer.nil?, true)
fail += 1 unless check("get_ffi_pointer nil (no FFI)", pd.get_ffi_pointer, nil)

puts
puts "=== perform_atomic (#data-ffi-atomic) ==="
ad = d.new_byte_data(size: 4)
# Block runs under the Data's mutex; yields the Data and propagates its result.
ret = ad.perform_atomic do |x|
  x.set_string(string: "ABCD")
  x.get_size
end
fail += 1 unless check("block return propagates", ret, 4)
fail += 1 unless check("mutation under lock took effect", ad.get_string, "ABCD")
# No block -> ArgumentError.
begin
  ad.perform_atomic
  puts "  FAIL perform_atomic without block did not raise"
  fail += 1
rescue => e
  puts "  ok  no-block raised: #{e.class}: #{e.message}"
end
# A raising block propagates the error AND releases the mutex (so the next
# perform_atomic still works rather than deadlocking).
begin
  ad.perform_atomic { raise "boom" }
  puts "  FAIL raising block did not propagate"
  fail += 1
rescue => e
  puts "  ok  raising block propagated: #{e.class}: #{e.message}"
end
again = ad.perform_atomic { 42 }
fail += 1 unless check("mutex released after raise", again, 42)

puts
if fail == 0
  puts "ALL PASS"
else
  puts "#{fail} CHECK(S) FAILED"
  exit 1
end
