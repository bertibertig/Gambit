#include "splat_input.h"
#include "splat_platform.h"

#include "screen_mode.h"  // Gambit: g_touch_state (GamePad touch screen)

#include <SDL.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

namespace {

// VPADStatus.hold bits (same values as Gambit's vpad.cpp).
enum : uint32_t {
    kA = 0x8000, kB = 0x4000, kX = 0x2000, kY = 0x1000,
    kLeft = 0x0800, kRight = 0x0400, kUp = 0x0200, kDown = 0x0100,
    kZL = 0x0080, kZR = 0x0040, kL = 0x0020, kR = 0x0010,
    kPlus = 0x0008, kMinus = 0x0004,
    kStickR = 0x00020000, kStickL = 0x00040000,
};

struct KeyButton { SDL_Scancode key; uint32_t bit; };
// Static PC layout (README.md "Controls"). Splatoon (Wii U): ZR shoot,
// ZL squid form, R sub weapon, R-stick click special, X jump, Y reset camera,
// A/B confirm/back, D-pad signals.
constexpr KeyButton kKeys[] = {
    {SDL_SCANCODE_LSHIFT, kZL},     {SDL_SCANCODE_SPACE, kX},
    {SDL_SCANCODE_Q, kStickR},      {SDL_SCANCODE_C, kY},
    {SDL_SCANCODE_E, kA},           {SDL_SCANCODE_RETURN, kA},
    {SDL_SCANCODE_KP_ENTER, kA},    {SDL_SCANCODE_BACKSPACE, kB},
    {SDL_SCANCODE_ESCAPE, kB},      {SDL_SCANCODE_UP, kUp},
    {SDL_SCANCODE_DOWN, kDown},     {SDL_SCANCODE_LEFT, kLeft},
    {SDL_SCANCODE_RIGHT, kRight},   {SDL_SCANCODE_F, kL},
    {SDL_SCANCODE_Z, kStickL},      {SDL_SCANCODE_P, kPlus},
    {SDL_SCANCODE_M, kMinus},       {SDL_SCANCODE_MINUS, kMinus},
};
struct MouseButton { uint32_t mask; uint32_t bit; };
constexpr MouseButton kMouse[] = {
    {SDL_BUTTON_LMASK, kZR}, {SDL_BUTTON_RMASK, kR}, {SDL_BUTTON_MMASK, kStickR},
};

float env_float(const char* name, float fallback) {
    const char* v = std::getenv(name);
    if (!v || !*v) return fallback;
    char* end = nullptr;
    const float f = std::strtof(v, &end);
    return end != v ? f : fallback;
}

// ---------------------------------------------------------------------------
// SPLAT_INPUT_SCRIPT: timed virtual input for automated tests, e.g.
//   SPLAT_INPUT_SCRIPT="8000:a,9000:a/500,20000:yaw=45,21000:pitch=-10,22000:move=up/1500"
// Times are milliseconds after the first VPADRead; "/n" holds for n ms
// (default 120). Buttons: a b x y l r zl zr plus minus up down left right l3 r3.
// yaw=/pitch= add degrees of GamePad rotation once; move=up|down|left|right
// and look=... hold the left/right stick; touch=x;y touches the GamePad
// screen (854x480); key=tab presses a window key once (Tab = TV/GamePad
// view, F11 = fullscreen). "@file" reads the list from a file.
// ---------------------------------------------------------------------------
struct ScriptEvent {
    uint32_t start_ms = 0, hold_ms = 120;
    uint32_t bit = 0;
    float yaw = 0, pitch = 0;
    int stick = -1;  // 0 = left, 1 = right
    float sx = 0, sy = 0;
    int touch_x = -1, touch_y = -1;  // GamePad screen 0..853 x 0..479
    SDL_Scancode key = SDL_SCANCODE_UNKNOWN;  // key=<name>: a window key press (e.g. tab)
    bool fired = false, released = false;
};

uint32_t button_bit(const std::string& name) {
    static const struct { const char* n; uint32_t b; } table[] = {
        {"a", kA}, {"b", kB}, {"x", kX}, {"y", kY}, {"l", kL}, {"r", kR},
        {"zl", kZL}, {"zr", kZR}, {"plus", kPlus}, {"minus", kMinus},
        {"up", kUp}, {"down", kDown}, {"left", kLeft}, {"right", kRight},
        {"l3", kStickL}, {"r3", kStickR},
    };
    for (const auto& e : table) if (name == e.n) return e.b;
    return 0;
}

std::vector<ScriptEvent> parse_script(std::string text) {
    std::vector<ScriptEvent> events;
    if (!text.empty() && text[0] == '@') {
        std::string content;
        if (FILE* f = std::fopen(text.c_str() + 1, "rb")) {
            char buf[4096];
            size_t n;
            while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) content.append(buf, n);
            std::fclose(f);
        }
        text = content;
    }
    std::replace(text.begin(), text.end(), '\n', ',');
    size_t pos = 0;
    while (pos < text.size()) {
        size_t comma = text.find(',', pos);
        if (comma == std::string::npos) comma = text.size();
        std::string item = text.substr(pos, comma - pos);
        pos = comma + 1;
        item.erase(std::remove_if(item.begin(), item.end(), ::isspace), item.end());
        const size_t colon = item.find(':');
        if (item.empty() || colon == std::string::npos) continue;
        ScriptEvent ev;
        ev.start_ms = uint32_t(std::strtoul(item.c_str(), nullptr, 10));
        std::string action = item.substr(colon + 1);
        if (const size_t slash = action.find('/'); slash != std::string::npos) {
            ev.hold_ms = uint32_t(std::strtoul(action.c_str() + slash + 1, nullptr, 10));
            action.resize(slash);
        }
        if (action.rfind("key=", 0) == 0) {
            ev.key = SDL_GetScancodeFromName(action.c_str() + 4);
            if (ev.key == SDL_SCANCODE_UNKNOWN) continue;
        } else if (action.rfind("touch=", 0) == 0) {
            ev.touch_x = std::atoi(action.c_str() + 6);
            const size_t semi = action.find(';');
            ev.touch_y = semi == std::string::npos ? 0 : std::atoi(action.c_str() + semi + 1);
        } else if (action.rfind("yaw=", 0) == 0) ev.yaw = std::strtof(action.c_str() + 4, nullptr);
        else if (action.rfind("pitch=", 0) == 0) ev.pitch = std::strtof(action.c_str() + 6, nullptr);
        else if (action.rfind("move=", 0) == 0 || action.rfind("look=", 0) == 0) {
            ev.stick = action[0] == 'm' ? 0 : 1;
            const std::string dir = action.substr(5);
            ev.sx = dir == "left" ? -1.f : dir == "right" ? 1.f : 0.f;
            ev.sy = dir == "down" ? -1.f : dir == "up" ? 1.f : 0.f;
        } else if (!(ev.bit = button_bit(action))) {
            std::fprintf(stderr, "[splat] input script: unknown action '%s'\n", action.c_str());
            continue;
        }
        events.push_back(ev);
    }
    std::fprintf(stderr, "[splat] input script: %zu events\n", events.size());
    return events;
}

void apply_script(SplatPadInput& out) {
    static std::mutex lock;
    static std::vector<ScriptEvent> events;
    static bool loaded = false;
    static std::chrono::steady_clock::time_point t0;
    std::lock_guard guard(lock);
    if (!loaded) {
        loaded = true;
        if (const char* s = std::getenv("SPLAT_INPUT_SCRIPT"); s && *s) events = parse_script(s);
        t0 = std::chrono::steady_clock::now();
    }
    if (events.empty()) return;
    const auto now = uint32_t(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - t0).count());
    for (auto& ev : events) {
        if (now < ev.start_ms) continue;
        if (ev.yaw != 0 || ev.pitch != 0) {
            if (!ev.fired) {
                out.yaw_deg += ev.yaw;
                out.pitch_deg += ev.pitch;
                ev.fired = true;
                std::fprintf(stderr, "[splat] input script t=%u: yaw %+g pitch %+g\n", now, ev.yaw, ev.pitch);
            }
            continue;
        }
        if (now >= ev.start_ms + ev.hold_ms) {
            if (ev.touch_x >= 0 && ev.fired && !ev.released) {
                g_touch_state.touched = false;
                ev.released = true;
            }
            continue;
        }
        if (!ev.fired) {
            ev.fired = true;
            std::fprintf(stderr, "[splat] input script t=%u: event at %u ms\n", now, ev.start_ms);
            if (ev.key != SDL_SCANCODE_UNKNOWN) {
                // Handled by the window thread's event loop like a real key.
                SDL_Event e{};
                e.type = SDL_KEYDOWN;
                e.key.state = SDL_PRESSED;
                e.key.keysym.scancode = ev.key;
                e.key.keysym.sym = SDL_GetKeyFromScancode(ev.key);
                SDL_PushEvent(&e);
            }
        }
        out.buttons |= ev.bit;
        if (ev.touch_x >= 0)
            g_touch_state = {true, uint16_t(ev.touch_x), uint16_t(ev.touch_y)};
        if (ev.stick == 0) { out.lx = ev.sx; out.ly = ev.sy; }
        if (ev.stick == 1) { out.rx = ev.sx; out.ry = ev.sy; }
    }
}

}  // namespace

float splat_input_sensitivity() {
    // Degrees of GamePad rotation per mouse count. 0.05 at 800 dpi: one inch
    // of mouse travel turns the pad by 40 degrees.
    static const float sens = env_float("SPLAT_MOUSE_SENS", 0.05f);
    return sens;
}

bool splat_input_invert_y() {
    static const bool invert = env_float("SPLAT_INVERT_Y", 0) != 0;
    return invert;
}

bool splat_input_mouse_aim() {
    static const bool on = env_float("SPLAT_MOUSE_AIM", 1) != 0;
    return on;
}

void splat_input_read(SplatPadInput& out, bool use_mouse) {
    out = {};
    // SDL's event pump runs on the window thread (GX2SwapScanBuffers); the
    // keyboard/mouse state read here is what it last saw.
    const Uint8* keys = SDL_GetKeyboardState(nullptr);
    const Uint32 mouse = use_mouse ? SDL_GetMouseState(nullptr, nullptr) : 0;
    int dx = 0, dy = 0;
    SDL_GetRelativeMouseState(&dx, &dy);  // always consume, also while unused

    if (keys) {
        for (const auto& k : kKeys) if (keys[k.key]) out.buttons |= k.bit;
        // WASD = left stick at full deflection (diagonals normalised);
        // hold Left Alt to walk (half deflection).
        const float x = float(int(keys[SDL_SCANCODE_D]) - int(keys[SDL_SCANCODE_A]));
        const float y = float(int(keys[SDL_SCANCODE_W]) - int(keys[SDL_SCANCODE_S]));
        if (x != 0 || y != 0) {
            const float scale = (keys[SDL_SCANCODE_LALT] ? 0.5f : 1.0f) / std::sqrt(x * x + y * y);
            out.lx = x * scale;
            out.ly = y * scale;
        }
        // I J K L = right stick, for menus that rotate or scroll with it.
        out.rx = float(int(keys[SDL_SCANCODE_L]) - int(keys[SDL_SCANCODE_J]));
        out.ry = float(int(keys[SDL_SCANCODE_I]) - int(keys[SDL_SCANCODE_K]));
    }
    // Mouse buttons count only while the mouse is captured (a click into the
    // released window captures it and is not a shot).
    if (use_mouse && SDL_GetRelativeMouseMode() && !splat_mouse_buttons_suppressed(mouse))
        for (const auto& m : kMouse) if (mouse & m.mask) out.buttons |= m.bit;

    if (use_mouse && SDL_GetRelativeMouseMode()) {
        const float sens = splat_input_sensitivity();
        out.yaw_deg = float(dx) * sens;
        out.pitch_deg = float(-dy) * sens * (splat_input_invert_y() ? -1.f : 1.f);
    }
    apply_script(out);
    out.keyboard_active = out.buttons || out.lx || out.ly || out.rx || out.ry ||
                          out.yaw_deg || out.pitch_deg;
}
