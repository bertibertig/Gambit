#include "splat_settings.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>

SplatSettings g_splat_settings;

namespace {

const char* const kAspectNames[SPLAT_ASPECT_COUNT] = {"16:9", "16:10", "21:9", "32:9"};
const double kAspectValues[SPLAT_ASPECT_COUNT] = {16.0 / 9, 16.0 / 10, 21.0 / 9, 32.0 / 9};
const char* const kWindowNames[SPLAT_WINDOW_COUNT] = {"windowed", "fullscreen", "borderless"};

std::string trim(std::string s) {
    const auto a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return {};
    const auto b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

int parse_int(const std::string& v, int fallback) {
    char* end = nullptr;
    const long n = std::strtol(v.c_str(), &end, 10);
    return end != v.c_str() ? int(n) : fallback;
}

bool parse_bool(const std::string& v, bool fallback) {
    if (v == "1" || v == "on" || v == "true" || v == "yes") return true;
    if (v == "0" || v == "off" || v == "false" || v == "no") return false;
    return fallback;
}

void set_value(SplatSettings& s, const std::string& key, const std::string& value) {
    if (key == "resolution_scale") {
        s.resolution_scale = std::clamp(parse_int(value, s.resolution_scale), 1, kSplatScaleMax);
    } else if (key == "aspect_ratio") {
        for (int i = 0; i < SPLAT_ASPECT_COUNT; ++i)
            if (value == kAspectNames[i]) s.aspect_ratio = i;
    } else if (key == "vsync") {
        s.vsync = parse_bool(value, s.vsync);
    } else if (key == "window_mode") {
        for (int i = 0; i < SPLAT_WINDOW_COUNT; ++i)
            if (value == kWindowNames[i]) s.window_mode = i;
    } else if (key == "display") {
        s.display = std::max(0, parse_int(value, s.display));
    } else if (key == "language") {
        std::string v;
        for (char c : value) v += char(std::tolower(static_cast<unsigned char>(c)));
        if (v == "auto" || splat_language_find(v)) s.language = v;
        else std::fprintf(stderr, "[splat] unknown language '%s', keeping %s\n", value.c_str(), s.language.c_str());
    }
}

const char* const kKeys[] = {"resolution_scale", "aspect_ratio", "vsync", "window_mode", "display", "language"};

// The language part of a POSIX locale ("de_DE.UTF-8" -> "de"), "" for C/POSIX.
std::string locale_language() {
    for (const char* var : {"LC_ALL", "LC_MESSAGES", "LANG"}) {
        const char* v = std::getenv(var);
        if (!v || !*v) continue;
        std::string l(v);
        if (l == "C" || l.rfind("C.", 0) == 0 || l == "POSIX") return {};
        if (l.rfind("zh_TW", 0) == 0 || l.rfind("zh_HK", 0) == 0) return "tw";
        return l.substr(0, l.find_first_of("_.@"));
    }
    return {};
}

void read_file(SplatSettings& s) {
    std::ifstream in(splat_settings_path());
    std::string line;
    while (std::getline(in, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#' || line[0] == ';' || line[0] == '[') continue;
        const auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        set_value(s, trim(line.substr(0, eq)), trim(line.substr(eq + 1)));
    }
}

void read_env(SplatSettings& s) {
    for (const char* key : kKeys) {
        std::string name = "SPLAT_";
        for (const char* p = key; *p; ++p) name += char(std::toupper(static_cast<unsigned char>(*p)));
        if (const char* v = std::getenv(name.c_str()); v && *v) set_value(s, key, v);
    }
}

}  // namespace

const std::vector<SplatLanguage>& splat_languages() {
    static const std::vector<SplatLanguage> langs = {
        {"en", 1, "English", "English"},     {"de", 3, "Deutsch", "German"},
        {"fr", 2, "Français", "French"},     {"es", 5, "Español", "Spanish"},
        {"it", 4, "Italiano", "Italian"},    {"nl", 8, "Nederlands", "Dutch"},
        {"pt", 9, "Português", "Portuguese"}, {"ru", 10, "Русский", "Russian"},
        {"ja", 0, "日本語", "Japanese"},     {"zh", 6, "简体中文", "Chinese"},
        {"ko", 7, "한국어", "Korean"},       {"tw", 11, "繁體中文", "Traditional Chinese"},
    };
    return langs;
}

const SplatLanguage* splat_language_find(const std::string& code) {
    for (const auto& l : splat_languages())
        if (code == l.code) return &l;
    return nullptr;
}

const std::vector<std::string>& splat_languages_in_game() {
    static const std::vector<std::string> codes = [] {
        // Splatoon's text: Message/CommonMsg_<region><language>.szs, e.g. CommonMsg_EUde.szs.
        std::vector<std::string> found;
        if (const char* content = std::getenv("RBREW_CONTENT_DIR"); content && *content) {
            std::error_code ec;
            for (const auto& e : std::filesystem::directory_iterator(std::filesystem::path(content) / "Message", ec)) {
                const std::string n = e.path().filename().string();
                if (n.size() == 18 && n.rfind("CommonMsg_", 0) == 0 && n.compare(14, 4, ".szs") == 0)
                    found.push_back(n.substr(12, 2));
            }
        }
        std::vector<std::string> out;
        for (const auto& l : splat_languages())
            if (found.empty() || std::find(found.begin(), found.end(), l.code) != found.end()) out.push_back(l.code);
        return out;
    }();
    return codes;
}

const SplatLanguage& splat_language_resolve(const std::string& setting) {
    const auto& have = splat_languages_in_game();
    const auto in_game = [&](const std::string& c) { return std::find(have.begin(), have.end(), c) != have.end(); };
    if (setting != "auto" && in_game(setting)) return *splat_language_find(setting);
    if (const std::string sys = locale_language(); in_game(sys)) return *splat_language_find(sys);
    if (in_game("en")) return *splat_language_find("en");
    return *splat_language_find(have.front());
}

int splat_settings_args(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--language" || a.rfind("--language=", 0) == 0) {
            std::string v = a.size() > 10 ? a.substr(11) : (i + 1 < argc ? argv[++i] : "");
            for (char& c : v) c = char(std::tolower(static_cast<unsigned char>(c)));
            if (v != "auto" && !splat_language_find(v)) {
                std::fprintf(stderr, "--language: use auto or one of");
                for (const auto& c : splat_languages_in_game()) std::fprintf(stderr, " %s", c.c_str());
                std::fprintf(stderr, "\n");
                return 2;
            }
            setenv("SPLAT_LANGUAGE", v.c_str(), 1);
        }
    }
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], "--settings") == 0 && !splat_settings_tui()) return 0;
    return -1;
}

const std::string& splat_settings_path() {
    static const std::string path = [] {
        if (const char* p = std::getenv("SPLAT_SETTINGS"); p && *p) return std::string(p);
        std::filesystem::path base;
        if (const char* x = std::getenv("XDG_CONFIG_HOME"); x && *x) base = x;
        else if (const char* h = std::getenv("HOME"); h && *h) base = std::filesystem::path(h) / ".config";
        else base = ".";
        return (base / "splatoon-recomp" / "settings.ini").string();
    }();
    return path;
}

void splat_settings_load_file_only() {
    g_splat_settings = {};
    read_file(g_splat_settings);
}

void splat_settings_load() {
    static std::once_flag once;
    std::call_once(once, [] {
        splat_settings_load_file_only();
        read_env(g_splat_settings);
        const auto& s = g_splat_settings;
        // Gambit's swapchain reads its present mode from the environment.
        if (!std::getenv("RBREW_VK_PRESENT_MODE"))
            setenv("RBREW_VK_PRESENT_MODE", s.vsync ? "fifo" : "mailbox", 1);
        // ... and the system language (UC cafe.language) from RBREW_CONSOLE_LANGUAGE.
        const SplatLanguage& lang = splat_language_resolve(s.language);
        if (s.language != "auto" && s.language != lang.code)
            std::fprintf(stderr, "[splat] the game has no %s text, using %s\n", s.language.c_str(), lang.code);
        if (!std::getenv("RBREW_CONSOLE_LANGUAGE"))
            setenv("RBREW_CONSOLE_LANGUAGE", std::to_string(lang.wiiu_id).c_str(), 1);
        std::fprintf(stderr, "[splat] settings %s: scale %dx, aspect %s, vsync %s, %s, display %d, language %s%s\n",
                     splat_settings_path().c_str(), s.resolution_scale, splat_aspect_name(s.aspect_ratio),
                     s.vsync ? "on" : "off", splat_window_mode_name(s.window_mode), s.display, lang.code,
                     s.language == "auto" ? " (auto)" : "");
    });
}

bool splat_settings_save() {
    const auto& s = g_splat_settings;
    std::error_code ec;
    const std::filesystem::path path(splat_settings_path());
    if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path(), ec);
    std::ostringstream out;
    out << "# Splatoon Recompiled settings (scripts/run.sh --settings)\n"
        << "resolution_scale = " << s.resolution_scale << "\n"
        << "aspect_ratio = " << splat_aspect_name(s.aspect_ratio) << "\n"
        << "vsync = " << (s.vsync ? "on" : "off") << "\n"
        << "window_mode = " << splat_window_mode_name(s.window_mode) << "\n"
        << "display = " << s.display << "\n"
        << "language = " << s.language << "\n";
    const auto tmp = path.string() + ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        f << out.str();
        if (!f) return false;
    }
    std::filesystem::rename(tmp, path, ec);
    return !ec;
}

double splat_aspect_value(int aspect) {
    return kAspectValues[std::clamp(aspect, 0, SPLAT_ASPECT_COUNT - 1)];
}
const char* splat_aspect_name(int aspect) {
    return kAspectNames[std::clamp(aspect, 0, SPLAT_ASPECT_COUNT - 1)];
}
const char* splat_window_mode_name(int mode) {
    return kWindowNames[std::clamp(mode, 0, SPLAT_WINDOW_COUNT - 1)];
}
