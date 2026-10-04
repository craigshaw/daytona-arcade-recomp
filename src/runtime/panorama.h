// Opt-in one-course panorama proof. The temporary test art is original,
// generated once, and never fed back into game RAM or polygon selection.
#pragma once
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <vector>

namespace rt {
class Panorama {
public:
    static constexpr int Width = 2048, Height = 512;
    void enable(bool on) {
        enabled = on;
        if (!on || !pixels.empty()) return;
        pixels.resize(Width * Height);
        // Periodic clouds straddle the texture join deliberately. Integer
        // ellipses and a vertical gradient make an auditable sampling fixture.
        struct Cloud { int x, y, rx, ry; };
        constexpr Cloud clouds[] = {{0,75,125,27}, {71,67,64,34}, {440,109,108,24},
            {500,96,56,35}, {900,65,120,24}, {960,54,58,33}, {1400,116,100,25},
            {1460,101,62,36}, {1750,52,70,19}};
        for (int y = 0; y < Height; ++y) for (int x = 0; x < Width; ++x) {
            const int t = std::min(y, 260);
            unsigned r = unsigned(38 + t * 108 / 260), g = unsigned(105 + t * 86 / 260), b = unsigned(188 + t * 35 / 260);
            for (const auto &cloud : clouds) {
                int dx = std::abs(x - cloud.x);
                dx = std::min(dx, Width - dx);
                const int dy = y - cloud.y;
                if (int64_t(dx) * dx * cloud.ry * cloud.ry + int64_t(dy) * dy * cloud.rx * cloud.rx <=
                    int64_t(cloud.rx) * cloud.rx * cloud.ry * cloud.ry) {
                    r = 232; g = 239; b = 246;
                    break;
                }
            }
            pixels[size_t(y) * Width + size_t(x)] = 0xff000000u | (r << 16) | (g << 8) | b;
        }
    }
    // The game derives layer-2 hscroll from phase >> 5, then masks to 511.
    // Retain the full 16-bit phase: one complete cycle spans 2048 texels.
    int scroll_x() const { return int(phase >> 5); }
    static int vertical_offset(unsigned reg) { return int((reg + 256) & 511) - 256; }
    uint32_t sample(int x, int y, int margin, unsigned vertical) const {
        const unsigned u = unsigned(x - margin - scroll_x()) & (Width - 1);
        const unsigned v = unsigned(std::clamp(y + vertical_offset(vertical), 0, Height - 1));
        return pixels[size_t(v) * Width + u];
    }
    bool enabled = false;
    bool sweep = false; // capture-only sampling stress, never changes the game
    bool only = false; // capture-only isolation of the existing background pass
    uint16_t phase = 0;
    uint16_t pending_phase = 0, register_phase = 0;
    bool phase_valid = false, register_valid = false;
    uint16_t horizontal = 0, vertical = 0;
    uint8_t course = 255;
    std::vector<uint32_t> pixels;
};
} // namespace rt
