# Exercises the ported Love::Sound module: real lullaby decode backend, the
# Decoder and SoundData object types, and SoundData's Data inheritance.

Love::Filesystem.init(arg0: Love::ARG0)
# Point the game source at the repo's testing/resources dir so we can decode
# its .ogg fixtures (set_source mounts an absolute OS dir as the search root).
res = "/home/adam/src/love/testing/resources"
Love::Filesystem.set_source(path: res)
puts "set_source(#{res})"

puts "== Decoder =="
dec = Love::Sound.new_decoder(file: "tone.ogg")
puts "class            -> #{dec.class}"
puts "is_a Decoder?    -> #{dec.is_a?(Love::Decoder)}"
puts "channel_count    -> #{dec.get_channel_count}"
puts "bit_depth        -> #{dec.get_bit_depth}"
puts "sample_rate      -> #{dec.get_sample_rate}"
puts "duration         -> #{format('%.4f', dec.get_duration)}"

chunk = dec.decode
puts "decode chunk     -> #{chunk.class} (#{chunk.get_sample_count} samples)"
dec.seek(position: 0)   # rewind
puts "seek(0) rewound, decode again -> #{dec.decode.class}"

clone = dec.clone
puts "clone            -> #{clone.class}, sample_rate #{clone.get_sample_rate}"

puts
puts "== SoundData from a file =="
sd = Love::Sound.new_sound_data(file: "tone.ogg")
puts "class            -> #{sd.class}"
puts "is_a Data?       -> #{sd.is_a?(Love::Data)}"
puts "channels         -> #{sd.get_channel_count}"
puts "bit_depth        -> #{sd.get_bit_depth}"
puts "sample_rate      -> #{sd.get_sample_rate}"
puts "sample_count     -> #{sd.get_sample_count}"
puts "duration         -> #{format('%.4f', sd.get_duration)}"
puts "size (Data)      -> #{sd.get_size}"     # inherited Data method
puts "sample[100]      -> #{format('%.5f', sd.get_sample(i: 100))}"

puts
puts "== SoundData (empty, generated) =="
gen = Love::Sound.new_sound_data(samples: 4, sample_rate: 22050, bit_depth: 16, channels: 1)
puts "samples #{gen.get_sample_count}, rate #{gen.get_sample_rate}, depth #{gen.get_bit_depth}, ch #{gen.get_channel_count}"
gen.set_sample(i: 0, sample: 0.5)
gen.set_sample(i: 1, sample: -0.5)
puts "set then get [0]  -> #{format('%.5f', gen.get_sample(i: 0))}"
puts "set then get [1]  -> #{format('%.5f', gen.get_sample(i: 1))}"

puts
puts "== slice / copy_from / clone =="
sl = sd.slice(start: 0, length: 1000)
puts "slice(0,1000)    -> #{sl.get_sample_count} samples"
sl.copy_from(source: sd, src_start: 0, count: 500, dst_start: 0)
puts "copy_from ok"
cl = sd.clone
puts "clone            -> #{cl.get_sample_count} samples, is_a Data? #{cl.is_a?(Love::Data)}"

puts
puts "== SoundData from a Decoder =="
dec2 = Love::Sound.new_decoder(file: "click.ogg")
sd2 = Love::Sound.new_sound_data(decoder: dec2)
puts "decoded click.ogg -> #{sd2.get_sample_count} samples, #{format('%.4f', sd2.get_duration)}s"

puts
puts "ALL SOUND TESTS PASSED"
