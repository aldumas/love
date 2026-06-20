/**
 * Copyright (c) 2006-2026 LOVE Development Team
 *
 * This software is provided 'as-is', without any express or implied
 * warranty.  In no event will the authors be held liable for any damages
 * arising from the use of this software.
 *
 * Permission is granted to anyone to use this software for any purpose,
 * including commercial applications, and to alter it and redistribute it
 * freely, subject to the following restrictions:
 *
 * 1. The origin of this software must not be misrepresented; you must not
 *    claim that you wrote the original software. If you use this software
 *    in a product, an acknowledgment in the product documentation would be
 *    appreciated but is not required.
 * 2. Altered source versions must be plainly marked as such, and must not be
 *    misrepresented as being the original software.
 * 3. This notice may not be removed or altered from any source distribution.
 **/

// mruby port of wrap_Sound.cpp + wrap_SoundData.cpp + wrap_Decoder.cpp.
//
// Exposes Love::Sound (the module), the Love::SoundData object type, and the
// Love::Decoder object type. snake_case names, keyword arguments. SoundData
// is-a Data, so it inherits the Data instance methods (get_string, get_size,
// ...) through the runtime's love::Type-mirrored class hierarchy — the data
// module must be initialised first.
//
// The backend is the **real** lullaby Sound: it decodes wav/flac/ogg/mp3/mod
// directly via Wuff, dr_flac, libvorbis, dr_mp3 and libmodplug (no SDL_sound).
//
// new_decoder resolves its `file:` keyword into a love::Stream:
//   - a filename String  -> opened via the filesystem module (the default
//     `stream_source: "file"` streams from disk; "memory" reads the whole file
//     into a DataStream),
//   - a Data object      -> wrapped in a DataStream,
//   - a Stream object     -> used directly.
//
// new_sound_data has three forms, disambiguated by keyword:
//   Love::Sound.new_sound_data(samples:, sample_rate:, bit_depth:, channels:)
//   Love::Sound.new_sound_data(decoder: <Decoder>)   # fully decode a Decoder
//   Love::Sound.new_sound_data(file: <filename/Data/Stream>)  # decode a file

#include "common/config.h"
#include "common/mrb_runtime.h"
#include "common/Object.h"
#include "common/Data.h"
#include "common/Stream.h"

#include "Sound.h"
#include "SoundData.h"
#include "Decoder.h"

// Implementation.
#include "lullaby/Sound.h"

#include "data/DataStream.h"
#include "filesystem/Filesystem.h"
#include "filesystem/File.h"
#include "filesystem/FileData.h"

namespace love
{
namespace sound
{

#define instance() (Module::getInstance<Sound>(Module::M_SOUND))

// --- Decoder instance methods --------------------------------------------

static mrb_value dec_clone(mrb_state *mrb, mrb_value self)
{
	Decoder *t = mrbx_checktype<Decoder>(mrb, self), *c = nullptr;
	mrbx_catchexcept(mrb, [&]() { c = t->clone(); });
	mrb_value r = mrbx_pushtype(mrb, c);
	if (c) c->release();
	return r;
}

static mrb_value dec_getChannelCount(mrb_state *mrb, mrb_value self)
{
	return mrbx_integer(mrb, mrbx_checktype<Decoder>(mrb, self)->getChannelCount());
}

static mrb_value dec_getBitDepth(mrb_state *mrb, mrb_value self)
{
	return mrbx_integer(mrb, mrbx_checktype<Decoder>(mrb, self)->getBitDepth());
}

static mrb_value dec_getSampleRate(mrb_state *mrb, mrb_value self)
{
	return mrbx_integer(mrb, mrbx_checktype<Decoder>(mrb, self)->getSampleRate());
}

static mrb_value dec_getDuration(mrb_state *mrb, mrb_value self)
{
	return mrbx_number(mrb, mrbx_checktype<Decoder>(mrb, self)->getDuration());
}

// Decodes the next chunk and returns it as a fresh SoundData, or nil at EOF.
static mrb_value dec_decode(mrb_state *mrb, mrb_value self)
{
	Decoder *t = mrbx_checktype<Decoder>(mrb, self);

	int decoded = t->decode();
	if (decoded <= 0)
		return mrb_nil_value();

	SoundData *s = nullptr;
	if (mrbx_catchexcept(mrb, [&]() {
		s = instance()->newSoundData(t->getBuffer(),
			decoded / (t->getBitDepth() / 8 * t->getChannelCount()),
			t->getSampleRate(), t->getBitDepth(), t->getChannelCount());
	}))
		return mrb_nil_value();

	mrb_value r = mrbx_pushtype(mrb, s);
	s->release();
	return r;
}

// seek(position:) — a position in seconds. 0 rewinds to the start.
static mrb_value dec_seek(mrb_state *mrb, mrb_value self)
{
	Decoder *t = mrbx_checktype<Decoder>(mrb, self);
	mrb_value v[1];
	mrbx_get_kwargs(mrb, {"position"}, 1, v);

	double offset = mrbx_checknumber(mrb, v[0]);
	if (offset < 0)
		mrb_raise(mrb, E_ARGUMENT_ERROR, "can't seek to a negative position");
	else if (offset == 0)
		t->rewind();
	else
		t->seek(offset);
	return self;
}

static const MrbReg decoderFunctions[] =
{
	{ "clone",             dec_clone,           MRB_ARGS_NONE() },
	{ "get_channel_count", dec_getChannelCount, MRB_ARGS_NONE() },
	{ "get_bit_depth",     dec_getBitDepth,     MRB_ARGS_NONE() },
	{ "get_sample_rate",   dec_getSampleRate,   MRB_ARGS_NONE() },
	{ "get_duration",      dec_getDuration,     MRB_ARGS_NONE() },
	{ "decode",            dec_decode,          MRB_ARGS_NONE() },
	{ "seek",              dec_seek,            MRB_ARGS_KEY(1, 0) },
	{ nullptr, nullptr, 0 }
};

// --- SoundData instance methods ------------------------------------------
// SoundData is-a Data, so it inherits the Data instance methods through the
// runtime's class hierarchy.

static mrb_value sd_clone(mrb_state *mrb, mrb_value self)
{
	SoundData *t = mrbx_checktype<SoundData>(mrb, self), *c = nullptr;
	mrbx_catchexcept(mrb, [&]() { c = t->clone(); });
	mrb_value r = mrbx_pushtype(mrb, c);
	if (c) c->release();
	return r;
}

static mrb_value sd_getChannelCount(mrb_state *mrb, mrb_value self)
{
	return mrbx_integer(mrb, mrbx_checktype<SoundData>(mrb, self)->getChannelCount());
}

static mrb_value sd_getBitDepth(mrb_state *mrb, mrb_value self)
{
	return mrbx_integer(mrb, mrbx_checktype<SoundData>(mrb, self)->getBitDepth());
}

static mrb_value sd_getSampleRate(mrb_state *mrb, mrb_value self)
{
	return mrbx_integer(mrb, mrbx_checktype<SoundData>(mrb, self)->getSampleRate());
}

static mrb_value sd_getSampleCount(mrb_state *mrb, mrb_value self)
{
	return mrbx_integer(mrb, mrbx_checktype<SoundData>(mrb, self)->getSampleCount());
}

static mrb_value sd_getDuration(mrb_state *mrb, mrb_value self)
{
	return mrbx_number(mrb, mrbx_checktype<SoundData>(mrb, self)->getDuration());
}

// get_sample(i:, channel: <opt 1-based>) -> normalized sample value (-1..1)
static mrb_value sd_getSample(mrb_state *mrb, mrb_value self)
{
	SoundData *sd = mrbx_checktype<SoundData>(mrb, self);
	mrb_value v[2];
	mrbx_get_kwargs(mrb, {"i", "channel"}, 1, v);

	int i = mrbx_checkint(mrb, v[0]);
	float sample = 0.0f;
	if (!mrb_undef_p(v[1]))
	{
		int channel = mrbx_checkint(mrb, v[1]);
		if (mrbx_catchexcept(mrb, [&]() { sample = sd->getSample(i, channel); }))
			return mrb_nil_value();
	}
	else if (mrbx_catchexcept(mrb, [&]() { sample = sd->getSample(i); }))
		return mrb_nil_value();

	return mrbx_number(mrb, sample);
}

// set_sample(i:, sample:, channel: <opt 1-based>)
static mrb_value sd_setSample(mrb_state *mrb, mrb_value self)
{
	SoundData *sd = mrbx_checktype<SoundData>(mrb, self);
	mrb_value v[3];
	mrbx_get_kwargs(mrb, {"i", "sample", "channel"}, 2, v);

	int i = mrbx_checkint(mrb, v[0]);
	float sample = (float) mrbx_checknumber(mrb, v[1]);
	if (!mrb_undef_p(v[2]))
	{
		int channel = mrbx_checkint(mrb, v[2]);
		mrbx_catchexcept(mrb, [&]() { sd->setSample(i, channel, sample); });
	}
	else
		mrbx_catchexcept(mrb, [&]() { sd->setSample(i, sample); });
	return self;
}

// copy_from(source:, src_start:, count:, dst_start: <opt 0>)
static mrb_value sd_copyFrom(mrb_state *mrb, mrb_value self)
{
	SoundData *dst = mrbx_checktype<SoundData>(mrb, self);
	mrb_value v[4];
	mrbx_get_kwargs(mrb, {"source", "src_start", "count", "dst_start"}, 3, v);

	const SoundData *src = mrbx_checktype<SoundData>(mrb, v[0]);
	int srcStart = mrbx_checkint(mrb, v[1]);
	int count = mrbx_checkint(mrb, v[2]);
	int dstStart = mrbx_optint(mrb, v[3], 0);

	mrbx_catchexcept(mrb, [&]() { dst->copyFrom(src, srcStart, count, dstStart); });
	return self;
}

// slice(start:, length: <opt -1, meaning to the end>) -> a new SoundData
static mrb_value sd_slice(mrb_state *mrb, mrb_value self)
{
	SoundData *t = mrbx_checktype<SoundData>(mrb, self), *c = nullptr;
	mrb_value v[2];
	mrbx_get_kwargs(mrb, {"start", "length"}, 1, v);

	int start = mrbx_checkint(mrb, v[0]);
	int length = mrbx_optint(mrb, v[1], -1);

	if (mrbx_catchexcept(mrb, [&]() { c = t->slice(start, length); }))
		return mrb_nil_value();
	mrb_value r = mrbx_pushtype(mrb, c);
	if (c) c->release();
	return r;
}

static const MrbReg soundDataFunctions[] =
{
	{ "clone",             sd_clone,           MRB_ARGS_NONE() },
	{ "get_channel_count", sd_getChannelCount, MRB_ARGS_NONE() },
	{ "get_bit_depth",     sd_getBitDepth,     MRB_ARGS_NONE() },
	{ "get_sample_rate",   sd_getSampleRate,   MRB_ARGS_NONE() },
	{ "get_sample_count",  sd_getSampleCount,  MRB_ARGS_NONE() },
	{ "get_duration",      sd_getDuration,     MRB_ARGS_NONE() },
	{ "get_sample",        sd_getSample,       MRB_ARGS_KEY(2, 0) },
	{ "set_sample",        sd_setSample,       MRB_ARGS_KEY(3, 0) },
	{ "copy_from",         sd_copyFrom,        MRB_ARGS_KEY(4, 0) },
	{ "slice",             sd_slice,           MRB_ARGS_KEY(2, 0) },
	{ nullptr, nullptr, 0 }
};

// --- Module-level functions ----------------------------------------------

// Resolve a `file:` keyword (a Stream, a Data, or a filename String) into a
// retained love::Stream the Decoder can read from. The caller releases. Returns
// nullptr with an mruby exception pending on failure.
static Stream *resolveStream(mrb_state *mrb, mrb_value file, Decoder::StreamSource source)
{
	if (mrbx_istype<Stream>(mrb, file))
	{
		Stream *s = mrbx_checktype<Stream>(mrb, file);
		s->retain();
		return s;
	}

	if (mrbx_istype<Data>(mrb, file))
	{
		Data *d = mrbx_checktype<Data>(mrb, file);
		Stream *s = nullptr;
		mrbx_catchexcept(mrb, [&]() { s = new love::data::DataStream(d); });
		return s;
	}

	std::string filename = mrbx_checkstring(mrb, file);
	auto fs = Module::getInstance<love::filesystem::Filesystem>(Module::M_FILESYSTEM);
	if (fs == nullptr)
	{
		mrb_raise(mrb, E_RUNTIME_ERROR, "The filesystem module must be loaded to open a sound file by name.");
		return nullptr;
	}

	Stream *s = nullptr;
	if (source == Decoder::STREAM_FILE)
	{
		mrbx_catchexcept(mrb, [&]() {
			s = fs->openFile(filename.c_str(), love::filesystem::File::MODE_READ);
		});
	}
	else
	{
		mrbx_catchexcept(mrb, [&]() {
			StrongRef<love::filesystem::FileData> data(fs->read(filename.c_str()), Acquire::NORETAIN);
			s = new love::data::DataStream(data);
		});
	}
	return s;
}

// Builds a Decoder from a `file:` keyword (+ optional buffer_size/stream_source).
// On success returns a retained Decoder (caller releases); on failure leaves an
// mruby exception pending and returns nullptr.
static Decoder *makeDecoder(mrb_state *mrb, mrb_value file, mrb_value bufsize, mrb_value streamsrc)
{
	int bufferSize = mrbx_optint(mrb, bufsize, Decoder::DEFAULT_BUFFER_SIZE);

	Decoder::StreamSource source = Decoder::STREAM_FILE;
	if (!mrb_undef_p(streamsrc))
	{
		std::string srcstr = mrbx_checkstring(mrb, streamsrc);
		if (!Decoder::getConstant(srcstr.c_str(), source))
		{
			mrb_raisef(mrb, E_ARGUMENT_ERROR, "invalid stream type: %s", srcstr.c_str());
			return nullptr;
		}
	}

	Stream *stream = resolveStream(mrb, file, source);
	if (stream == nullptr)
		return nullptr;

	Decoder *d = nullptr;
	bool err = mrbx_catchexcept(mrb, [&]() { d = instance()->newDecoder(stream, bufferSize); });
	stream->release();
	if (err)
		return nullptr;
	return d;
}

static mrb_value w_newDecoder(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[3];
	mrbx_get_kwargs(mrb, {"file", "buffer_size", "stream_source"}, 1, v);

	Decoder *d = makeDecoder(mrb, v[0], v[1], v[2]);
	if (d == nullptr)
		return mrb_nil_value();

	mrb_value r = mrbx_pushtype(mrb, d);
	d->release();
	return r;
}

static mrb_value w_newSoundData(mrb_state *mrb, mrb_value self)
{
	(void) self;
	// samples form: samples/sample_rate/bit_depth/channels.
	// decoder form: decoder:.  file form: file: (+ buffer_size/stream_source).
	mrb_value v[7];
	mrbx_get_kwargs(mrb,
		{"samples", "sample_rate", "bit_depth", "channels", "decoder", "file", "stream_source"},
		0, v);

	SoundData *t = nullptr;

	if (!mrb_undef_p(v[0])) // samples form: an empty buffer of the given format
	{
		int samples = mrbx_checkint(mrb, v[0]);
		int sampleRate = mrbx_optint(mrb, v[1], Decoder::DEFAULT_SAMPLE_RATE);
		int bitDepth = mrbx_optint(mrb, v[2], Decoder::DEFAULT_BIT_DEPTH);
		int channels = mrbx_optint(mrb, v[3], Decoder::DEFAULT_CHANNELS);

		if (mrbx_catchexcept(mrb, [&]() { t = instance()->newSoundData(samples, sampleRate, bitDepth, channels); }))
			return mrb_nil_value();
	}
	else if (!mrb_undef_p(v[4])) // decoder form: fully decode an existing Decoder
	{
		Decoder *decoder = mrbx_checktype<Decoder>(mrb, v[4]);
		if (mrbx_catchexcept(mrb, [&]() { t = instance()->newSoundData(decoder); }))
			return mrb_nil_value();
	}
	else if (!mrb_undef_p(v[5])) // file form: build a decoder, then fully decode it
	{
		Decoder *decoder = makeDecoder(mrb, v[5], mrb_undef_value() /* default buffer */, v[6]);
		if (decoder == nullptr)
			return mrb_nil_value();
		bool err = mrbx_catchexcept(mrb, [&]() { t = instance()->newSoundData(decoder); });
		decoder->release();
		if (err)
			return mrb_nil_value();
	}
	else
	{
		mrb_raise(mrb, E_ARGUMENT_ERROR, "expected `samples:` (create), `decoder:`, or `file:` (decode)");
		return mrb_nil_value();
	}

	mrb_value r = mrbx_pushtype(mrb, t);
	t->release();
	return r;
}

static const MrbReg functions[] =
{
	{ "new_decoder",    w_newDecoder,    MRB_ARGS_KEY(3, 0) },
	{ "new_sound_data", w_newSoundData,  MRB_ARGS_KEY(7, 0) },
	{ nullptr, nullptr, 0 }
};

// Equivalent of luaopen_love_sound: creates the module instance (the real
// lullaby backend), registers the Love::Sound module plus the Love::SoundData
// and Love::Decoder types. SoundData inherits the Data instance methods via the
// class hierarchy, so the data module must be initialised first.
extern "C" void mrb_love_sound_init(mrb_state *mrb)
{
	Sound *inst = instance();
	if (inst == nullptr)
		inst = new love::sound::lullaby::Sound();
	else
		inst->retain();

	WrappedModule w;
	w.module = inst;
	w.name = "Sound";
	w.type = &Sound::type;
	w.functions = functions;

	mrbx_register_module(mrb, w);
	mrbx_register_type(mrb, SoundData::type, soundDataFunctions);
	mrbx_register_type(mrb, Decoder::type, decoderFunctions);
}

} // sound
} // love
