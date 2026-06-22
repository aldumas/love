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

// mruby port of love.video.
//
// The module itself is the real theora decode backend (love::video::theora::Video,
// which spins up a decode worker thread). It is created here and registered at
// M_VIDEO so love.graphics.new_video can build a VideoStream from a file.
//
// This wrapper exposes the Love::VideoStream object type (playback control). The
// graphics-side Love::Video object type (which wraps a VideoStream into a
// Drawable, with the YUV->RGB shader) is registered by the graphics module.
//
// Playback is timer-driven: a TheoraVideoStream owns a DeltaSync by default, so
// a video advances on its own decode worker thread once played -- no audio track
// is required (audio-track playback is not wired up here).

#include "common/config.h"
#include "common/mrb_runtime.h"
#include "common/Module.h"
#include "VideoStream.h"
#include "theora/Video.h"

namespace love
{
namespace video
{

static mrb_value w_vs_play(mrb_state *mrb, mrb_value self)
{
	mrbx_checktype<VideoStream>(mrb, self)->play();
	return mrb_nil_value();
}

static mrb_value w_vs_pause(mrb_state *mrb, mrb_value self)
{
	mrbx_checktype<VideoStream>(mrb, self)->pause();
	return mrb_nil_value();
}

static mrb_value w_vs_seek(mrb_state *mrb, mrb_value self)
{
	mrb_value v[1];
	mrbx_get_kwargs(mrb, {"offset"}, 1, v);
	mrbx_checktype<VideoStream>(mrb, self)->seek(mrbx_checknumber(mrb, v[0]));
	return mrb_nil_value();
}

static mrb_value w_vs_rewind(mrb_state *mrb, mrb_value self)
{
	mrbx_checktype<VideoStream>(mrb, self)->seek(0.0);
	return mrb_nil_value();
}

static mrb_value w_vs_tell(mrb_state *mrb, mrb_value self)
{
	return mrbx_number(mrb, mrbx_checktype<VideoStream>(mrb, self)->tell());
}

static mrb_value w_vs_is_playing(mrb_state *mrb, mrb_value self)
{
	return mrbx_boolean(mrb, mrbx_checktype<VideoStream>(mrb, self)->isPlaying());
}

static mrb_value w_vs_get_filename(mrb_state *mrb, mrb_value self)
{
	return mrbx_string(mrb, mrbx_checktype<VideoStream>(mrb, self)->getFilename());
}

static mrb_value w_vs_get_width(mrb_state *mrb, mrb_value self)
{
	return mrbx_integer(mrb, mrbx_checktype<VideoStream>(mrb, self)->getWidth());
}

static mrb_value w_vs_get_height(mrb_state *mrb, mrb_value self)
{
	return mrbx_integer(mrb, mrbx_checktype<VideoStream>(mrb, self)->getHeight());
}

static mrb_value w_vs_get_dimensions(mrb_state *mrb, mrb_value self)
{
	VideoStream *vs = mrbx_checktype<VideoStream>(mrb, self);
	mrb_value arr = mrb_ary_new_capa(mrb, 2);
	mrb_ary_push(mrb, arr, mrbx_integer(mrb, vs->getWidth()));
	mrb_ary_push(mrb, arr, mrbx_integer(mrb, vs->getHeight()));
	return arr;
}

static const MrbReg videoStreamFunctions[] =
{
	{ "play",           w_vs_play,           MRB_ARGS_NONE() },
	{ "pause",          w_vs_pause,          MRB_ARGS_NONE() },
	{ "seek",           w_vs_seek,           MRB_ARGS_KEY(1, 0) },
	{ "rewind",         w_vs_rewind,         MRB_ARGS_NONE() },
	{ "tell",           w_vs_tell,           MRB_ARGS_NONE() },
	{ "playing?",       w_vs_is_playing,     MRB_ARGS_NONE() },
	{ "get_filename",   w_vs_get_filename,   MRB_ARGS_NONE() },
	{ "get_width",      w_vs_get_width,      MRB_ARGS_NONE() },
	{ "get_height",     w_vs_get_height,     MRB_ARGS_NONE() },
	{ "get_dimensions", w_vs_get_dimensions, MRB_ARGS_NONE() },
	{ nullptr, nullptr, 0 }
};

extern "C" void mrb_love_video_init(mrb_state *mrb)
{
	// Create the real theora decode backend; the Module base ctor registers it
	// at M_VIDEO. graphics.new_video resolves it via Module::getInstance.
	if (Module::getInstance<Video>(Module::M_VIDEO) == nullptr)
		new theora::Video();

	mrbx_register_type(mrb, VideoStream::type, videoStreamFunctions);
}

} // video
} // love
