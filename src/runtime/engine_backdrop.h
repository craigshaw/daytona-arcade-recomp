// Optional local artwork for the engine-start screen. Never used at native aspect.
#pragma once
#include <algorithm>
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

namespace rt {
class EngineBackdrop {
public:
    static constexpr int Width = 1366, Height = 384, Margin = (Width - 496) / 2;
    // D2BGv002, little-endian width/height, then Width*Height u16 samples.
    // Each sample blends two live palette entries: a:4, b:4, weight:8.
    // Replaces the complete recognised picture. Native aspect never uses it.
    // Missing or invalid files retain the original picture and edge fill.
    bool load(const std::string &path) {
        samples_.clear(); active_ = false; checked_ = false;
        std::ifstream in(path, std::ios::binary);
        unsigned char header[16]{};
        if (!in.read(reinterpret_cast<char *>(header), sizeof header)) return false;
        const unsigned char expected[16] = {'D','2','B','G','v','0','0','2',0x56,5,0,0,0x80,1,0,0};
        if (!std::equal(header, header + sizeof header, expected)) return false;
        std::vector<unsigned char> bytes(size_t(Width) * Height * 2);
        if (!in.read(reinterpret_cast<char *>(bytes.data()), std::streamsize(bytes.size())) || in.peek() != EOF) return false;
        std::vector<uint16_t> samples(size_t(Width) * Height);
        for (size_t i = 0; i < samples.size(); ++i) {
            const unsigned v = bytes[2*i] | bytes[2*i+1] << 8;
            if (!(v & 15) || !(v & 240)) return false; // entry zero is not part of this picture
            samples[i] = uint16_t(v);
        }
        samples_ = std::move(samples);
        return true;
    }
    bool loaded() const { return !samples_.empty(); }
    bool active() const { return active_; }
    void deactivate() { active_ = false; }

    template<class TileWord>
    void update(int margin, const uint16_t *pixels, const uint8_t *flags, uint64_t generation, TileWord word) {
        active_ = false;
        if (!loaded() || margin <= 0 || margin > Margin || word(0x5000) || word(0x5004)) return;
        // The picture is opaque, unscrolled layer 0. Reject windows, partial
        // uploads and stale artwork as the game changes screens.
        for (unsigned i = 0; i < Height * 4; ++i) if (word(0x6000 + i)) return;
        if (!checked_ || generation != generation_) {
            checked_ = true; generation_ = generation; matches_ = false;
            if (pixels[0] == 0x60e && pixels[495] == 0x604) {
                uint64_t hash = 14695981039346656037ull;
                for (int y = 0; y < Height; ++y) for (int x = 0; x < 496; ++x) {
                    const size_t i = size_t(y) * 512 + x;
                    hash = (hash ^ pixels[i]) * 1099511628211ull;
                    hash = (hash ^ flags[i]) * 1099511628211ull;
                }
                // Same complete decoded picture and categories in both ROMs.
                matches_ = hash == 0x11f65258c9c6ddacull;
            }
        }
        active_ = matches_;
    }
    // x is relative to the original 496-wide picture. Narrower aspects crop
    // the same composition rather than squeezing the picture or lettering.
    uint32_t sample(int x, int y, const uint32_t *pens) const {
        const unsigned v = samples_[size_t(y) * Width + size_t(x + Margin)];
        const uint32_t a = pens[0x600 + (v & 15)], b = pens[0x600 + ((v >> 4) & 15)];
        const unsigned t = v >> 8;
        uint32_t colour = 0xff000000u;
        for (unsigned shift : {0u, 8u, 16u})
            colour |= ((((a >> shift) & 255) * (255 - t) + ((b >> shift) & 255) * t + 127) / 255) << shift;
        return colour;
    }
    void compose(uint32_t *dest, int margin, const uint32_t *pens) const {
        const int width = 496 + 2 * margin;
        for (int y = 0; y < Height; ++y) for (int x = 0; x < width; ++x)
            dest[size_t(y) * width + x] = sample(x - margin, y, pens);
    }
private:
    std::vector<uint16_t> samples_;
    uint64_t generation_ = 0;
    bool checked_ = false, matches_ = false, active_ = false;
};
} // namespace rt
