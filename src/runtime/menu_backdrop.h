// Widescreen menu borders, recognised from decoded artwork in either ROM.
// No stored artwork or RGB assumptions: palette fades remain the game's own.
#pragma once
#include <cstdint>
#include <initializer_list>

namespace rt {
struct MenuBackdrop {
    enum Pattern { None, Title, Mission }; // also sent to the shared tile shader
    static bool band(Pattern pattern, int y) {
        return pattern == Title ? y < 40 || y >= 344 : pattern == Mission && y >= 347;
    }
    static unsigned source_x(Pattern pattern, int x) {
        // Each mission half retains its own palette and check phase.
        return pattern == Mission ? (x < 0 ? 0u : 480u) + (unsigned(x) & 15u) : unsigned(x) & 63u;
    }

    static bool repeats(const uint16_t *row, const uint8_t *flags, int begin, int end, int half) {
        const uint16_t a = row[begin], b = row[begin + half];
        if (a == b || !(a & 15) || !(b & 15)) return false;
        for (int x = begin; x < end; ++x)
            if (row[x] != (((x - begin) / half) & 1 ? b : a) || flags[x] != 0x10) return false;
        return true;
    }

    template<class TileWord>
    static Pattern detect(const uint16_t *pixels, const uint8_t *flags, TileWord word) {
        // These menus use unscrolled layer 2 through the normal window mask.
        // Refuse disabled, scrolling or split layers, including stale title
        // artwork left behind while the game moves to another screen.
        if (word(0x5002) || word(0x5006)) return None;
        for (Pattern pattern : {Title, Mission}) {
            bool match = true;
            for (int y = 0; y < 384 && match; ++y) if (band(pattern, y)) {
                for (unsigned m = 0; m < 4; ++m)
                    if (word(0x6800 + unsigned(y) * 4 + m)) match = false;
                const auto *row = pixels + y * 512;
                const auto *f = flags + y * 512;
                // Mission's central 16 columns contain the blue/red zigzag
                // join. Check the complete repeating areas on either side.
                match = match && (pattern == Title ? repeats(row, f, 0, 496, 32) :
                    repeats(row, f, 0, 240, 8) && repeats(row, f, 256, 496, 8));
            }
            if (match) return pattern;
        }
        return None;
    }

    template<class TileWord>
    static bool uncovered(Pattern pattern, const uint8_t *overlay0, const uint8_t *overlay1, TileWord word) {
        const uint8_t *flags[] = {overlay0, overlay1};
        // Either overlay category can conceal the border (before or after
        // 3D). Sample its actual window/scroll selection at both native edges,
        // including the line-scroll transitions out of the selection screens.
        for (int y = 0; y < 384; ++y) if (band(pattern, y)) for (int x : {0, 495}) {
            const unsigned ctrl = word(0x5004), mode = (ctrl >> 13) & 3;
            for (unsigned l = 0; l < 2; ++l) {
                const unsigned v = word(0x5004 + l), hreg = word(0x5000 + l);
                if (v & 0x8000) continue;
                const unsigned h = hreg & 0x8000 ? word(0x4000 + l * 0x200 + unsigned(y)) : hreg;
                unsigned source = l;
                if (mode) {
                    if (l) continue;
                    if (mode == 1) {
                        source = ((0u - v) & 512) ? 0 : 1;
                        if (unsigned(y) >= ((0u - v) & 511)) source ^= 1;
                    } else {
                        source = h & 512 ? 0 : 1;
                        if (unsigned(x) >= (h & 511)) source ^= 1;
                    }
                } else {
                    unsigned mask = word(0x6000 + unsigned(y) * 4 + unsigned(x >> 7));
                    if (l) mask = ~mask;
                    if (mask & (0x8000u >> ((x >> 3) & 15))) continue;
                }
                const unsigned index = ((unsigned(y) + v) & 511) * 512 + ((unsigned(x) - h) & 511);
                if (flags[source][index] & 0x10) return false;
            }
        }
        return true;
    }
};
} // namespace rt
