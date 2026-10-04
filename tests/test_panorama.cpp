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
    // Synthetic ROM, not extracted game content: distinct sections and a
    // foreground-category tile verify decoding, ordering and wrap direction.
    std::vector<uint8_t> rom(0x80000);
    auto put = [&](unsigned offset, uint32_t value) {
        for (unsigned i = 0; i < 4; ++i) rom[offset + i] = uint8_t(value >> (8 * i));
    };
    put(0x74240, 0x2000100);
    put(0x100, 0x2000200);
    put(0x104, 0x1080000);
    put(0x200, 8);
    for (unsigned section = 0; section < 8; ++section) {
        std::fill_n(rom.data() + 0x204 + section * 32, 32, uint8_t((section + 1) * 17));
        const unsigned offset = 0x1000 + section * 0xc80;
        put(0x74248 + section * 4, 0x2000000 + offset);
        put(offset + 4, 49);
        put(offset + 8, 32);
        for (unsigned tile = 0; tile < 32 * 49; ++tile) {
            rom[offset + 12 + tile * 2] = uint8_t(section);
            rom[offset + 13 + tile * 2] = section == 7 ? 0x80 : 0;
        }
    }
    rt::Panorama original;
    check(original.load_original(rom), "valid source must decode");
    check(original.indices.size() * 2 == 1605632 && original.pixels.empty(), "index cache only, no RGB texture");
    std::vector<uint8_t> live_tiles(0x10000), live_chars(0x80000);
    std::copy_n(rom.data() + 0x204, 8 * 32, live_chars.data());
    check(!original.matches_live(live_tiles.data(), live_chars.data()), "unfinished map upload must fall back");
    // The initial map spans sections 0 and 1 in the native 496-pixel view.
    for (unsigned row = 6; row < 55; ++row) for (unsigned x = 0; x < 64; ++x)
        live_tiles[(0x2000 + row * 64 + x) * 2] = uint8_t(x / 32);
    check(original.matches_live(live_tiles.data(), live_chars.data()), "complete live source window accepted");
    live_chars[0] ^= 1;
    check(!original.matches_live(live_tiles.data(), live_chars.data()), "stale character bank must fall back");
    live_chars[0] ^= 1;
    live_tiles[(0x2000 + 6 * 64) * 2] ^= 1;
    check(!original.matches_live(live_tiles.data(), live_chars.data()), "stale streamed column must fall back");
    for (int section = 0; section < 8; ++section)
        check(original.original_pixel(section * 256, 48) == ((section == 7 ? 0x8000 : 0) | (section + 1)), "source section order/category");
    original.phase = 32;
    check(original.original_pixel(0, 439) == 0x8008, "original full-width wrap and last source row");
    const auto *cached = original.indices.data();
    check(original.load_original({}) && cached == original.indices.data(), "immutable source stays cached across palette changes");
    check(!rt::Panorama{}.load_original({}), "truncated ROM rejected");
    put(0x200, 0xffffffff);
    check(!rt::Panorama{}.load_original(rom), "overflowing character count rejected");
    put(0x200, 8);
    put(0x1008, 64);
    check(!rt::Panorama{}.load_original(rom), "unexpected map width rejected");
    put(0x1008, 32);
    put(0x104, 0x1080001);
    check(!rt::Panorama{}.load_original(rom), "unaligned character upload rejected");
    std::puts("Panorama periodicity, aspect anchoring, horizon and cache checks passed");
}
