#include "runtime/panorama.h"
#include <cstdio>
#include <cstdlib>

int main() {
    auto check = [](bool value, const char *message) {
        if (!value) { std::fprintf(stderr, "%s\n", message); std::exit(1); }
    };
    rt::Panorama p;
    check(p.pixels.empty(), "disabled prototype must allocate no texture");
    p.enable(true);
    check(p.pixels.size() == 2048 * 512, "one 4 MiB texture");
    const auto *allocation = p.pixels.data();
    p.enable(false);
    p.enable(true);
    check(p.pixels.data() == allocation, "reenabling must reuse the prepared asset");
    // Every representable full-turn position, both sides of every original
    // 512-pixel boundary and the full 2048-pixel boundary. A one-texel camera
    // step must remain one texel even when either counter wraps.
    for (unsigned phase = 0; phase < 65536; phase += 32) {
        p.phase = uint16_t(phase);
        const uint32_t before = p.sample(433, 80, 435, 0);
        p.phase = uint16_t(phase + 32);
        check(p.sample(434, 80, 435, 0) == before, "scroll step jumped at a wrap");
    }
    for (int y = 0; y < 512; ++y) for (int x = -512; x < 2560; ++x) {
        check(p.sample(x, y, 435, 0) == p.sample(x + 2048, y, 435, 0), "horizontal join is not periodic");
        check(p.sample(x + 93, y, 93, 0) == p.sample(x + 435, y, 435, 0), "aspect ratio changed scale or anchoring");
    }
    check(rt::Panorama::vertical_offset(0x2fff) == -1, "negative horizon offset");
    check(rt::Panorama::vertical_offset(0x2050) == 80, "positive horizon offset");
    check(p.sample(0, 0, 435, 0x2fff) == p.sample(0, 0, 435, 0), "top edge must clamp");
    check(p.sample(0, 511, 435, 80) == p.sample(0, 511, 435, 0), "bottom edge must clamp");
    std::puts("Panorama periodicity, aspect anchoring, horizon and cache checks passed");
}
