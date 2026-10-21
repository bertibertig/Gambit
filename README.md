# Gambit – Splatoon Recompiled

A native PC port of **Splatoon (Wii U)**: the game's PowerPC executable
(`Gambit.rpx`) is statically recompiled to C++ by
[RebrewU](https://github.com/ApfelTeeSaft/RebrewU) and runs on a native Cafe OS
runtime with a Vulkan renderer. No emulator, no JIT.

This is a fork of **[ApfelTeeSaft/Gambit](https://github.com/ApfelTeeSaft/Gambit)**,
which ApfelTeeSaft wrote and has since discontinued:

> Due to me not having any time or motivation for this specific port anymore,
> i'm calling it quits, use this or don't, fork it or don't, but make sure to
> credit me!
>
> — ApfelTeeSaft, original README

All of Gambit and RebrewU – the recompiler, the Cafe OS runtime, the Vulkan
GX2 renderer, the launcher and the function-boundary profile – is
**ApfelTeeSaft's work**. This fork adds the *Splatoon Recompiled* features
listed below on top of it.

> [!IMPORTANT]
> This repository contains **no game files, no game code, no keys**. You need
> your own dump of Splatoon for Wii U; the build recompiles *your* copy on
> *your* machine.

## Splatoon Recompiled additions

| Feature | Where |
|---|---|
| Keyboard + mouse controls like a PC third-person shooter; the mouse aims through the game's own GamePad gyro aiming, 1:1 (no deadzone, curve, smoothing or acceleration) | `port/splat/splat_input.*`, `port/os/vpad/vpad.cpp` |
| F10 releases / captures the mouse; focus loss releases it | `port/splat/splat_platform.*` |
| Native Wayland (preferred) with X11 fallback, HiDPI window, app id `splatoon-recomp` | `port/splat/splat_platform.*` |
| Settings screen in the terminal (`Splatoon --settings`): resolution scaling, V-Sync, window mode, monitor, language | `port/splat/splat_settings*.cpp` |
| Resolution scaling 1x–4x (up to 5120x2880): screen-chain render targets at a multiple of the console size, Cemu-style `fragCoordScale` / `texScale` | `port/splat/splat_render.*`, `port/os/gx2/gx2_render_vk*.cpp` |
| Game language from the settings / `--language` (EUR: en de fr es it) | `port/splat/splat_settings.cpp` |
| Fix: GX2 blend combine modes (MIN / MAX / reverse subtract were shifted) – the text on the ink-splat buttons (character creation) was missing | `port/os/gx2/gx2_render_vk.cpp` |
| Linux / GCC build: `cmake/linux_compat.h` (MSVC-implicit headers and intrinsics), `-march=x86-64-v2` | `port/CMakeLists.txt` |
| `OSReport` logs the text of `%s` arguments (sead assertion messages) | `port/os/coreinit/coreinit_misc.cpp` |

Every change to ApfelTeeSaft's files is marked with `SPLAT_RECOMP` /
`splatoon-recomp` comments. `-DSPLAT_RECOMP=OFF` builds the original runtime.

Supported game: **Splatoon EUR (AGMP01) v16**, `code/Gambit.rpx` SHA-256
`45ecd1ed81a97f46b1c8ae24820cd47fbc55399b50af09bf1f9050fe803308e5`
(the build `profiles/agmp-v16.entries` is made for).

## Building on Linux

Needs CMake 3.20+, Ninja, a C++20 compiler (tested with GCC 16 on Arch Linux), the Vulkan
loader and the usual SDL2 system libraries (Wayland / X11, ALSA / PulseAudio
/ PipeWire headers are picked up when present). SDL2, glslang, Vulkan-Headers,
volk and zlib are fetched by the build (`SPLPORT_FETCH_DEPS=ON`).

```bash
git clone --recursive https://github.com/bertibertig/Gambit
cd Gambit

# 1. The recompiler
cmake -S recompiler -B build/rebrewu -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DREBREWU_BUILD_TESTS=OFF -DCMAKE_CXX_FLAGS="-include algorithm"
cmake --build build/rebrewu --target rebrewu

# 2. Recompile your own Gambit.rpx (EUR v16) with the boundary profile
build/rebrewu/bin/rebrewu recompile --rpx /path/to/game/code/Gambit.rpx \
      -o build/generated --analyzer-entries profiles/agmp-v16.entries

# 3. The game (about 12 minutes with 8 jobs; the generated units need RAM)
cmake -S port -B build/port -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DSPLPORT_FETCH_DEPS=ON -DGAMBIT_GENERATED_DIR=$PWD/build/generated
cmake --build build/port --target Gambit -j 8
```

The result is `build/port/Splatoon`. The original launcher (`SplatoonSetup`,
top-level `CMakeLists.txt`) builds the same `port/` tree, so it picks up the
additions too; that path (Zig toolchain, Windows) has not been tested with
them yet.

## Running

```bash
cd /path/to/a/run/folder           # saves and caches are written here
RBREW_CONTENT_DIR=/path/to/game/content \
RBREW_TITLE_METADATA=/path/to/game/meta/meta.xml \
RBREW_MLC_DIR=$PWD/mlc01 RBREW_VK_CACHE_DIR=$PWD/vulkan_cache \
  /path/to/Gambit/build/port/Splatoon            # --settings, --language de
```

The game wants the Wii U shared system fonts in
`mlc01/sys/title/0005001b/10042400/content/` (`CafeCn.ttf`, `CafeKr.ttf`,
`CafeStd.ttf`, `CafeTw.ttf`): from your own console, or Cemu's open
replacements (Noto Sans CJK, SIL OFL 1.1, see `LICENSES/`) from
[cemu-project/Cemu `bin/resources/sharedFonts`](https://github.com/cemu-project/Cemu/tree/cc6fea125a5d4f559c662d13c5f4af0b6dd0876f/bin/resources/sharedFonts).

## Settings

`Splatoon --settings` opens the settings screen in the terminal before the
game starts. Arrow keys move and change values, Tab switches between the
categories and the options, Enter saves and starts, S saves, Q quits. The
choices are saved in `~/.config/splatoon-recomp/settings.ini`.

| Category | Options | Status |
|---|---|---|
| Resolution Scaling | Internal resolution 1x–4x (1280x720 up to 5120x2880) | ✅ |
| Aspect Ratio | 16:9 (original), 16:10, 21:9, 32:9 | ⬜ shown, not implemented yet |
| Framerate | V-Sync on (FIFO) / off (mailbox, no tearing); the game runs at its original 60 fps | ✅ |
| Display | Windowed, fullscreen or borderless fullscreen, and the monitor | ✅ |
| Language | Auto (system language) or one of the game's languages (EUR: English, Deutsch, Français, Español, Italiano) | ✅ |

One run can override the file with environment variables:
`SPLAT_RESOLUTION_SCALE`, `SPLAT_ASPECT_RATIO`, `SPLAT_VSYNC`,
`SPLAT_WINDOW_MODE` (`windowed`, `fullscreen`, `borderless`), `SPLAT_DISPLAY`,
`SPLAT_LANGUAGE`; `SPLAT_SETTINGS=<file>` uses another file.
`SDL_VIDEODRIVER=x11` forces X11.

## Controls

### Keyboard + mouse (default)

| Key | Wii U GamePad | In the game |
|---|---|---|
| Mouse | Gyro (tilt / turn) | Aim and look, both axes |
| W A S D | Left stick | Move (Left Alt held: walk) |
| Left mouse button | ZR | Shoot ink |
| Right mouse button | R | Sub weapon |
| Left Shift | ZL | Squid form |
| Space | X | Jump |
| Q or middle mouse button | Right stick click | Special weapon |
| C | Y | Reset camera |
| E or Enter | A | Confirm |
| Backspace or Escape | B | Back |
| Arrow keys | D-pad | Menus |
| I J K L | Right stick | Menus (e.g. rotate the character) |
| F | L | |
| Z | Left stick click | |
| P | + | |
| M or - | − | |
| Tab | – | Switch TV / GamePad view (map, Super Jump: click a teammate) |
| F10 | – | Release / capture the mouse (a click into the window captures it again) |
| F11 | – | Fullscreen |

Mouse sensitivity is in degrees of GamePad rotation per mouse count:
`SPLAT_MOUSE_SENS=0.05` (default; at 800 dpi one inch turns by 40°).
`SPLAT_INVERT_Y=1` inverts vertical aim, `SPLAT_MOUSE_AIM=0` turns mouse aim
off (mouse = right stick, as in the original Gambit), `SPLAT_MOUSE_CAPTURE=0`
starts with the mouse released. The layout is fixed for now.

### Controllers

Controllers use Gambit's controller mode (`controls.ini` next to the
executable, written by the launcher); buttons by position like on a GamePad.

### Automated tests

`SPLAT_INPUT_SCRIPT` plays timed virtual input without touching real devices,
e.g. `SPLAT_INPUT_SCRIPT="8000:a,9000:move=up/1500,20000:yaw=45,21000:touch=213;240"`
(buttons, sticks, mouse angles, GamePad touch, `key=tab`; `@file` reads a
list). Details in `port/splat/splat_input.cpp`.

## Credits

- **[ApfelTeeSaft](https://github.com/ApfelTeeSaft)** – Gambit and
  [RebrewU](https://github.com/ApfelTeeSaft/RebrewU): recompiler, runtime,
  renderer, launcher, profiles. Original repository:
  [ApfelTeeSaft/Gambit](https://github.com/ApfelTeeSaft/Gambit).
- [bertibertig](https://github.com/bertibertig) – the Splatoon Recompiled
  additions in this fork.
- [Cemu](https://github.com/cemu-project/Cemu) (MPL-2.0) – Latte shader
  decompiler and address library in `port/third_party/cemu_shader`, register
  semantics, shared-font replacements.
- SDL2, glslang, Vulkan-Headers, volk, zlib, curl, miniaudio – see `LICENSES/`.

## Disclaimer

Not affiliated with, endorsed by, or sponsored by Nintendo. Splatoon, Wii U
and Nintendo are trademarks of Nintendo. No game assets, code or keys are
distributed here.
