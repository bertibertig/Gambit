// Splatoon Recompiled additions to Gambit's runtime: platform / window setup.
#pragma once

#include <SDL.h>
#include <cstdint>

// Call before SDL_Init: video driver preference (Wayland first, X11 as
// fallback, unless SDL_VIDEODRIVER is set), Wayland app id / X11 class.
void splat_platform_init();

// Extra SDL_CreateWindow flags (HiDPI drawable on scaled Wayland outputs).
uint32_t splat_platform_window_flags();

// Window placement from the settings (window mode, monitor): position, size
// and fullscreen flags for SDL_CreateWindow. Call after SDL_Init.
void splat_platform_window_rect(int& x, int& y, int& w, int& h, uint32_t& flags);

// Logs the video driver SDL picked. Call after the window exists.
void splat_platform_report();

// Mouse capture (relative mode) for mouse aim. F10 releases / captures the
// mouse; while released, a click into the window captures it again (that
// click is not passed to the game); losing focus releases it. Starts
// captured unless SPLAT_MOUSE_CAPTURE=0.
// Applies the capture state; tv_view = false in the GamePad view (mouse =
// touch screen, never captured).
void splat_platform_apply_capture(bool tv_view);
// Handles capture-related window events. Returns true when the event is
// consumed and must not reach the game.
bool splat_platform_event(const SDL_Event& event, bool tv_view);
// True while mouse buttons must be ignored (the click that captured the
// mouse is still held).
bool splat_mouse_buttons_suppressed(uint32_t mouse_state);
