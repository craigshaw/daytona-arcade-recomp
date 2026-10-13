#include "runtime/menu_backdrop.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <vector>

static void check(bool ok, const char *message) {
    if (!ok) { std::fprintf(stderr, "%s\n", message); std::exit(1); }
}

int main() {
    using rt::MenuBackdrop;
    // Synthetic pens and pattern, never copied ROM artwork. Poison the
    // offscreen columns: the tilemap's 512-pixel wrap is not the 64-pixel repeat.
    std::vector<uint16_t> pixels(512 * 512, 0xeee);
    std::vector<uint8_t> flags(512 * 512, 0x10);
    std::array<uint16_t, 0x8000> words{};
    auto matches = [&] { return MenuBackdrop::detect(pixels.data(), flags.data(),
        [&](unsigned i) { return words[i]; }); };
    check(!matches(), "plain or uninitialised backgrounds must not activate");
    for (int y = 0; y < 384; ++y) if (MenuBackdrop::band(MenuBackdrop::Title, y))
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
    check(MenuBackdrop::source_x(MenuBackdrop::Title, -1) == 63 && MenuBackdrop::source_x(MenuBackdrop::Title, -64) == 0 &&
          MenuBackdrop::source_x(MenuBackdrop::Title, 496) == 48, "both seams must continue the native check phase");
    check(!MenuBackdrop::band(MenuBackdrop::Title, 40) && !MenuBackdrop::band(MenuBackdrop::Title, 343), "gradients and centre must not repeat");
    std::fill(pixels.begin(), pixels.end(), 0xeee);
    for (int y = 347; y < 384; ++y) for (int x = 0; x < 496; ++x)
        if (x < 240 || x >= 256)
            pixels[y * 512 + x] = uint16_t((x < 240 ? 0x121 : 0x231) + ((x / 8 + y / 3) % 2));
    check(matches() == MenuBackdrop::Mission, "mission recognises both colours without sampling the centre join");
    for (int index : {347 * 512, 383 * 512 + 239, 347 * 512 + 256, 383 * 512 + 495}) {
        pixels[index] ^= 1;
        check(matches() == MenuBackdrop::None, "damage to either mission half must reject the extension");
        pixels[index] ^= 1;
    }
    check(MenuBackdrop::source_x(MenuBackdrop::Mission, -1) == 15 &&
          MenuBackdrop::source_x(MenuBackdrop::Mission, 496) == 480 &&
          MenuBackdrop::source_x(MenuBackdrop::Mission, 503) == 487,
          "mission must continue each side's own palette and phase");
    check(!MenuBackdrop::band(MenuBackdrop::Mission, 346) &&
          MenuBackdrop::band(MenuBackdrop::Mission, 347), "mission must extend only its bottom band");

    std::vector<uint8_t> overlay0(512 * 512), overlay1(512 * 512);
    auto uncovered = [&] { return MenuBackdrop::uncovered(MenuBackdrop::Mission,
        overlay0.data(), overlay1.data(), [&](unsigned i) { return words[i]; }); };
    check(uncovered(), "transparent overlays should preserve the border");
    overlay0[360 * 512 + 100] = 0x11;
    check(uncovered(), "central menu labels must not disable the margins");
    for (uint8_t category : {0x10, 0x11}) {
        overlay0[360 * 512] = category;
        check(!uncovered(), "either overlay priority must hide stale mission artwork");
    }
    overlay0[360 * 512] = 0;
    overlay1[360 * 512 + 495] = 0x10;
    check(uncovered(), "masked-out overlay pixels are not visible");
    words[0x6000 + 360 * 4 + 3] = 4; // x=495 selects the odd layer
    check(!uncovered(), "window-selected overlay must suppress the extension");
    words[0x6000 + 360 * 4 + 3] = 0;
    words[0x5000] = 0x8000; words[0x4000 + 360] = 511;
    overlay0[360 * 512 + 1] = 0x10;
    check(!uncovered(), "line scroll must use the visible overlay pixel");
    words[0x5000] = 0x400; words[0x5004] = 0x4000;
    check(uncovered(), "title's zero-offset split must select overlay 0");
    words[0x5000] = 0x200;
    check(!uncovered(), "opposite split must select overlay 1");
    words[0x5000] = 0; words[0x5004] = 0x2000;
    check(uncovered(), "zero vertical split selects overlay 0 below its boundary");
    words[0x5004] = 0x2200;
    check(!uncovered(), "opposite vertical split selects overlay 1");
    words[0x5004] = 0x8000; words[0x5005] = 0x8000;
    check(uncovered(), "disabled overlays must not conceal the border");
    std::puts("PASS: title/circuit and mission borders; phases, uploads, palette separation and overlay transitions");
}
