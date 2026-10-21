#include "splat_platform.h"

#include "splat_settings.h"

#include <SDL.h>
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>

namespace {
constexpr const char* kAppId = "splatoon-recomp";

std::atomic<bool> s_capture_wanted{[] {
    const char* v = std::getenv("SPLAT_MOUSE_CAPTURE");
    return !(v && v[0] == '0');
}()};
std::atomic<bool> s_suppress_buttons{false};

bool window_focused() {
    SDL_Window* w = SDL_GetKeyboardFocus();
    return w && (SDL_GetWindowFlags(w) & SDL_WINDOW_INPUT_FOCUS);
}
}  // namespace

void splat_platform_init() {
    splat_settings_load();
#if defined(__linux__)
    // SDL2 picks X11 (XWayland) first on a Wayland session. Prefer native
    // Wayland and keep X11 as the fallback; an explicit SDL_VIDEODRIVER wins.
    if (!std::getenv("SDL_VIDEODRIVER"))
        SDL_SetHint(SDL_HINT_VIDEODRIVER, "wayland,x11");
    // Wayland app id / X11 WM_CLASS, so KDE groups the window and can find a
    // splatoon-recomp.desktop entry.
    setenv("SDL_VIDEO_WAYLAND_WMCLASS", kAppId, 0);
    setenv("SDL_VIDEO_X11_WMCLASS", kAppId, 0);
#endif
    SDL_SetHint(SDL_HINT_APP_NAME, "Splatoon Recompiled");
    // Relative mouse motion as the device reports it: no OS acceleration.
    // (Wayland: unaccelerated relative-pointer deltas, X11: XInput2 raw motion.)
    SDL_SetHint(SDL_HINT_MOUSE_RELATIVE_SYSTEM_SCALE, "0");
    SDL_SetHint(SDL_HINT_MOUSE_RELATIVE_MODE_WARP, "0");
}

uint32_t splat_platform_window_flags() {
    return SDL_WINDOW_ALLOW_HIGHDPI;
}

void splat_platform_window_rect(int& x, int& y, int& w, int& h, uint32_t& flags) {
    const auto& s = g_splat_settings;
    int display = s.display;
    if (display < 0 || display >= SDL_GetNumVideoDisplays()) display = 0;
    SDL_Rect bounds{0, 0, 1920, 1080};
    SDL_GetDisplayBounds(display, &bounds);
    x = int(SDL_WINDOWPOS_CENTERED_DISPLAY(display));
    y = int(SDL_WINDOWPOS_CENTERED_DISPLAY(display));
    if (s.window_mode == SPLAT_WINDOWED) {
        // 16:9 window that fits the monitor: 1920x1080 or 80 % of the screen.
        w = std::min(1920, bounds.w * 4 / 5);
        h = std::min(w * 9 / 16, bounds.h * 4 / 5);
        w = h * 16 / 9;
        return;
    }
    w = bounds.w;
    h = bounds.h;
    flags |= s.window_mode == SPLAT_FULLSCREEN ? SDL_WINDOW_FULLSCREEN : SDL_WINDOW_FULLSCREEN_DESKTOP;
}

void splat_platform_report() {
    const char* driver = SDL_GetCurrentVideoDriver();
    std::fprintf(stderr, "[splat] video driver: %s\n", driver ? driver : "(none)");
}

void splat_platform_apply_capture(bool tv_view) {
    const bool on = tv_view && s_capture_wanted.load() && window_focused();
    if (bool(SDL_GetRelativeMouseMode()) != on) SDL_SetRelativeMouseMode(on ? SDL_TRUE : SDL_FALSE);
}

bool splat_platform_event(const SDL_Event& event, bool tv_view) {
    switch (event.type) {
    case SDL_KEYDOWN:
        if (event.key.keysym.scancode == SDL_SCANCODE_F10 && !event.key.repeat) {
            s_capture_wanted = !SDL_GetRelativeMouseMode();
            splat_platform_apply_capture(tv_view);
            std::fprintf(stderr, "[splat] mouse %s\n", SDL_GetRelativeMouseMode() ? "captured" : "released (F10 or click to capture)");
            return true;
        }
        break;
    case SDL_MOUSEBUTTONDOWN:
        if (tv_view && !SDL_GetRelativeMouseMode()) {
            s_capture_wanted = true;
            s_suppress_buttons = true;  // this click only captures the mouse
            splat_platform_apply_capture(tv_view);
            return true;
        }
        break;
    case SDL_WINDOWEVENT:
        if (event.window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
            if (SDL_GetRelativeMouseMode()) SDL_SetRelativeMouseMode(SDL_FALSE);
        } else if (event.window.event == SDL_WINDOWEVENT_FOCUS_GAINED) {
            splat_platform_apply_capture(tv_view);
        }
        break;
    default:
        break;
    }
    return false;
}

bool splat_mouse_buttons_suppressed(uint32_t mouse_state) {
    if (s_suppress_buttons && mouse_state == 0) s_suppress_buttons = false;
    return s_suppress_buttons;
}
