#include "splat_render.h"

#include "splat_settings.h"

uint32_t splat_target_scale(uint32_t width, uint32_t height) {
    const auto scale = uint32_t(g_splat_settings.resolution_scale);
    if (scale <= 1) return 1;
    // TV chain: 1280x720 >> k (20x11 is the smallest bloom level).
    for (uint32_t k = 0; k <= 6; ++k)
        if (width == (1280u >> k) && height == (720u >> k)) return scale;
    // GamePad screen.
    if (width == 854 && height == 480) return scale;
    return 1;
}
