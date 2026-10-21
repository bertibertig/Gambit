// Splatoon Recompiled additions to Gambit's renderer: resolution scaling.
//
// Render targets in the TV image chain (1280x720 and its halvings, as the
// game's bloom / post-processing uses them) and the GamePad's 854x480 are
// allocated at resolution_scale times their console size. Shadow maps,
// lookup tables and the small helper targets stay at console size. The
// renderer scales viewports / scissors on scaled targets, sets the shaders'
// fragCoordScale / texScale (the Cemu shader decompiler's hooks) and blits
// between targets of different scale.
#pragma once

#include <cstdint>

// Factor for a new render target of this console size (1 = unscaled).
uint32_t splat_target_scale(uint32_t width, uint32_t height);
