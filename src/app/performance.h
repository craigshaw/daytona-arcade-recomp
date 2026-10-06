// Bounded, host-only diagnostics. No allocation, sorting or I/O while sampling.
#pragma once
#include "runtime/frame_profile.h"
#include "runtime/video_profile.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <iosfwd>

namespace app {
class Performance {
public:
    enum Mode { Off, Fps, Timings };
    struct Sample {
        uint64_t frame = 0, wall = 0, max_update = 0;
        uint64_t render = 0, overlay = 0, wait = 0, submit = 0, discarded = 0;
        rt::FrameProfile game;
        rt::VideoProfile video;
        uint32_t updates = 0, presents = 0, slow = 0;
        uint32_t width = 0, polygons = 0, budget = 0;
        int distance = 0, skip = 0, scale = 1;
        unsigned course = 255;
        bool hardware = false, panorama = false, hud = false, native_audio = false;
        void update(const rt::FrameProfile &, const rt::VideoProfile &, double budget_ns);
    };
    static constexpr size_t Capacity = 1024;
    explicit Performance(double hz) : hz_(hz) {}
    Mode mode() const { return mode_; }
    bool enabled() const { return mode_ != Off; }
    bool detailed() const { return mode_ == Timings; }
    void cycle() { mode_ = Mode((mode_ + 1) % 3); clear(); }
    void clear();
    void push(const Sample &sample);
    size_t size() const { return count_; }
    const Sample &at(size_t chronological_index) const;
    const char *text() const { return text_.data(); }
    void write_csv(std::ostream &, const char *romset, const char *gpu) const;
private:
    void refresh();
    Mode mode_ = Off;
    double hz_;
    std::array<Sample, Capacity> samples_{};
    size_t next_ = 0, count_ = 0;
    uint64_t refresh_elapsed_ = 0;
    std::array<char, 768> text_{};
};
} // namespace app
