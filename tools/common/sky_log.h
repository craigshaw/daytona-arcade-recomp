// Read-only background telemetry for the panorama prototype.
#pragma once
#include "runtime/game_loop.h"
#include <fstream>

namespace tools {
class SkyLog {
public:
    explicit SkyLog(const std::string &path) {
        if (path.empty()) return;
        stream_.open(path);
        if (!stream_) throw std::runtime_error("cannot open sky log: " + path);
    }
    void write(rt::GameLoop &game) {
        if (!stream_.is_open()) return;
        auto &b = game.board();
        const auto &v = b.video();
        stream_ << "{\"frame\":" << b.frame() << ",\"course\":" << unsigned(b.read_byte(0x501460))
                << ",\"windows\":" << v.gpu_windows() << ",\"backdrop\":" << int(v.backdrop())
                << ",\"sky_phase\":" << b.read_word(0x5fe11a)
                << ",\"camera_phase\":" << b.read_dword(0x50174c)
                << ",\"sky_offset\":" << b.read_dword(0x501748)
                << ",\"tile_generation\":" << v.system24_texture_generation()
                << ",\"panorama\":" << (v.panorama_active() ? "true" : "false")
                << ",\"panorama_phase\":" << v.panorama().phase
                << ",\"panorama_valid\":" << (v.panorama().phase_valid ? "true" : "false")
                << ",\"panorama_vertical\":" << v.panorama().vertical
                << ",\"scroll\":[";
        for (unsigned i = 0; i < 8; ++i) {
            if (i) stream_ << ',';
            stream_ << v.system24_word(0x5000 + i);
        }
        stream_ << "]}\n";
        if (!stream_) throw std::runtime_error("cannot write sky log");
    }
private:
    std::ofstream stream_;
};
} // namespace tools
