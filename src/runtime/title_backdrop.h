// Widescreen title borders, recognised from decoded artwork in either ROM.
// No stored artwork or RGB assumptions: palette fades remain the game's own.
#pragma once
#include <cstdint>
#include <initializer_list>

namespace rt {
struct TitleBackdrop {
    static constexpr int Period = 64, BandHeight = 40, Bottom = 344;
    static bool band(int y) { return y < BandHeight || y >= Bottom; }
    static unsigned source_x(int x) { return unsigned(x) & (Period - 1); }

    template<class TileWord>
    static bool matches(const uint16_t *pixels, const uint8_t *flags, TileWord word) {
        // The title uses unscrolled layer 2 through the normal window mask.
        // Refuse disabled, scrolling or split layers, including stale title
        // artwork left behind while the game moves to another screen.
        if (word(0x5002) || word(0x5006)) return false;
        for (int top : {0, Bottom}) for (int y = top; y < top + BandHeight; ++y) {
            for (unsigned m = 0; m < 4; ++m)
                if (word(0x6800 + unsigned(y) * 4 + m)) return false;
            const auto *row = pixels + y * 512;
            const uint16_t a = row[0], b = row[Period / 2];
            if (a == b || !(a & 15) || !(b & 15)) return false;
            // Check the whole native width, not just an edge that might
            // happen to resemble the title. Never sample the unused 16 columns.
            for (int x = 0; x < 496; ++x)
                if (row[x] != ((x / (Period / 2)) & 1 ? b : a) || flags[y * 512 + x] != 0x10)
                    return false;
        }
        return true;
    }
};
} // namespace rt
