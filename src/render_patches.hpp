#pragma once
#include <array>
#include <cstdint>

namespace mp2 {
// MP2's full-screen photo renderer emits a 5x5 grid of 64x48 blocks,
// split into 32- and 16-row strips. Positions describe pixel centers (+0.5).
// RT64's rectangle viewports ceil those coordinates to integer edges, exposing
// stale RAM along the top/left. Translate this specific grid to pixel edges;
// leave UI, scaled/flip/copy rectangles and non-full-screen scissors alone.
inline bool align_background_tile(uint32_t &w0, uint32_t &w1, uint32_t derivatives, bool one_cycle,
                                  uint16_t fb_width, uint8_t fb_size, const std::array<int32_t, 4> &scissor) {
    if (!one_cycle || fb_width != 320 || fb_size != 2 || derivatives != 0x04000400 ||
        scissor != std::array<int32_t, 4>{0, 0, 1280, 960})
        return false;
    const int left = (w1 >> 12) & 0xFFF, top = w1 & 0xFFF;
    const int right = (w0 >> 12) & 0xFFF, bottom = w0 & 0xFFF;
    const int strip = top % 192;
    if ((w0 >> 24) != 0xE4 || left < 2 || left > 1026 || (left - 2) % 256 != 0 || right - left != 256 ||
        top < 2 || bottom > 962 ||
        !((strip == 2 && bottom - top == 128) || (strip == 130 && bottom - top == 64)))
        return false;
    w0 = (w0 & 0xFF000000u) | (uint32_t(right - 2) << 12) | uint32_t(bottom - 2);
    w1 = (w1 & 0xFF000000u) | (uint32_t(left - 2) << 12) | uint32_t(top - 2);
    return true;
}
} // namespace mp2
