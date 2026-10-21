// Splatoon Recompiled additions to Gambit's runtime: keyboard + mouse.
//
// PC third-person-shooter controls on top of the GamePad: the mouse drives
// the GamePad's motion sensors (Splatoon's gyro aim) 1:1 - no deadzone, no
// response curve, no smoothing, no acceleration. The layout is static for
// now and documented in README.md ("Controls").
#pragma once

#include <cstdint>

struct SplatPadInput {
    uint32_t buttons = 0;          // VPAD_BUTTON_* bits
    float lx = 0, ly = 0;          // left stick, y > 0 = up
    float rx = 0, ry = 0;          // right stick (menus only; aiming is the mouse)
    float yaw_deg = 0;             // mouse motion since the last read, degrees of
    float pitch_deg = 0;           //   GamePad rotation (yaw > 0 = right, pitch > 0 = up)
    bool keyboard_active = false;  // any key/button/mouse input this read
};

// Keyboard + mouse state for one VPADRead. use_mouse = false in the GamePad
// view, where the mouse is the touch screen. Consumes the accumulated
// relative mouse motion.
void splat_input_read(SplatPadInput& out, bool use_mouse);

// True when the mouse aims through the gyro: Splatoon's motion controls are
// then kept on in every scene (Gambit turns them off outside the tutorial).
bool splat_input_mouse_aim();

// Degrees of virtual GamePad rotation per mouse count (SPLAT_MOUSE_SENS),
// and whether vertical mouse motion is inverted (SPLAT_INVERT_Y=1).
float splat_input_sensitivity();
bool splat_input_invert_y();
