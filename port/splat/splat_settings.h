// Splatoon Recompiled: graphics and language settings. One INI file, read at start-up and
// written by the settings screen (splat_settings_tui.cpp, `Splatoon --settings`,
// `scripts/run.sh --settings`).
//
// File: $SPLAT_SETTINGS, else $XDG_CONFIG_HOME/splatoon-recomp/settings.ini
// (~/.config/... without XDG_CONFIG_HOME). A missing file means the defaults,
// which are the original look: 1x (1280x720), 16:9, windowed.
//
// One run can override the file with environment variables named like the
// keys, upper-cased with a SPLAT_ prefix: SPLAT_RESOLUTION_SCALE=2,
// SPLAT_ASPECT_RATIO=21:9, SPLAT_VSYNC=1, SPLAT_WINDOW_MODE=borderless,
// SPLAT_DISPLAY=1, SPLAT_LANGUAGE=de. `--language <code>` on the command line
// does the same as SPLAT_LANGUAGE (splat_settings_args).
#pragma once

#include <string>
#include <vector>

enum SplatAspect { SPLAT_ASPECT_16_9, SPLAT_ASPECT_16_10, SPLAT_ASPECT_21_9, SPLAT_ASPECT_32_9,
                   SPLAT_ASPECT_COUNT };
enum SplatWindowMode { SPLAT_WINDOWED, SPLAT_FULLSCREEN, SPLAT_BORDERLESS, SPLAT_WINDOW_COUNT };

constexpr int kSplatScaleMax = 4;  // 4x = 5120x2880

struct SplatSettings {
    int resolution_scale = 1;  // 1..kSplatScaleMax, x 1280x720
    int aspect_ratio = SPLAT_ASPECT_16_9;
    bool vsync = false;        // true: FIFO; false: mailbox (no tearing, lowest latency)
    int window_mode = SPLAT_WINDOWED;
    int display = 0;           // SDL display index for fullscreen / borderless
    std::string language = "auto";  // a SplatLanguage code, or "auto" (from $LANG)
};

// The Wii U system languages (UC cafe.language), which the game reads to pick
// its text. A game only has some of them (EU: en de fr es it).
struct SplatLanguage {
    const char* code;     // "de"
    int wiiu_id;          // cafe.language
    const char* native;   // "Deutsch"
    const char* english;  // "German"
};
const std::vector<SplatLanguage>& splat_languages();
// Codes of the languages this copy of the game has text for (from
// $RBREW_CONTENT_DIR/Message), all of them when that cannot be told.
const std::vector<std::string>& splat_languages_in_game();
const SplatLanguage* splat_language_find(const std::string& code);
// "auto": the language of $LC_ALL / $LC_MESSAGES / $LANG if the game has it,
// else English. Unknown or missing codes resolve the same way.
const SplatLanguage& splat_language_resolve(const std::string& setting);

extern SplatSettings g_splat_settings;

// Command-line options, before splat_settings_load: `--settings` (the settings
// screen) and `--language <code>`. Returns -1 to go on, else an exit code.
int splat_settings_args(int argc, char** argv);
// Defaults, then the file, then the environment overrides. Called once, early
// (idempotent). Also exports what the renderer reads from the environment.
void splat_settings_load();
// Defaults and the file only (the settings screen edits these).
void splat_settings_load_file_only();
// Writes the current values. True on success.
bool splat_settings_save();
const std::string& splat_settings_path();

double splat_aspect_value(int aspect);
const char* splat_aspect_name(int aspect);
const char* splat_window_mode_name(int mode);

// The settings screen in the terminal. Returns true when the player chose to
// start the game, false to quit.
bool splat_settings_tui();
