#include "runtime/title_backdrop.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <vector>

static void check(bool ok, const char *message) {
    if (!ok) { std::fprintf(stderr, "%s\n", message); std::exit(1); }
}

int main() {
    using rt::TitleBackdrop;
    // Synthetic pens and pattern, never copied ROM artwork. Poison the
    // offscreen columns: the tilemap's 512-pixel wrap is not the 64-pixel repeat.
    std::vector<uint16_t> pixels(512 * 512, 0xeee);
    std::vector<uint8_t> flags(512 * 512, 0x10);
    std::array<uint16_t, 0x8000> words{};
    auto matches = [&] { return TitleBackdrop::matches(pixels.data(), flags.data(),
        [&](unsigned i) { return words[i]; }); };
    check(!matches(), "plain or uninitialised backgrounds must not activate");
    for (int y = 0; y < 384; ++y) if (TitleBackdrop::band(y))
        for (int x = 0; x < 496; ++x)
            pixels[y * 512 + x] = uint16_t(0x121 + ((x / 32 + y / 3) % 2));
    check(matches(), "both complete title bands should activate without RGB assumptions");
    for (int index : {0, 495, 39 * 512 + 495, 344 * 512, 383 * 512 + 495}) {
        pixels[index] ^= 1;
        check(!matches(), "partial upload or changed border must fall back immediately");
        pixels[index] ^= 1;
        flags[index] ^= 1;
        check(!matches(), "foreground-category source must not activate");
        flags[index] ^= 1;
    }
    for (unsigned reg : {0x5002u, 0x5006u}) for (uint16_t value : {1, 0x2000, 0x4000, 0x8000}) {
        words[reg] = value;
        check(!matches(), "scroll, split or disabled source must not activate");
        words[reg] = 0;
    }
    for (int y : {0, 39, 344, 383}) for (unsigned m = 0; m < 4; ++m) {
        words[0x6800 + y * 4 + m] = 0x8000;
        check(!matches(), "window selecting another layer must not activate");
        words[0x6800 + y * 4 + m] = 0;
    }
    check(matches(), "restoring the title must reactivate");
    check(TitleBackdrop::source_x(-1) == 63 && TitleBackdrop::source_x(-64) == 0 &&
          TitleBackdrop::source_x(496) == 48, "both seams must continue the native check phase");
    check(!TitleBackdrop::band(40) && !TitleBackdrop::band(343), "gradients and centre must not repeat");
    std::puts("PASS: title pattern, partial uploads, window/scroll/priority rejection and seam phase");
}
