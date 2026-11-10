#include "render_patches.hpp"
#include <cstdio>
int main() {
    constexpr std::array<int32_t, 4> scissor{0, 0, 1280, 960};
    // Replay the observed photo grid. Every output strip must cover its exact
    // pixel edges, including row/column zero and the 320x240 lower boundary.
    unsigned tiles = 0;
    for (unsigned y = 0; y < 240; y += 48)
        for (unsigned x = 0; x < 320; x += 64)
            for (unsigned strip = 0; strip < 2; ++strip) {
                unsigned top = y + (strip ? 32 : 0), height = strip ? 16 : 32;
                uint32_t w0 = 0xE4000000 | ((x + 64) * 4 + 2) << 12 | ((top + height) * 4 + 2);
                uint32_t w1 = (x * 4 + 2) << 12 | (top * 4 + 2);
                if (!mp2::align_background_tile(w0, w1, 0x04000400, true, 320, 2, scissor))
                    return 1;
                if (((w1 >> 12) & 0xFFF) != x * 4 || (w1 & 0xFFF) != top * 4 ||
                    ((w0 >> 12) & 0xFFF) != (x + 64) * 4 || (w0 & 0xFFF) != (top + height) * 4)
                    return 2;
                ++tiles;
            }
    auto unchanged = [&](uint32_t w0, uint32_t w1, uint32_t d, bool cycle, uint16_t width, uint8_t size,
                         std::array<int32_t, 4> clip) {
        auto a = w0, b = w1;
        return !mp2::align_background_tile(a, b, d, cycle, width, size, clip) && a == w0 && b == w1;
    };
    if (!unchanged(0xE4102082, 0x2002, 0x04000400, false, 320, 2, scissor) ||
        !unchanged(0xE4102082, 0x2002, 0x08000400, true, 320, 2, scissor) ||
        !unchanged(0xE4102082, 0x2002, 0x04000400, true, 640, 2, scissor) ||
        !unchanged(0xE4102082, 0x2002, 0x04000400, true, 320, 3, scissor) ||
        !unchanged(0xE4102082, 0x2002, 0x04000400, true, 320, 2, {4, 4, 1276, 956}) ||
        !unchanged(0xE5102082, 0x2002, 0x04000400, true, 320, 2, scissor) ||
        !unchanged(0xE4100080, 0, 0x04000400, true, 320, 2, scissor) ||
        !unchanged(0xE4082042, 0x2002, 0x04000400, true, 320, 2, scissor))
        return 3;
    printf("PASS: %u background tiles cover 320x240; UI/scaled/copy/flip/other targets unchanged.\n", tiles);
}
