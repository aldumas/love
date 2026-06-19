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

// mruby port of wrap_Window.cpp.
//
// API shape: a Love::Window module, snake_case names, keyword arguments. Where
// the Lua API packed window options into a settings table, the mruby API
// flattens them into keyword arguments:
//
//   love.window.setMode(800, 600, {fullscreen=true, resizable=true})
//     -> Love::Window.set_mode(width: 800, height: 600, fullscreen: true, resizable: true)
//
//   local w, h, flags = love.window.getMode()
//     -> mode = Love::Window.get_mode   # a Hash {width:, height:, fullscreen:, ...}
//
// Backend note: like the event module, this provides a LEAN backend
// (HarnessWindow) rather than linking love.window.sdl. The real sdl::Window
// (window/sdl/Window.cpp) creates an OpenGL/Metal/Vulkan context and drives the
// love.graphics backbuffer -- it pulls in the entire graphics subsystem. Until
// graphics is ported, HarnessWindow creates a real SDL window (no renderer
// context) so an actual window appears on screen and real keyboard/mouse events
// flow through the (also lean) HarnessEvent pump. Graphics-dependent methods
// (setGraphics, the renderer context) are no-ops/stubs. Swap this for
// window/sdl/Window.cpp once graphics lands -- the Ruby-facing API is unchanged.

#include "common/config.h"
#include "common/mrb_runtime.h"
#include "common/pixelformat.h"
#include "Window.h"
#include "image/ImageData.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

namespace love
{
namespace window
{

#define instance() (Module::getInstance<Window>(Module::M_WINDOW))

// The base Window.cpp (setHighDPIAllowed) defers the actual toggle to the
// backend. HarnessWindow ignores HiDPI, so this is a no-op.
void setHighDPIAllowedImplementation(bool /*enable*/)
{
}

// =========================================================================
// Lean, graphics-independent SDL window backend
// =========================================================================

// TODO(mruby) #win-backend: lean backend — real SDL window, no renderer/
// graphics context; swap for window/sdl/Window.cpp (see PORTING.md §B)
class HarnessWindow : public love::window::Window
{
public:

	HarnessWindow()
		: love::window::Window("love.window.harness")
	{
		if (!SDL_InitSubSystem(SDL_INIT_VIDEO))
			throw love::Exception("Could not initialize SDL video subsystem (%s)", SDL_GetError());
	}

	~HarnessWindow() override
	{
		close();
		SDL_QuitSubSystem(SDL_INIT_VIDEO);
	}

	// --- graphics seam (no-ops without a renderer) -----------------------

	void setGraphics(graphics::Graphics * /*g*/) override {}

	// --- create / query / destroy ----------------------------------------

	bool setWindow(int width, int height, WindowSettings *s) override
	{
		WindowSettings defaults;
		if (s == nullptr)
			s = &defaults;

		// Always request an OpenGL-capable window so the (lean) graphics module
		// can create a GL context on it. Harmless when graphics is unused.
		Uint32 flags = SDL_WINDOW_OPENGL;
		if (s->resizable)  flags |= SDL_WINDOW_RESIZABLE;
		if (s->borderless) flags |= SDL_WINDOW_BORDERLESS;
		if (s->fullscreen) flags |= SDL_WINDOW_FULLSCREEN;

		if (window == nullptr)
		{
			window = SDL_CreateWindow(title.c_str(), width, height, flags);
			if (window == nullptr)
				throw love::Exception("Could not create window (%s)", SDL_GetError());
		}
		else
		{
			SDL_SetWindowSize(window, width, height);
			SDL_SetWindowResizable(window, s->resizable);
			SDL_SetWindowBordered(window, !s->borderless);
			SDL_SetWindowFullscreen(window, s->fullscreen);
		}

		// Position: explicit coordinates, otherwise centered on the display.
		SDL_DisplayID disp = displayForIndex(s->displayindex);
		if (s->useposition)
			SDL_SetWindowPosition(window, s->x, s->y);
		else if (s->centered)
			SDL_SetWindowPosition(window,
				SDL_WINDOWPOS_CENTERED_DISPLAY(disp),
				SDL_WINDOWPOS_CENTERED_DISPLAY(disp));

		settings = *s;
		open = true;
		refreshDimensions();
		return true;
	}

	void getWindow(int &width, int &height, WindowSettings &s) override
	{
		refreshDimensions();
		width = windowWidth;
		height = windowHeight;
		s = settings;
	}

	void close() override
	{
		if (window != nullptr)
		{
			SDL_DestroyWindow(window);
			window = nullptr;
		}
		open = false;
	}

	bool onSizeChanged(int width, int height) override
	{
		windowWidth = width;
		windowHeight = height;
		if (window != nullptr && !SDL_GetWindowSizeInPixels(window, &pixelWidth, &pixelHeight))
		{
			pixelWidth = width;
			pixelHeight = height;
		}
		return true;
	}

	bool isOpen() const override { return open; }

	// --- fullscreen -------------------------------------------------------

	bool setFullscreen(bool fullscreen, FullscreenType fstype) override
	{
		if (window == nullptr)
			return false;
		settings.fullscreen = fullscreen;
		settings.fstype = fstype;
		bool ok = SDL_SetWindowFullscreen(window, fullscreen);
		refreshDimensions();
		return ok;
	}

	bool setFullscreen(bool fullscreen) override
	{
		return setFullscreen(fullscreen, settings.fstype);
	}

	// --- displays ---------------------------------------------------------

	int getDisplayCount() const override
	{
		int count = 0;
		SDL_DisplayID *ids = SDL_GetDisplays(&count);
		SDL_free(ids);
		return count;
	}

	const char *getDisplayName(int displayindex) const override
	{
		const char *name = SDL_GetDisplayName(displayForIndex(displayindex));
		return name != nullptr ? name : "";
	}

	DisplayOrientation getDisplayOrientation(int displayindex) const override
	{
		switch (SDL_GetCurrentDisplayOrientation(displayForIndex(displayindex)))
		{
		case SDL_ORIENTATION_LANDSCAPE:         return ORIENTATION_LANDSCAPE;
		case SDL_ORIENTATION_LANDSCAPE_FLIPPED: return ORIENTATION_LANDSCAPE_FLIPPED;
		case SDL_ORIENTATION_PORTRAIT:          return ORIENTATION_PORTRAIT;
		case SDL_ORIENTATION_PORTRAIT_FLIPPED:  return ORIENTATION_PORTRAIT_FLIPPED;
		default:                                return ORIENTATION_UNKNOWN;
		}
	}

	std::vector<DisplayMode> getFullscreenModes(int displayindex) const override
	{
		std::vector<DisplayMode> result;
		int count = 0;
		SDL_DisplayMode **modes = SDL_GetFullscreenDisplayModes(displayForIndex(displayindex), &count);
		for (int i = 0; i < count; i++)
		{
			DisplayMode m;
			m.width = modes[i]->w;
			m.height = modes[i]->h;
			m.refreshRate = modes[i]->refresh_rate;
			result.push_back(m);
		}
		SDL_free(modes);
		return result;
	}

	void getDesktopDimensions(int displayindex, int &width, int &height) const override
	{
		const SDL_DisplayMode *mode = SDL_GetDesktopDisplayMode(displayForIndex(displayindex));
		if (mode != nullptr)
		{
			width = mode->w;
			height = mode->h;
		}
		else
		{
			width = 0;
			height = 0;
		}
	}

	// --- position ---------------------------------------------------------

	void setPosition(int x, int y, int /*displayindex*/, bool /*waitForSync*/) override
	{
		if (window != nullptr)
			SDL_SetWindowPosition(window, x, y);
	}

	void getPosition(int &x, int &y, int &displayindex) override
	{
		x = y = 0;
		if (window != nullptr)
			SDL_GetWindowPosition(window, &x, &y);
		displayindex = indexForDisplay(window != nullptr ? SDL_GetDisplayForWindow(window) : 0);
	}

	Rect getSafeArea() const override
	{
		SDL_Rect r = {0, 0, windowWidth, windowHeight};
		if (window != nullptr)
			SDL_GetWindowSafeArea(window, &r);
		return Rect{r.x, r.y, r.w, r.h};
	}

	// --- title ------------------------------------------------------------

	void setWindowTitle(const std::string &t) override
	{
		title = t;
		if (window != nullptr)
			SDL_SetWindowTitle(window, t.c_str());
	}

	const std::string &getWindowTitle() const override { return title; }

	// --- icon -------------------------------------------------------------

	bool setIcon(love::image::ImageData *imgd) override
	{
		if (imgd == nullptr)
			return false;

		if (imgd->getFormat() != PIXELFORMAT_RGBA8_UNORM)
			throw love::Exception("setIcon only accepts 32-bit RGBA images.");

		icon.set(imgd);

		if (window == nullptr)
			return false;

		int w = imgd->getWidth();
		int h = imgd->getHeight();
		int pitch = w * (int) getPixelFormatBlockSize(imgd->getFormat());

		SDL_Surface *sdlicon = SDL_CreateSurfaceFrom(w, h, SDL_PIXELFORMAT_ABGR8888, imgd->getData(), pitch);
		if (sdlicon == nullptr)
			return false;

		SDL_SetWindowIcon(window, sdlicon);
		SDL_DestroySurface(sdlicon);
		return true;
	}

	love::image::ImageData *getIcon() override { return icon.get(); }

	// --- vsync (no renderer context, so just remembered) ------------------

	void setVSync(int vsync) override { settings.vsync = vsync; }
	int getVSync() const override { return settings.vsync; }

	// --- misc window state ------------------------------------------------

	void setDisplaySleepEnabled(bool enable) override
	{
		if (enable) SDL_EnableScreenSaver();
		else        SDL_DisableScreenSaver();
	}

	bool isDisplaySleepEnabled() const override { return SDL_ScreenSaverEnabled(); }

	void minimize() override { if (window) SDL_MinimizeWindow(window); }
	void maximize() override { if (window) SDL_MaximizeWindow(window); }
	void restore()  override { if (window) SDL_RestoreWindow(window); }
	void focus()    override { if (window) SDL_RaiseWindow(window); }

	bool isMaximized() const override { return hasFlag(SDL_WINDOW_MAXIMIZED); }
	bool isMinimized() const override { return hasFlag(SDL_WINDOW_MINIMIZED); }

	bool hasFocus()      const override { return hasFlag(SDL_WINDOW_INPUT_FOCUS); }
	bool hasMouseFocus() const override { return hasFlag(SDL_WINDOW_MOUSE_FOCUS); }

	bool isVisible()  const override { return window != nullptr && !hasFlag(SDL_WINDOW_HIDDEN); }
	bool isOccluded() const override { return hasFlag(SDL_WINDOW_OCCLUDED); }

	void setMouseGrab(bool grab) override
	{
		mouseGrabbed = grab;
		if (window != nullptr)
			SDL_SetWindowMouseGrab(window, grab);
	}

	bool isMouseGrabbed() const override { return mouseGrabbed; }

	int getWidth()  const override { return windowWidth; }
	int getHeight() const override { return windowHeight; }
	int getPixelWidth()  const override { return pixelWidth; }
	int getPixelHeight() const override { return pixelHeight; }

	// --- coordinate transforms (HiDPI) ------------------------------------
	// Faithful to window/sdl/Window.cpp: window<->pixel uses the tracked
	// pixel/window size ratio (SDL_GetWindowSizeInPixels), and the DPI scale is
	// SDL's display scale, honored only when the window opted into use_dpi_scale.

	void clampPositionInWindow(double *wx, double *wy) const override
	{
		if (wx) *wx = std::max(0.0, std::min(*wx, (double) windowWidth));
		if (wy) *wy = std::max(0.0, std::min(*wy, (double) windowHeight));
	}

	void windowToPixelCoords(double *x, double *y) const override
	{
		if (x != nullptr) *x = (*x) * ((double) pixelWidth / (double) windowWidth);
		if (y != nullptr) *y = (*y) * ((double) pixelHeight / (double) windowHeight);
	}

	void pixelToWindowCoords(double *x, double *y) const override
	{
		if (x != nullptr) *x = (*x) * ((double) windowWidth / (double) pixelWidth);
		if (y != nullptr) *y = (*y) * ((double) windowHeight / (double) pixelHeight);
	}

	void windowToDPICoords(double *x, double *y) const override
	{
		double px = x != nullptr ? *x : 0.0;
		double py = y != nullptr ? *y : 0.0;
		windowToPixelCoords(&px, &py);
		double dpix = 0.0, dpiy = 0.0;
		fromPixels(px, py, dpix, dpiy);
		if (x != nullptr) *x = dpix;
		if (y != nullptr) *y = dpiy;
	}

	void DPIToWindowCoords(double *x, double *y) const override
	{
		double dpix = x != nullptr ? *x : 0.0;
		double dpiy = y != nullptr ? *y : 0.0;
		double px = 0.0, py = 0.0;
		toPixels(dpix, dpiy, px, py);
		pixelToWindowCoords(&px, &py);
		if (x != nullptr) *x = px;
		if (y != nullptr) *y = py;
	}

	double getDPIScale() const override { return settings.usedpiscale ? getNativeDPIScale() : 1.0; }
	double getNativeDPIScale() const override { return window != nullptr ? SDL_GetWindowDisplayScale(window) : 1.0; }

	double toPixels(double x) const override { return x * getDPIScale(); }
	void toPixels(double wx, double wy, double &px, double &py) const override
	{
		double scale = getDPIScale();
		px = wx * scale;
		py = wy * scale;
	}
	double fromPixels(double x) const override { return x / getDPIScale(); }
	void fromPixels(double px, double py, double &wx, double &wy) const override
	{
		double scale = getDPIScale();
		wx = px / scale;
		wy = py / scale;
	}

	void *getHandle() const override { return window; }

	// --- dialogs / attention ----------------------------------------------

	bool showMessageBox(const std::string &t, const std::string &m, MessageBoxType type, bool /*attach*/) override
	{
		return SDL_ShowSimpleMessageBox(convertMessageBoxFlags(type), t.c_str(), m.c_str(), window);
	}

	int showMessageBox(const MessageBoxData &data) override
	{
		std::vector<SDL_MessageBoxButtonData> buttons;
		for (size_t i = 0; i < data.buttons.size(); i++)
		{
			SDL_MessageBoxButtonData b = {};
			b.buttonID = (int) i;
			b.text = data.buttons[i].c_str();
			if ((int) i == data.enterButtonIndex)  b.flags |= SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT;
			if ((int) i == data.escapeButtonIndex) b.flags |= SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT;
			buttons.push_back(b);
		}

		SDL_MessageBoxData boxdata = {};
		boxdata.flags = convertMessageBoxFlags(data.type);
		boxdata.window = data.attachToWindow ? window : nullptr;
		boxdata.title = data.title.c_str();
		boxdata.message = data.message.c_str();
		boxdata.numbuttons = (int) buttons.size();
		boxdata.buttons = buttons.data();

		int pressed = -1;
		SDL_ShowMessageBox(&boxdata, &pressed);
		return pressed;
	}

	void showFileDialog(const FileDialogData &, FileDialogCallback callback, void *context) override
	{
		// The lean backend doesn't host a native file dialog -- that needs the
		// async SDL-event delivery built into the real window/sdl/Window.cpp
		// (folded into the #win-backend swap). The Ruby callback plumbing is
		// real, though: report "unsupported" through it so a game's block runs
		// and can handle the error path, exactly as it would a user cancel.
		if (callback != nullptr)
			callback(context, std::vector<std::string>(), nullptr, "File dialogs are not supported in the mruby harness window backend.");
	}

	void requestAttention(bool continuous) override
	{
		if (window != nullptr)
			SDL_FlashWindow(window, continuous ? SDL_FLASH_UNTIL_FOCUSED : SDL_FLASH_BRIEFLY);
	}

	SystemTheme getSystemTheme() const override
	{
		switch (SDL_GetSystemTheme())
		{
		case SDL_SYSTEM_THEME_LIGHT: return THEME_LIGHT;
		case SDL_SYSTEM_THEME_DARK:  return THEME_DARK;
		default:                     return THEME_UNKNOWN;
		}
	}

private:

	SDL_MessageBoxFlags convertMessageBoxFlags(MessageBoxType type) const
	{
		switch (type)
		{
		case MESSAGEBOX_ERROR:   return SDL_MESSAGEBOX_ERROR;
		case MESSAGEBOX_WARNING: return SDL_MESSAGEBOX_WARNING;
		default:                 return SDL_MESSAGEBOX_INFORMATION;
		}
	}

	bool hasFlag(Uint32 flag) const
	{
		return window != nullptr && (SDL_GetWindowFlags(window) & flag) != 0;
	}

	// Maps a 0-based display index onto an SDL display id (falling back to the
	// primary display), and the inverse.
	SDL_DisplayID displayForIndex(int index) const
	{
		int count = 0;
		SDL_DisplayID *ids = SDL_GetDisplays(&count);
		SDL_DisplayID result = (count > 0) ? ids[0] : 0;
		if (ids != nullptr && index >= 0 && index < count)
			result = ids[index];
		SDL_free(ids);
		return result;
	}

	int indexForDisplay(SDL_DisplayID id) const
	{
		int count = 0;
		SDL_DisplayID *ids = SDL_GetDisplays(&count);
		int result = 0;
		for (int i = 0; i < count; i++)
			if (ids[i] == id) { result = i; break; }
		SDL_free(ids);
		return result;
	}

	void refreshDimensions()
	{
		if (window == nullptr)
			return;
		SDL_GetWindowSize(window, &windowWidth, &windowHeight);
		if (!SDL_GetWindowSizeInPixels(window, &pixelWidth, &pixelHeight))
		{
			pixelWidth = windowWidth;
			pixelHeight = windowHeight;
		}
	}

	std::string title = "LÖVE";
	int windowWidth = 800;
	int windowHeight = 600;
	int pixelWidth = 800;
	int pixelHeight = 600;
	bool open = false;
	bool mouseGrabbed = false;
	WindowSettings settings;
	SDL_Window *window = nullptr;
	StrongRef<love::image::ImageData> icon;

}; // HarnessWindow

// =========================================================================
// Love::Window module functions
// =========================================================================

// Builds a WindowSettings from the flattened keyword arguments. Each kwarg that
// the caller omitted arrives as `undef`, leaving the WindowSettings default.
static void readWindowSettings(mrb_state *mrb, mrb_value *v, WindowSettings &s)
{
	// Index map matches the key list in w_set_mode.
	s.fullscreen  = mrbx_optboolean(mrb, v[2], s.fullscreen);

	if (!mrb_undef_p(v[3]))
	{
		std::string typestr = mrbx_checkstring(mrb, v[3]);
		if (!Window::getConstant(typestr.c_str(), s.fstype))
			mrb_raisef(mrb, E_ARGUMENT_ERROR, "Invalid fullscreen type: %s", typestr.c_str());
	}

	s.vsync       = mrbx_optint(mrb, v[4], s.vsync);
	s.msaa        = mrbx_optint(mrb, v[5], s.msaa);
	s.stencil     = mrbx_optboolean(mrb, v[6], s.stencil);
	s.depth       = mrbx_optboolean(mrb, v[7], s.depth);
	s.resizable   = mrbx_optboolean(mrb, v[8], s.resizable);
	s.minwidth    = mrbx_optint(mrb, v[9], s.minwidth);
	s.minheight   = mrbx_optint(mrb, v[10], s.minheight);
	s.borderless  = mrbx_optboolean(mrb, v[11], s.borderless);
	s.centered    = mrbx_optboolean(mrb, v[12], s.centered);
	// Display index is 1-based in the Ruby API, 0-based internally.
	s.displayindex = mrbx_optint(mrb, v[13], s.displayindex + 1) - 1;
	s.usedpiscale = mrbx_optboolean(mrb, v[14], s.usedpiscale);

	if (!mrb_undef_p(v[15]) || !mrb_undef_p(v[16]))
	{
		s.useposition = true;
		s.x = mrbx_optint(mrb, v[15], 0);
		s.y = mrbx_optint(mrb, v[16], 0);
	}
}

static void hset(mrb_state *mrb, mrb_value h, const char *key, mrb_value val)
{
	mrb_hash_set(mrb, h, mrb_symbol_value(mrb_intern_cstr(mrb, key)), val);
}

static mrb_value w_set_mode(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[17];
	mrbx_get_kwargs(mrb, {"width", "height", "fullscreen", "fullscreen_type",
		"vsync", "msaa", "stencil", "depth", "resizable", "min_width",
		"min_height", "borderless", "centered", "display", "use_dpi_scale",
		"x", "y"}, 2, v);

	int w = mrbx_checkint(mrb, v[0]);
	int h = mrbx_checkint(mrb, v[1]);

	WindowSettings settings;
	readWindowSettings(mrb, v, settings);

	bool success = false;
	mrbx_catchexcept(mrb, [&]() { success = instance()->setWindow(w, h, &settings); });
	return mrbx_boolean(mrb, success);
}

static mrb_value w_get_mode(mrb_state *mrb, mrb_value self)
{
	(void) self;
	int w, h;
	WindowSettings s;
	instance()->getWindow(w, h, s);

	const char *fstypestr = "desktop";
	Window::getConstant(s.fstype, fstypestr);

	mrb_value out = mrb_hash_new(mrb);
	hset(mrb, out, "width", mrbx_integer(mrb, w));
	hset(mrb, out, "height", mrbx_integer(mrb, h));
	hset(mrb, out, "fullscreen", mrbx_boolean(mrb, s.fullscreen));
	hset(mrb, out, "fullscreen_type", mrbx_string(mrb, fstypestr));
	hset(mrb, out, "vsync", mrbx_integer(mrb, s.vsync));
	hset(mrb, out, "msaa", mrbx_integer(mrb, s.msaa));
	hset(mrb, out, "stencil", mrbx_boolean(mrb, s.stencil));
	hset(mrb, out, "depth", mrbx_boolean(mrb, s.depth));
	hset(mrb, out, "resizable", mrbx_boolean(mrb, s.resizable));
	hset(mrb, out, "min_width", mrbx_integer(mrb, s.minwidth));
	hset(mrb, out, "min_height", mrbx_integer(mrb, s.minheight));
	hset(mrb, out, "borderless", mrbx_boolean(mrb, s.borderless));
	hset(mrb, out, "centered", mrbx_boolean(mrb, s.centered));
	hset(mrb, out, "display", mrbx_integer(mrb, s.displayindex + 1));
	hset(mrb, out, "use_dpi_scale", mrbx_boolean(mrb, s.usedpiscale));
	hset(mrb, out, "refreshrate", mrbx_number(mrb, s.refreshrate));
	hset(mrb, out, "x", mrbx_integer(mrb, s.x));
	hset(mrb, out, "y", mrbx_integer(mrb, s.y));
	return out;
}

static mrb_value w_is_open(mrb_state *mrb, mrb_value self)
{
	(void) self;
	return mrbx_boolean(mrb, instance()->isOpen());
}

static mrb_value w_get_display_count(mrb_state *mrb, mrb_value self)
{
	(void) self;
	return mrbx_integer(mrb, instance()->getDisplayCount());
}

static mrb_value w_get_display_name(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[1];
	mrbx_get_kwargs(mrb, {"display"}, 0, v);
	int index = mrbx_optint(mrb, v[0], 1) - 1;

	const char *name = nullptr;
	mrbx_catchexcept(mrb, [&]() { name = instance()->getDisplayName(index); });
	return mrbx_string(mrb, name != nullptr ? name : "");
}

static mrb_value w_get_display_orientation(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[1];
	mrbx_get_kwargs(mrb, {"display"}, 0, v);
	int index = mrbx_optint(mrb, v[0], 1) - 1;

	const char *str = "unknown";
	Window::getConstant(instance()->getDisplayOrientation(index), str);
	return mrbx_string(mrb, str);
}

static mrb_value w_get_fullscreen_modes(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[1];
	mrbx_get_kwargs(mrb, {"display"}, 0, v);
	int index = mrbx_optint(mrb, v[0], 1) - 1;

	std::vector<Window::DisplayMode> modes = instance()->getFullscreenModes(index);
	mrb_value arr = mrb_ary_new_capa(mrb, (mrb_int) modes.size());
	for (const auto &m : modes)
	{
		mrb_value entry = mrb_hash_new(mrb);
		hset(mrb, entry, "width", mrbx_integer(mrb, m.width));
		hset(mrb, entry, "height", mrbx_integer(mrb, m.height));
		hset(mrb, entry, "refreshrate", mrbx_number(mrb, m.refreshRate));
		mrb_ary_push(mrb, arr, entry);
	}
	return arr;
}

static mrb_value w_get_desktop_dimensions(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[1];
	mrbx_get_kwargs(mrb, {"display"}, 0, v);
	int index = mrbx_optint(mrb, v[0], 1) - 1;

	int w = 0, h = 0;
	instance()->getDesktopDimensions(index, w, h);
	mrb_value arr = mrb_ary_new_capa(mrb, 2);
	mrb_ary_push(mrb, arr, mrbx_integer(mrb, w));
	mrb_ary_push(mrb, arr, mrbx_integer(mrb, h));
	return arr;
}

static mrb_value w_set_fullscreen(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[2];
	mrbx_get_kwargs(mrb, {"fullscreen", "type"}, 1, v);

	bool fullscreen = mrbx_checkboolean(mrb, v[0]);
	Window::FullscreenType fstype = Window::FULLSCREEN_MAX_ENUM;
	if (!mrb_undef_p(v[1]))
	{
		std::string typestr = mrbx_checkstring(mrb, v[1]);
		if (!Window::getConstant(typestr.c_str(), fstype))
			mrb_raisef(mrb, E_ARGUMENT_ERROR, "Invalid fullscreen type: %s", typestr.c_str());
	}

	bool success = false;
	mrbx_catchexcept(mrb, [&]() {
		if (fstype == Window::FULLSCREEN_MAX_ENUM)
			success = instance()->setFullscreen(fullscreen);
		else
			success = instance()->setFullscreen(fullscreen, fstype);
	});
	return mrbx_boolean(mrb, success);
}

static mrb_value w_get_fullscreen(mrb_state *mrb, mrb_value self)
{
	(void) self;
	int w, h;
	WindowSettings s;
	instance()->getWindow(w, h, s);

	const char *typestr = "desktop";
	Window::getConstant(s.fstype, typestr);

	mrb_value out = mrb_hash_new(mrb);
	hset(mrb, out, "fullscreen", mrbx_boolean(mrb, s.fullscreen));
	hset(mrb, out, "type", mrbx_string(mrb, typestr));
	return out;
}

static mrb_value w_set_position(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[4];
	mrbx_get_kwargs(mrb, {"x", "y", "display", "wait_for_sync"}, 2, v);
	int x = mrbx_checkint(mrb, v[0]);
	int y = mrbx_checkint(mrb, v[1]);
	int display = mrbx_optint(mrb, v[2], 1) - 1;
	bool wait = mrbx_optboolean(mrb, v[3], true);
	instance()->setPosition(x, y, display, wait);
	return mrb_nil_value();
}

static mrb_value w_get_position(mrb_state *mrb, mrb_value self)
{
	(void) self;
	int x = 0, y = 0, display = 0;
	instance()->getPosition(x, y, display);
	mrb_value out = mrb_hash_new(mrb);
	hset(mrb, out, "x", mrbx_integer(mrb, x));
	hset(mrb, out, "y", mrbx_integer(mrb, y));
	hset(mrb, out, "display", mrbx_integer(mrb, display + 1));
	return out;
}

static mrb_value w_get_safe_area(mrb_state *mrb, mrb_value self)
{
	(void) self;
	Rect r = instance()->getSafeArea();
	mrb_value out = mrb_hash_new(mrb);
	hset(mrb, out, "x", mrbx_integer(mrb, r.x));
	hset(mrb, out, "y", mrbx_integer(mrb, r.y));
	hset(mrb, out, "width", mrbx_integer(mrb, r.w));
	hset(mrb, out, "height", mrbx_integer(mrb, r.h));
	return out;
}

static mrb_value w_set_title(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[1];
	mrbx_get_kwargs(mrb, {"title"}, 1, v);
	instance()->setWindowTitle(mrbx_checkstring(mrb, v[0]));
	return mrb_nil_value();
}

static mrb_value w_get_title(mrb_state *mrb, mrb_value self)
{
	(void) self;
	return mrbx_string(mrb, instance()->getWindowTitle());
}

static mrb_value w_set_vsync(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[1];
	mrbx_get_kwargs(mrb, {"vsync"}, 1, v);
	instance()->setVSync(mrbx_checkint(mrb, v[0]));
	return mrb_nil_value();
}

static mrb_value w_get_vsync(mrb_state *mrb, mrb_value self)
{
	(void) self;
	return mrbx_integer(mrb, instance()->getVSync());
}

static mrb_value w_set_display_sleep_enabled(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[1];
	mrbx_get_kwargs(mrb, {"enable"}, 1, v);
	instance()->setDisplaySleepEnabled(mrbx_checkboolean(mrb, v[0]));
	return mrb_nil_value();
}

static mrb_value w_is_display_sleep_enabled(mrb_state *mrb, mrb_value self)
{
	(void) self;
	return mrbx_boolean(mrb, instance()->isDisplaySleepEnabled());
}

static mrb_value w_minimize(mrb_state *mrb, mrb_value self) { (void) self; instance()->minimize(); return mrb_nil_value(); }
static mrb_value w_maximize(mrb_state *mrb, mrb_value self) { (void) self; instance()->maximize(); return mrb_nil_value(); }
static mrb_value w_restore (mrb_state *mrb, mrb_value self) { (void) self; instance()->restore();  return mrb_nil_value(); }
static mrb_value w_focus   (mrb_state *mrb, mrb_value self) { (void) self; instance()->focus();    return mrb_nil_value(); }

static mrb_value w_is_maximized(mrb_state *mrb, mrb_value self) { (void) self; return mrbx_boolean(mrb, instance()->isMaximized()); }
static mrb_value w_is_minimized(mrb_state *mrb, mrb_value self) { (void) self; return mrbx_boolean(mrb, instance()->isMinimized()); }
static mrb_value w_has_focus(mrb_state *mrb, mrb_value self) { (void) self; return mrbx_boolean(mrb, instance()->hasFocus()); }
static mrb_value w_has_mouse_focus(mrb_state *mrb, mrb_value self) { (void) self; return mrbx_boolean(mrb, instance()->hasMouseFocus()); }
static mrb_value w_is_visible(mrb_state *mrb, mrb_value self) { (void) self; return mrbx_boolean(mrb, instance()->isVisible()); }
static mrb_value w_is_occluded(mrb_state *mrb, mrb_value self) { (void) self; return mrbx_boolean(mrb, instance()->isOccluded()); }

static mrb_value w_get_dpi_scale(mrb_state *mrb, mrb_value self) { (void) self; return mrbx_number(mrb, instance()->getDPIScale()); }
static mrb_value w_get_native_dpi_scale(mrb_state *mrb, mrb_value self) { (void) self; return mrbx_number(mrb, instance()->getNativeDPIScale()); }

// to_pixels(x:) -> Number, or to_pixels(x:, y:) -> Hash {x:, y:}. Converts
// density-independent units to pixels using the window's DPI scale.
static mrb_value w_to_pixels(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[2];
	mrbx_get_kwargs(mrb, {"x", "y"}, 1, v);
	double wx = mrbx_checknumber(mrb, v[0]);
	if (mrb_undef_p(v[1]))
		return mrbx_number(mrb, instance()->toPixels(wx));
	double px = 0.0, py = 0.0;
	instance()->toPixels(wx, mrbx_checknumber(mrb, v[1]), px, py);
	mrb_value out = mrb_hash_new(mrb);
	hset(mrb, out, "x", mrbx_number(mrb, px));
	hset(mrb, out, "y", mrbx_number(mrb, py));
	return out;
}

// from_pixels(x:) -> Number, or from_pixels(x:, y:) -> Hash {x:, y:}. Inverse of
// to_pixels: converts pixels back to density-independent units.
static mrb_value w_from_pixels(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[2];
	mrbx_get_kwargs(mrb, {"x", "y"}, 1, v);
	double px = mrbx_checknumber(mrb, v[0]);
	if (mrb_undef_p(v[1]))
		return mrbx_number(mrb, instance()->fromPixels(px));
	double wx = 0.0, wy = 0.0;
	instance()->fromPixels(px, mrbx_checknumber(mrb, v[1]), wx, wy);
	mrb_value out = mrb_hash_new(mrb);
	hset(mrb, out, "x", mrbx_number(mrb, wx));
	hset(mrb, out, "y", mrbx_number(mrb, wy));
	return out;
}

static mrb_value w_request_attention(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[1];
	mrbx_get_kwargs(mrb, {"continuous"}, 0, v);
	instance()->requestAttention(mrbx_optboolean(mrb, v[0], false));
	return mrb_nil_value();
}

static mrb_value w_get_system_theme(mrb_state *mrb, mrb_value self)
{
	(void) self;
	const char *str = "unknown";
	Window::getConstant(instance()->getSystemTheme(), str);
	return mrbx_string(mrb, str);
}

static mrb_value w_show_message_box(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[4];
	mrbx_get_kwargs(mrb, {"title", "message", "type", "attach_to_window"}, 2, v);

	std::string title = mrbx_checkstring(mrb, v[0]);
	std::string message = mrbx_checkstring(mrb, v[1]);

	Window::MessageBoxType type = Window::MESSAGEBOX_INFO;
	if (!mrb_undef_p(v[2]))
	{
		std::string typestr = mrbx_checkstring(mrb, v[2]);
		if (!Window::getConstant(typestr.c_str(), type))
			mrb_raisef(mrb, E_ARGUMENT_ERROR, "Invalid messagebox type: %s", typestr.c_str());
	}
	bool attach = mrbx_optboolean(mrb, v[3], true);

	bool success = false;
	mrbx_catchexcept(mrb, [&]() { success = instance()->showMessageBox(title, message, type, attach); });
	return mrbx_boolean(mrb, success);
}

static mrb_value w_set_icon(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[1];
	mrbx_get_kwargs(mrb, {"image_data"}, 1, v);
	love::image::ImageData *imgd = mrbx_checktype<love::image::ImageData>(mrb, v[0]);
	bool success = false;
	mrbx_catchexcept(mrb, [&]() { success = instance()->setIcon(imgd); });
	return mrbx_boolean(mrb, success);
}

static mrb_value w_get_icon(mrb_state *mrb, mrb_value self)
{
	(void) self;
	return mrbx_pushtype(mrb, instance()->getIcon());
}

// Carries the Ruby block from w_show_file_dialog down to the C callback the
// Window backend invokes when the dialog resolves. The lean backend calls back
// synchronously (still within w_show_file_dialog), so the block stays referenced
// on the C stack and needs no separate GC root; a future async backend would
// pin it instead.
struct FileDialogContext
{
	mrb_state *mrb;
	mrb_value block;
};

static void mrbFileDialogCallback(void *context, const std::vector<std::string> &files,
	const char *filtername, const char *err)
{
	auto ctx = (FileDialogContext *) context;
	mrb_state *mrb = ctx->mrb;

	mrb_value filesary = mrb_ary_new_capa(mrb, (mrb_int) files.size());
	for (const std::string &f : files)
		mrb_ary_push(mrb, filesary, mrbx_string(mrb, f));

	mrb_value argv[3] = {
		filesary,
		filtername != nullptr ? mrbx_string(mrb, filtername) : mrb_nil_value(),
		err != nullptr ? mrbx_string(mrb, err) : mrb_nil_value(),
	};
	mrb_yield_argv(mrb, ctx->block, 3, argv);
}

// Love::Window.show_file_dialog(type:, ...) { |files, filter_name, err| ... }
// The trailing block is the result callback (Lua passed it as the 2nd arg). It
// receives the selected paths (Array), the matched filter name (or nil), and an
// error string (or nil) -- the same triple love.window.showFileDialog yields.
static mrb_value w_show_file_dialog(mrb_state *mrb, mrb_value self)
{
	(void) self;

	// Grab the keyword arguments and the result block together. "type" is the
	// only required keyword; omitting it raises ArgumentError via mrb_get_args.
	mrb_value blk = mrb_nil_value();
	mrb_value kv[8];
	mrb_sym names[8] = {
		mrb_intern_lit(mrb, "type"),
		mrb_intern_lit(mrb, "title"),
		mrb_intern_lit(mrb, "accept_label"),
		mrb_intern_lit(mrb, "cancel_label"),
		mrb_intern_lit(mrb, "default_name"),
		mrb_intern_lit(mrb, "filters"),
		mrb_intern_lit(mrb, "multi_select"),
		mrb_intern_lit(mrb, "attach_to_window"),
	};
	const mrb_kwargs kw = { 8, 1, names, kv, nullptr };
	mrb_get_args(mrb, ":&", &kw, &blk);

	if (mrb_nil_p(blk))
		mrb_raise(mrb, E_ARGUMENT_ERROR, "show_file_dialog requires a block to receive the result");

	Window::FileDialogData data = {};

	std::string typestr = mrbx_checkstring(mrb, kv[0]);
	if (!Window::getConstant(typestr.c_str(), data.type))
		mrb_raisef(mrb, E_ARGUMENT_ERROR, "Invalid file dialog type: %s", typestr.c_str());

	if (!mrb_undef_p(kv[1])) data.title       = mrbx_checkstring(mrb, kv[1]);
	if (!mrb_undef_p(kv[2])) data.acceptLabel = mrbx_checkstring(mrb, kv[2]);
	if (!mrb_undef_p(kv[3])) data.cancelLabel = mrbx_checkstring(mrb, kv[3]);
	if (!mrb_undef_p(kv[4])) data.defaultName = mrbx_checkstring(mrb, kv[4]);

	// filters: a Hash mapping a filter name to its pattern, e.g.
	//   filters: { "Images" => "png;jpg", "All" => "*" }
	if (!mrb_undef_p(kv[5]))
	{
		mrb_value fh = kv[5];
		mrb_value keys = mrb_hash_keys(mrb, fh);
		for (mrb_int i = 0; i < RARRAY_LEN(keys); i++)
		{
			mrb_value k = mrb_ary_ref(mrb, keys, i);
			Window::FileDialogFilter filter = {};
			filter.name = mrbx_checkstring(mrb, k);
			filter.pattern = mrbx_checkstring(mrb, mrb_hash_get(mrb, fh, k));
			data.filters.push_back(filter);
		}
	}

	data.multiSelect    = mrbx_optboolean(mrb, kv[6], false);
	data.attachToWindow = mrbx_optboolean(mrb, kv[7], false);

	FileDialogContext ctx = { mrb, blk };
	mrbx_catchexcept(mrb, [&]() { instance()->showFileDialog(data, mrbFileDialogCallback, &ctx); });
	return mrb_nil_value();
}

// Re-apply the window mode. Like set_mode but every keyword is optional: any
// omitted key (including width/height) keeps the current window's value, so a
// game can tweak a single setting without restating the whole mode.
static mrb_value w_update_mode(mrb_state *mrb, mrb_value self)
{
	(void) self;
	mrb_value v[17];
	mrbx_get_kwargs(mrb, {"width", "height", "fullscreen", "fullscreen_type",
		"vsync", "msaa", "stencil", "depth", "resizable", "min_width",
		"min_height", "borderless", "centered", "display", "use_dpi_scale",
		"x", "y"}, 0, v);

	int w, h;
	WindowSettings settings;
	instance()->getWindow(w, h, settings);

	w = mrbx_optint(mrb, v[0], w);
	h = mrbx_optint(mrb, v[1], h);
	readWindowSettings(mrb, v, settings);

	bool success = false;
	mrbx_catchexcept(mrb, [&]() { success = instance()->setWindow(w, h, &settings); });
	return mrbx_boolean(mrb, success);
}

// The native window handle as an opaque pointer (Lua returned a lightuserdata;
// the mruby analog is a TT_CPTR value). For native/FFI interop only.
static mrb_value w_get_pointer(mrb_state *mrb, mrb_value self)
{
	(void) self; (void) mrb;
	return mrb_cptr_value(mrb, instance()->getHandle());
}

static const MrbReg functions[] =
{
	{ "set_mode",                  w_set_mode,                  MRB_ARGS_KEY(17, 0) },
	{ "get_mode",                  w_get_mode,                  MRB_ARGS_NONE() },
	{ "is_open",                   w_is_open,                   MRB_ARGS_NONE() },
	{ "get_display_count",         w_get_display_count,         MRB_ARGS_NONE() },
	{ "get_display_name",          w_get_display_name,          MRB_ARGS_KEY(1, 0) },
	{ "get_display_orientation",   w_get_display_orientation,   MRB_ARGS_KEY(1, 0) },
	{ "get_fullscreen_modes",      w_get_fullscreen_modes,      MRB_ARGS_KEY(1, 0) },
	{ "get_desktop_dimensions",    w_get_desktop_dimensions,    MRB_ARGS_KEY(1, 0) },
	{ "set_fullscreen",            w_set_fullscreen,            MRB_ARGS_KEY(2, 0) },
	{ "get_fullscreen",            w_get_fullscreen,            MRB_ARGS_NONE() },
	{ "set_position",              w_set_position,              MRB_ARGS_KEY(4, 0) },
	{ "get_position",              w_get_position,              MRB_ARGS_NONE() },
	{ "get_safe_area",             w_get_safe_area,             MRB_ARGS_NONE() },
	{ "set_title",                 w_set_title,                 MRB_ARGS_KEY(1, 0) },
	{ "get_title",                 w_get_title,                 MRB_ARGS_NONE() },
	{ "set_vsync",                 w_set_vsync,                 MRB_ARGS_KEY(1, 0) },
	{ "get_vsync",                 w_get_vsync,                 MRB_ARGS_NONE() },
	{ "set_display_sleep_enabled", w_set_display_sleep_enabled, MRB_ARGS_KEY(1, 0) },
	{ "is_display_sleep_enabled",  w_is_display_sleep_enabled,  MRB_ARGS_NONE() },
	{ "minimize",                  w_minimize,                  MRB_ARGS_NONE() },
	{ "maximize",                  w_maximize,                  MRB_ARGS_NONE() },
	{ "restore",                   w_restore,                   MRB_ARGS_NONE() },
	{ "focus",                     w_focus,                     MRB_ARGS_NONE() },
	{ "is_maximized",              w_is_maximized,              MRB_ARGS_NONE() },
	{ "is_minimized",              w_is_minimized,              MRB_ARGS_NONE() },
	{ "has_focus",                 w_has_focus,                 MRB_ARGS_NONE() },
	{ "has_mouse_focus",           w_has_mouse_focus,           MRB_ARGS_NONE() },
	{ "is_visible",                w_is_visible,                MRB_ARGS_NONE() },
	{ "is_occluded",               w_is_occluded,               MRB_ARGS_NONE() },
	{ "get_dpi_scale",             w_get_dpi_scale,             MRB_ARGS_NONE() },
	{ "get_native_dpi_scale",      w_get_native_dpi_scale,      MRB_ARGS_NONE() },
	{ "to_pixels",                 w_to_pixels,                 MRB_ARGS_KEY(2, 0) },
	{ "from_pixels",               w_from_pixels,               MRB_ARGS_KEY(2, 0) },
	{ "request_attention",         w_request_attention,         MRB_ARGS_KEY(1, 0) },
	{ "get_system_theme",          w_get_system_theme,          MRB_ARGS_NONE() },
	{ "show_message_box",          w_show_message_box,          MRB_ARGS_KEY(4, 0) },
	{ "set_icon",                  w_set_icon,                  MRB_ARGS_KEY(1, 0) },
	{ "get_icon",                  w_get_icon,                  MRB_ARGS_NONE() },
	{ "show_file_dialog",          w_show_file_dialog,          MRB_ARGS_KEY(8, 0) | MRB_ARGS_BLOCK() },
	{ "update_mode",               w_update_mode,               MRB_ARGS_KEY(17, 0) },
	{ "get_pointer",               w_get_pointer,               MRB_ARGS_NONE() },
	{ nullptr, nullptr, 0 }
};

// Equivalent of luaopen_love_window: creates the lean backend and registers
// Love::Window with its keyword-argument methods.
extern "C" void mrb_love_window_init(mrb_state *mrb)
{
	Window *inst = instance();
	if (inst == nullptr)
	{
		// If the video subsystem can't initialize (e.g. a headless machine with
		// no display), skip registering Love::Window entirely rather than
		// aborting. Scripts/boot already guard window use with const_defined?.
		try
		{
			inst = new HarnessWindow();
		}
		catch (const std::exception &e)
		{
			SDL_Log("love.window unavailable: %s", e.what());
			return;
		}
	}
	else
		inst->retain();

	WrappedModule w;
	w.module = inst;
	w.name = "Window";
	w.type = &Module::type;
	w.functions = functions;

	mrbx_register_module(mrb, w);
}

} // window
} // love
