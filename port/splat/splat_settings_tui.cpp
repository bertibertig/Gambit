// The settings screen in the terminal (`Splatoon --settings`, or
// `scripts/run.sh --settings`): categories on the left, the selected
// category's options on the right, a description of the highlighted option
// below. Plain ANSI escapes (xterm-256 colours) and a raw terminal
// (termios), no curses. Layout and keys follow Amped2-Recomp's settings
// screen, the look Splatoon's menus (see "the look" below).
//
// Keys: Up/Down (or k/j, w) move, Left/Right (h/l, a/d) change a value or
// switch between the panes, Tab switches panes, Enter starts the game with
// the settings saved, S saves, Esc/Q leaves (asks when something is unsaved).
#include "splat_settings.h"

#include <SDL.h>
#include <algorithm>
#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <string>
#include <vector>

#if defined(_WIN32)
bool splat_settings_tui() {
    std::fprintf(stderr, "The settings screen is not available on Windows yet; edit %s\n",
                 splat_settings_path().c_str());
    return false;
}
#else
#include <poll.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

namespace {

// ---- terminal --------------------------------------------------------------

enum Key { K_NONE, K_UP, K_DOWN, K_LEFT, K_RIGHT, K_TAB, K_ENTER, K_ESC, K_SAVE, K_QUIT, K_OTHER };

termios s_old_term;

void term_raw() {
    tcgetattr(0, &s_old_term);
    termios t = s_old_term;
    t.c_lflag &= ~tcflag_t(ICANON | ECHO);
    t.c_cc[VMIN] = 1;
    t.c_cc[VTIME] = 0;
    tcsetattr(0, TCSANOW, &t);
}
void term_restore() { tcsetattr(0, TCSANOW, &s_old_term); }

void term_size(int& w, int& h) {
    winsize ws{};
    w = 80; h = 24;
    if (ioctl(1, TIOCGWINSZ, &ws) == 0 && ws.ws_col && ws.ws_row) { w = ws.ws_col; h = ws.ws_row; }
}

int read_byte(int ms) {
    pollfd p{0, POLLIN, 0};
    unsigned char c;
    if (poll(&p, 1, ms) <= 0 || read(0, &c, 1) != 1) return -1;
    return c;
}

Key read_key() {
    const int c = read_byte(-1);
    if (c == 27) {
        const int c2 = read_byte(30);
        if (c2 < 0) return K_ESC;
        if (c2 != '[' && c2 != 'O') return K_OTHER;
        int c3 = read_byte(30);
        switch (c3) {
        case 'A': return K_UP;
        case 'B': return K_DOWN;
        case 'C': return K_RIGHT;
        case 'D': return K_LEFT;
        default:
            while (c3 >= 0 && !(c3 >= 0x40 && c3 <= 0x7E)) c3 = read_byte(30);
            return K_OTHER;
        }
    }
    switch (c) {
    case '\t': return K_TAB;
    case '\r': case '\n': return K_ENTER;
    case 'k': case 'w': case 'K': case 'W': return K_UP;
    case 'j': case 'J': return K_DOWN;
    case 'h': case 'a': case 'H': case 'A': return K_LEFT;
    case 'l': case 'd': case 'L': case 'D': return K_RIGHT;
    case 's': case 'S': return K_SAVE;
    case 'q': case 'Q': return K_QUIT;
    case -1: return K_QUIT;  // stdin closed
    default: return K_OTHER;
    }
}

// ---- output ------------------------------------------------------------------

std::string s_out;
int s_x0 = 0;  // left edge of the screen (centred in wide terminals)

void out(const char* fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    const int n = std::vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    if (n > 0) s_out.append(buf, std::min<size_t>(size_t(n), sizeof buf - 1));
}
void flush_out() {
    std::fwrite(s_out.data(), 1, s_out.size(), stdout);
    std::fflush(stdout);
    s_out.clear();
}
void at(int row, int col) { out("\x1b[%d;%dH", row, s_x0 + col); }

// Terminal columns of the code point at p (CJK and Hangul take two), and its length in bytes.
int glyph(const char* p, int& bytes) {
    const auto u = [&](int i) { return static_cast<unsigned char>(p[i]); };
    unsigned cp = u(0);
    bytes = 1;
    if (cp >= 0xF0) { cp &= 0x07; bytes = 4; }
    else if (cp >= 0xE0) { cp &= 0x0F; bytes = 3; }
    else if (cp >= 0xC0) { cp &= 0x1F; bytes = 2; }
    for (int i = 1; i < bytes; ++i) {
        if ((u(i) & 0xC0) != 0x80) { bytes = i; return 1; }
        cp = (cp << 6) | (u(i) & 0x3F);
    }
    const bool wide = (cp >= 0x1100 && cp <= 0x115F) || (cp >= 0x2E80 && cp <= 0xA4CF) ||
                      (cp >= 0xAC00 && cp <= 0xD7A3) || (cp >= 0xF900 && cp <= 0xFAFF) ||
                      (cp >= 0xFF00 && cp <= 0xFF60) || (cp >= 0xFFE0 && cp <= 0xFFE6);
    return wide ? 2 : 1;
}

// s, cut or padded to exactly w columns.
void field(const char* s, int w) {
    int n = 0;
    for (const char* p = s; *p;) {
        int bytes;
        const int cols = glyph(p, bytes);
        if (n + cols > w) break;
        out("%.*s", bytes, p);
        p += bytes;
        n += cols;
    }
    for (; n < w; ++n) out(" ");
}

std::vector<std::string> wrap(const std::string& text, int w) {
    std::vector<std::string> lines;
    size_t pos = 0;
    while (pos < text.size()) {
        while (pos < text.size() && text[pos] == ' ') ++pos;
        if (text.size() - pos <= size_t(w)) { lines.push_back(text.substr(pos)); break; }
        size_t cut = pos + size_t(w);
        while (cut > pos && text[cut] != ' ') --cut;
        if (cut == pos) cut = pos + size_t(w);
        lines.push_back(text.substr(pos, cut - pos));
        pos = cut;
    }
    return lines;
}

// ---- the look ----------------------------------------------------------------
// Splatoon's: loud ink colours on a near-black lobby, ink dripping off the
// banner, chunky pill-shaped selections. Every category inks the screen in its
// own pair of team colours (Turf War pairings), so the banner, frame and
// highlight change colour as you move through the list.

struct Ink {
    int a, b;  // xterm-256 colours: main ink, the opposing team's ink
};
constexpr Ink kOrangeBlue{208, 33}, kPinkGreen{199, 118}, kYellowPurple{220, 99}, kTurquoiseOrange{44, 208},
    kLimePurple{154, 135}, kBlueOrange{39, 214};
constexpr int kBg = 234, kText = 253, kDim = 244, kInkFg = 16;

std::string fg(int c) { return "\x1b[38;5;" + std::to_string(c) + "m"; }
std::string bg(int c) { return "\x1b[48;5;" + std::to_string(c) + "m"; }
// Back to the lobby: plain text on the dark background.
const std::string& base() {
    static const std::string s = "\x1b[0m" + bg(kBg) + fg(kText);
    return s;
}
void paint(const std::string& s) { out("%s", s.c_str()); }

// A pill: ▐ text ▌ in ink, w columns wide in all.
void pill(const std::string& text, int w, int ink) {
    paint(base() + fg(ink));
    out("▐");
    paint(bg(ink) + fg(kInkFg) + "\x1b[1m");
    field(text.c_str(), w - 2);
    paint(base() + fg(ink));
    out("▌");
    paint(base());
}

// How far ink drips below the banner at a column: 0, 1 or 2 rows. Fixed
// pseudo-random, so the drips stay put between frames.
int drip(int col) {
    unsigned h = unsigned(col) * 2654435761u;
    h ^= h >> 13;
    h *= 0x5bd1e995u;
    h ^= h >> 15;
    const unsigned r = h % 100;
    return r < 9 ? 2 : r < 26 ? 1 : 0;
}

// An inkling in squid form, for the empty space under the list.
const char* const kSquid[] = {
    "     ▄█▄     ",
    "   ▄█████▄   ",
    " ▄█████████▄ ",
    "▀▀▀███████▀▀▀",
    "   █ ███ █   ",
    "   █▀█▀█▀█   ",
    "  ▄▀ █ █ ▀▄  ",
};

// ---- the model ---------------------------------------------------------------

struct Displays {
    int count = 0;
    std::vector<std::string> name;
} s_disp;

void query_displays() {
    const bool inited = SDL_WasInit(SDL_INIT_VIDEO) != 0;
    if (!inited && SDL_InitSubSystem(SDL_INIT_VIDEO) != 0) return;
    s_disp.count = std::max(0, std::min(SDL_GetNumVideoDisplays(), 16));
    for (int i = 0; i < s_disp.count; ++i) {
        SDL_DisplayMode m{};
        const char* nm = SDL_GetDisplayName(i);
        char buf[128];
        if (SDL_GetDesktopDisplayMode(i, &m) == 0)
            std::snprintf(buf, sizeof buf, "%.22s  %dx%d %d Hz", nm ? nm : "Display", m.w, m.h, m.refresh_rate);
        else
            std::snprintf(buf, sizeof buf, "%s", nm ? nm : "Display");
        s_disp.name.emplace_back(buf);
    }
    if (!inited) SDL_QuitSubSystem(SDL_INIT_VIDEO);
}

struct Option {
    const char* label;
    const char* help;
    int (*count)();
    std::string (*text)(int i);
    int (*get)();
    void (*set)(int i);
    const char* (*disabled)();  // nullptr or a reason why it cannot be changed now
};

struct Category {
    const char* title;
    const char* blurb;
    Ink ink;
    std::vector<Option> opts;
};

SplatSettings& S = g_splat_settings;

std::string fmt(const char* f, ...) {
    char buf[256];
    va_list ap;
    va_start(ap, f);
    std::vsnprintf(buf, sizeof buf, f, ap);
    va_end(ap);
    return buf;
}

// The language choices: "auto", then the game's languages. A configured
// language the game does not have stays listed so it is not lost silently.
std::vector<std::string>& language_choices() {
    static std::vector<std::string> v = [] {
        std::vector<std::string> c{"auto"};
        for (const auto& code : splat_languages_in_game()) c.push_back(code);
        if (std::find(c.begin(), c.end(), S.language) == c.end()) c.push_back(S.language);
        return c;
    }();
    return v;
}

const std::vector<Category>& categories() {
    static const std::vector<Category> cats = {
        {"Resolution Scaling", "Render the game above the Wii U's 1280x720.", kOrangeBlue,
         {{"Internal resolution",
           "The resolution the game is drawn at before it is scaled to the window. "
           "1x is the Wii U's 1280x720; every step multiplies both sides. Higher values "
           "sharpen edges and distant detail and cost GPU time (4x is 16 times the pixels "
           "of 1x per frame).",
           [] { return kSplatScaleMax; },
           [](int i) {
               const int h = 720 * (i + 1), w = int(h * splat_aspect_value(S.aspect_ratio) + 0.5);
               return fmt("%dx  (%d x %d)%s", i + 1, w, h, i == 0 ? "  original" : "");
           },
           [] { return S.resolution_scale - 1; }, [](int i) { S.resolution_scale = i + 1; }, nullptr}}},
        {"Aspect Ratio", "Widescreen and ultrawide views.", kPinkGreen,
         {{"Aspect ratio",
           "16:9 is the original. Wider ratios show more at the sides (Hor+); the HUD "
           "stays 16:9 in the middle.",
           [] { return int(SPLAT_ASPECT_COUNT); },
           [](int i) {
               static const char* const extra[SPLAT_ASPECT_COUNT] = {"original", "", "ultrawide",
                                                                     "super ultrawide"};
               return fmt("%s  %s", splat_aspect_name(i), extra[i]);
           },
           [] { return S.aspect_ratio; }, [](int i) { S.aspect_ratio = i; },
           []() -> const char* { return "not implemented yet – stays 16:9"; }}}},
        {"Framerate", "Frame pacing and tearing.", kYellowPurple,
         {{"V-Sync",
           "Off: frames are shown as soon as they are ready, without tearing (mailbox) – "
           "the lowest latency. On: every frame waits for the monitor's refresh (FIFO), "
           "the smoothest pacing on fixed-refresh monitors. The game itself always runs "
           "at its original 60 frames per second.",
           [] { return 2; }, [](int i) { return std::string(i ? "On  (FIFO)" : "Off  (mailbox, no tearing)"); },
           [] { return int(S.vsync); }, [](int i) { S.vsync = i != 0; }, nullptr}}},
        {"Display", "Window mode and monitor.", kTurquoiseOrange,
         {{"Window mode",
           "Windowed: a resizable window. Fullscreen: fullscreen on the monitor below. "
           "Borderless fullscreen: a window that covers the monitor, quick to Alt+Tab away "
           "from. F11 switches between windowed and fullscreen in game.",
           [] { return int(SPLAT_WINDOW_COUNT); },
           [](int i) {
               static const char* const names[SPLAT_WINDOW_COUNT] = {"Windowed", "Fullscreen",
                                                                     "Borderless fullscreen"};
               return std::string(names[i]);
           },
           [] { return S.window_mode; }, [](int i) { S.window_mode = i; }, nullptr},
          {"Monitor", "Which monitor fullscreen and borderless fullscreen use.",
           [] { return std::max(1, s_disp.count); },
           [](int i) {
               return s_disp.count ? fmt("%d: %s", i + 1, s_disp.name[size_t(i)].c_str())
                                   : fmt("%d: (no display information)", i + 1);
           },
           [] { return S.display < std::max(1, s_disp.count) ? S.display : 0; },
           [](int i) { S.display = i; },
           [] { return S.window_mode == SPLAT_WINDOWED ? "only for fullscreen / borderless" : nullptr; }}}},
        {"Language", "The language of the game's text.", kLimePurple,
         {{"Game language",
           "The Wii U system language the game reads at start-up; it picks the language of "
           "all of the game's text, menus and dialogue. Auto follows your "
           "system language (LANG) when the game has it, else English. Only the languages "
           "this copy of the game has text for are listed (the European version: English, "
           "German, French, Spanish, Italian). For one run: --language <code>.",
           [] { return int(language_choices().size()); },
           [](int i) {
               const std::string& code = language_choices()[size_t(i)];
               if (code == "auto") return fmt("Auto  (system: %s)", splat_language_resolve("auto").native);
               const SplatLanguage* l = splat_language_find(code);
               const auto& have = splat_languages_in_game();
               const bool in_game = std::find(have.begin(), have.end(), code) != have.end();
               if (l && !std::strcmp(l->native, l->english))
                   return fmt("%s%s", l->native, in_game ? "" : "  (not in this game)");
               return fmt("%s  (%s)%s", l ? l->native : code.c_str(), l ? l->english : "?",
                          in_game ? "" : "  not in this game");
           },
           [] {
               const auto& c = language_choices();
               return int(std::find(c.begin(), c.end(), S.language) - c.begin());
           },
           [](int i) { S.language = language_choices()[size_t(i)]; }, nullptr}}},
    };
    return cats;
}

enum { ACT_START, ACT_SAVE, ACT_QUIT, NACTS };
const char* const kActs[NACTS] = {"Save and start the game", "Save", "Quit"};
constexpr Ink kActInk[NACTS] = {kOrangeBlue, kBlueOrange, kPinkGreen};

struct Ui {
    int cat = 0;  // 0..ncats-1 categories, ncats.. actions
    int opt = 0;
    int pane = 0;  // 0 left, 1 right
    bool dirty = false;
    std::string status;
};

Ink ink_of(int cat) {
    const auto& cats = categories();
    return cat < int(cats.size()) ? cats[size_t(cat)].ink : kActInk[cat - int(cats.size())];
}

std::string upper(const char* s) {
    std::string r(s);
    for (char& c : r) c = char(std::toupper(static_cast<unsigned char>(c)));
    return r;
}

void hline(const char* left, const char* mid, const char* right, int w, int split) {
    out("%s", left);
    for (int i = 1; i < w - 1; ++i) out("%s", (split && i == split) ? mid : "─");
    out("%s", right);
}

void draw(const Ui& u) {
    const auto& cats = categories();
    const int ncats = int(cats.size());
    const Ink ink = ink_of(u.cat);
    int TW, H;
    term_size(TW, H);
    const int W = std::min(TW, 110), lw = 28, fy = 4;  // fy: the frame's top row
    s_x0 = 0;
    out("\x1b[0m\x1b[H\x1b[2J");
    if (W < 60 || H < 22) {
        out("Please make the terminal at least 60x22 (now %dx%d).", TW, H);
        flush_out();
        return;
    }
    // The lobby: the whole terminal dark, the screen centred on it.
    for (int r = 1; r <= H; ++r) {
        out("\x1b[%d;1H", r);
        paint(base());
        out("%*s", TW, "");
    }
    s_x0 = (TW - W) / 2;
    const int rw = W - lw - 3;

    // Banner, with ink dripping off it.
    at(1, 1);
    paint(bg(ink.a) + fg(kInkFg) + "\x1b[1m");
    const char* tag = "Stay fresh!  ";
    field("  ◢◤ SPLATOON RECOMPILED  ·  SETTINGS", W - int(std::strlen(tag)));
    out("%s", tag);
    at(2, 1);
    paint(base() + fg(ink.a));
    for (int c = 1; c <= W; ++c) out("%s", drip(c) ? "█" : "▀");
    at(3, 1);
    for (int c = 1; c <= W; ++c) out("%s", drip(c) == 2 ? "▀" : (drip(c + 3) == 2 && c % 7 == 0) ? "•" : " ");

    // Frame in the other team's ink.
    paint(base() + fg(ink.b));
    at(fy, 1); hline("╭", "┬", "╮", W, lw + 1);
    for (int row = fy + 1; row < H - 3; ++row) {
        at(row, 1); out("│");
        at(row, lw + 2); out("│");
        at(row, W); out("│");
    }
    at(H - 3, 1); hline("├", "┴", "┤", W, lw + 1);
    at(H - 2, 1); out("│"); at(H - 2, W); out("│");
    at(H - 1, 1); hline("╰", "─", "╯", W, 0);
    paint(base());

    // Left pane: categories, then the actions; each wears its own ink.
    int row = fy + 2;
    at(fy + 1, 3); paint(base() + fg(ink.b) + "\x1b[1m"); out("MODES"); paint(base());
    for (int i = 0; i < ncats + NACTS; ++i) {
        const bool sel = i == u.cat;
        const Ink in = ink_of(i);
        const char* name = i < ncats ? cats[size_t(i)].title : kActs[i - ncats];
        if (i == ncats) ++row;
        at(row++, 2);
        if (sel && u.pane == 0) {
            pill(fmt("▶ %s", name), lw, in.a);
        } else if (sel) {
            paint(base() + fg(in.a) + "\x1b[1m");
            field(fmt(" ▶ %s", name).c_str(), lw);
        } else {
            paint(base() + fg(in.a));
            out(" ●");
            paint(base() + fg(i < ncats ? kText : kDim));
            field(fmt(" %s", name).c_str(), lw - 2);
        }
        paint(base());
    }
    if (row + 1 + int(std::size(kSquid)) < H - 3) {
        const int sy = H - 4 - int(std::size(kSquid));
        paint(base() + fg(ink.a));
        for (size_t i = 0; i < std::size(kSquid); ++i) {
            at(sy + int(i), 2 + (lw - 13) / 2);
            out("%s", kSquid[i]);
        }
        paint(base());
    }

    // Right pane: the category's options.
    row = fy + 1;
    const int x = lw + 4;
    if (u.cat < ncats) {
        const auto& c = cats[size_t(u.cat)];
        at(row++, x); paint(base() + fg(ink.a) + "\x1b[1m"); out("%s", upper(c.title).c_str());
        at(row++, x); paint(base() + fg(kDim)); field(c.blurb, rw - 2); paint(base());
        ++row;
        for (size_t i = 0; i < c.opts.size(); ++i) {
            const auto& o = c.opts[i];
            const bool sel = u.pane == 1 && int(i) == u.opt;
            const char* why = o.disabled ? o.disabled() : nullptr;
            const int v = o.get(), n = o.count(), lab = 22;
            const int pips = n >= 3 && n <= 8 ? n * 2 + 1 : 0;
            const std::string val = fmt("%s %s %s", v > 0 ? "◀" : " ", o.text(v).c_str(), v < n - 1 ? "▶" : " ");
            at(row, lw + 3);
            if (sel) {
                std::string line = fmt(" %-20s", o.label);
                pill(line + val, rw - pips, why ? kDim : ink.a);
            } else {
                paint(base() + fg(why ? kDim : kText));
                field(fmt("  %s", o.label).c_str(), lab);
                paint(base() + fg(why ? kDim : ink.a) + "\x1b[1m");
                field(val.c_str(), rw - lab - pips);
            }
            if (pips) {  // a gauge, like the weapon stats in the shop
                paint(base() + " ");
                for (int p = 0; p < n; ++p) {
                    paint(base() + fg(why ? kDim : p <= v ? ink.a : 239));
                    out("%s", p <= v ? "■ " : "□ ");
                }
            }
            paint(base());
            ++row;
            if (why) {
                at(row++, lw + 3 + lab);
                paint(base() + fg(kDim)); out("  (%s)", why); paint(base());
            }
            ++row;
        }
        if (u.pane == 1 || c.opts.size() == 1) {
            ++row;
            for (const auto& line : wrap(c.opts[size_t(u.pane == 1 ? u.opt : 0)].help, rw - 5)) {
                if (row >= H - 4) break;
                at(row++, x);
                paint(base() + fg(ink.b)); out("▌ ");
                paint(base() + fg(250)); out("%s", line.c_str());
            }
            paint(base());
        }
    } else {
        const int act = u.cat - ncats;
        const char* what = act == ACT_START ? "Saves the settings and starts the game with them. Booyah!"
                           : act == ACT_SAVE ? "Saves the settings and stays here."
                                             : "Leaves without starting the game.";
        at(row++, x); paint(base() + fg(ink.a) + "\x1b[1m"); out("%s", upper(kActs[act]).c_str());
        at(row + 1, x); paint(base() + fg(250)); out("%s", what);
        at(row + 3, x); paint(base() + fg(kDim)); out("File: "); paint(base());
        field(splat_settings_path().c_str(), rw - 8);
    }

    // Status line: the keys as coloured buttons.
    at(H - 2, 3);
    if (!u.status.empty()) {
        paint(base() + fg(220) + "\x1b[1m");
        field(u.status.c_str(), W - 4);
    } else {
        static const struct { const char* key; const char* what; int ink; } keys[] = {
            {"↑↓", "move", 208}, {"←→", "change", 39}, {"Tab", "pane", 199},
            {"Enter", "save & start", 118}, {"S", "save", 220}, {"Q", "quit", 44}};
        int col = 3;
        const int room = W - 2 - (u.dirty ? 10 : 0);
        for (const auto& k : keys) {
            int kb, wk = 0;  // columns of the key's label
            for (const char* p = k.key; *p; p += kb) wk += glyph(p, kb);
            col += wk + 2 + int(std::strlen(k.what)) + 3;
            if (col > room) break;
            paint(base() + bg(k.ink) + fg(kInkFg) + "\x1b[1m");
            out(" %s ", k.key);
            paint(base());
            out(" %s  ", k.what);
        }
        if (u.dirty) { paint(base() + fg(199) + "\x1b[1m"); out(" ● unsaved"); }
    }
    paint(base());
    at(H, 1);
    flush_out();
}

// ---- main loop -----------------------------------------------------------------

const Option* current_option(const Ui& u) {
    const auto& cats = categories();
    if (u.cat >= int(cats.size())) return nullptr;
    return &cats[size_t(u.cat)].opts[size_t(u.opt)];
}

bool option_enabled(const Option& o) { return !o.disabled || !o.disabled(); }

void change(Ui& u, int dir) {
    const Option* o = current_option(u);
    if (!o) return;
    const int v = o->get() + dir;
    if (v < 0 || v >= o->count()) return;
    // A disabled option may still be stepped back towards its working value.
    if (!option_enabled(*o) && o->get() == 0) return;
    o->set(v);
    u.dirty = true;
}

bool save(Ui& u) {
    if (!splat_settings_save()) {
        u.status = "Could not write " + splat_settings_path();
        return false;
    }
    u.dirty = false;
    u.status = "Saved to " + splat_settings_path();
    return true;
}

}  // namespace

bool splat_settings_tui() {
    if (!isatty(0) || !isatty(1)) {
        std::fprintf(stderr, "--settings needs a terminal\n");
        return false;
    }
    Ui u;
    int result = -1;
    bool confirm_quit = false;
    const int ncats = int(categories().size());
    splat_settings_load_file_only();
    query_displays();
    if (s_disp.count && g_splat_settings.display >= s_disp.count) g_splat_settings.display = 0;

    term_raw();
    out("\x1b[?1049h\x1b[?25l");  // alternate screen, hide cursor
    const char* unsaved = "Unsaved changes – Q/Esc/Enter: quit without saving, S: save and quit, other key: stay";
    while (result < 0) {
        draw(u);
        const Key k = read_key();
        if (k != K_OTHER && k != K_NONE) u.status.clear();
        if (confirm_quit) {
            confirm_quit = false;
            if (k == K_QUIT || k == K_ESC || k == K_ENTER) result = 0;
            else if (k == K_SAVE && save(u)) result = 0;
            continue;
        }
        switch (k) {
        case K_UP:
            if (u.pane == 0) { if (u.cat > 0) { --u.cat; u.opt = 0; } }
            else if (u.opt > 0) --u.opt;
            break;
        case K_DOWN:
            if (u.pane == 0) { if (u.cat < ncats + NACTS - 1) { ++u.cat; u.opt = 0; } }
            else if (u.opt < int(categories()[size_t(u.cat)].opts.size()) - 1) ++u.opt;
            break;
        case K_LEFT:
            if (u.pane == 1) {
                const Option* o = current_option(u);
                if (o->get() == 0) u.pane = 0;  // at the left end: back to the list
                else change(u, -1);
            }
            break;
        case K_RIGHT:
            if (u.pane == 0) { if (u.cat < ncats) u.pane = 1; }
            else change(u, +1);
            break;
        case K_TAB:
            if (u.cat < ncats) u.pane ^= 1;
            break;
        case K_ENTER:
            if (u.cat < ncats) {
                if (u.pane == 0) { u.pane = 1; break; }
                if (save(u)) result = 1;
            } else if (u.cat == ncats + ACT_START) {
                if (save(u)) result = 1;
            } else if (u.cat == ncats + ACT_SAVE) {
                save(u);
            } else if (u.dirty) {
                u.status = unsaved;
                confirm_quit = true;
            } else {
                result = 0;
            }
            break;
        case K_SAVE:
            save(u);
            break;
        case K_ESC:
            if (u.pane == 1) { u.pane = 0; break; }
            [[fallthrough]];
        case K_QUIT:
            if (u.dirty) { u.status = unsaved; confirm_quit = true; }
            else result = 0;
            break;
        default:
            break;
        }
    }
    out("\x1b[0m\x1b[?25h\x1b[?1049l");
    flush_out();
    term_restore();
    return result == 1;
}
#endif
