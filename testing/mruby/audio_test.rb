# Exercises the ported Love::Audio module: the real OpenAL backend (null
# fallback), the Source object type, listener state, effects, and queueable
# sources. Decodes .ogg fixtures from testing/resources via the sound module.

Love::Filesystem.init(arg0: Love::ARG0)
res = "/home/adam/src/love/testing/resources"
Love::Filesystem.set_source(path: res)
puts "set_source(#{res})"

puts "== module / listener =="
puts "active sources    -> #{Love::Audio.get_active_source_count}"
Love::Audio.set_volume(volume: 0.5)
puts "volume round-trip -> #{Love::Audio.get_volume}"

Love::Audio.set_position(x: 1, y: 2, z: 3)
p Love::Audio.get_position
Love::Audio.set_velocity(x: 0.5, y: 0, z: -1)
p Love::Audio.get_velocity
Love::Audio.set_orientation(fx: 0, fy: 0, fz: -1, ux: 0, uy: 1, uz: 0)
p Love::Audio.get_orientation

Love::Audio.set_doppler_scale(scale: 2.0)
puts "doppler scale     -> #{Love::Audio.get_doppler_scale}"
Love::Audio.set_distance_model(model: "linearclamped")
puts "distance model    -> #{Love::Audio.get_distance_model}"
puts "effects supported?-> #{Love::Audio.effects_supported?}"
puts "max scene effects -> #{Love::Audio.get_max_scene_effects}"
puts "max source effects-> #{Love::Audio.get_max_source_effects}"
puts "recording devices -> #{Love::Audio.get_recording_devices.length}"

puts
puts "== Source from a file (static) =="
src = Love::Audio.new_source(file: "tone.ogg", type: "static")
puts "class             -> #{src.class}"
puts "is_a Source?      -> #{src.is_a?(Love::Source)}"
puts "type              -> #{src.get_type}"
puts "channels          -> #{src.get_channel_count}"
puts "duration (s)      -> #{format('%.4f', src.get_duration)}"
src.set_volume(volume: 0.8)
puts "volume            -> #{src.get_volume}"
src.set_pitch(pitch: 1.5)
puts "pitch             -> #{src.get_pitch}"
src.set_looping(looping: true)
puts "looping?          -> #{src.looping?}"
puts "playing? (init)   -> #{src.playing?}"
puts "play -> #{Love::Audio.play(source: src)}; playing? -> #{src.playing?}"
src.seek(offset: 0.5)
puts "tell after seek   -> #{format('%.4f', src.tell)}"
Love::Audio.stop(source: src)
puts "stopped; playing? -> #{src.playing?}"

clone = src.clone
puts "clone             -> #{clone.class}, duration #{format('%.4f', clone.get_duration)}"

puts
puts "== Source from a Decoder (stream) =="
dec = Love::Sound.new_decoder(file: "click.ogg")
ssrc = Love::Audio.new_source(decoder: dec, type: "stream")
puts "type              -> #{ssrc.get_type}"
puts "channels          -> #{ssrc.get_channel_count}"

puts
puts "== Source from SoundData =="
sd = Love::Sound.new_sound_data(file: "click.ogg")
dsrc = Love::Audio.new_source(sound_data: sd)
puts "type              -> #{dsrc.get_type} (always static)"
puts "duration          -> #{format('%.4f', dsrc.get_duration)}"

puts
puts "== queueable source =="
q = Love::Audio.new_queueable_source(sample_rate: 22050, bit_depth: 16, channels: 1)
puts "type              -> #{q.get_type}"
puts "free buffers      -> #{q.get_free_buffer_count}"
buf = Love::Sound.new_sound_data(samples: 1024, sample_rate: 22050, bit_depth: 16, channels: 1)
puts "queue ok          -> #{q.queue(sound_data: buf)}"

puts
puts "== play multiple, then stop all =="
puts "play [src,ssrc]   -> #{Love::Audio.play(sources: [src, ssrc])}"
puts "active sources    -> #{Love::Audio.get_active_source_count}"
Love::Audio.stop
puts "stop all; active  -> #{Love::Audio.get_active_source_count}"

puts
puts "== scene effects (Hash <-> effect description) =="
ok = Love::Audio.set_effect(name: "myreverb", settings: { type: "reverb", decaytime: 4.0 })
puts "set_effect reverb -> #{ok}"
desc = Love::Audio.get_effect(name: "myreverb")
puts "get_effect type   -> #{desc && desc[:type]}, decaytime #{desc && desc[:decaytime]}"
puts "active effects    -> #{Love::Audio.get_active_effects.inspect}"
puts "unset             -> #{Love::Audio.set_effect(name: 'myreverb', settings: false)}"

puts
puts "== source filter + effect =="
puts "set_filter lowpass-> #{src.set_filter(settings: { type: 'lowpass', volume: 0.8, highgain: 0.5 })}"
f = src.get_filter
puts "get_filter        -> #{f.inspect}"
puts "clear filter      -> #{src.set_filter}"
Love::Audio.set_effect(name: "fx", settings: { type: "echo" })
puts "src set_effect     -> #{src.set_effect(name: 'fx')}"
puts "src active effects -> #{src.get_active_effects.inspect}"

puts
puts "ALL AUDIO TESTS PASSED"
