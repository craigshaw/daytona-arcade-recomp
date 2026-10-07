// Decisions about pictures within a batch, not about guest execution speed.
#pragma once
#include <algorithm>
#include <cstdint>

namespace app {
inline unsigned pending_updates(double pending, double frame_ns) {
    unsigned count = 0;
    // Use the pacing loop's repeated subtraction, including at FP boundaries.
    for (; pending >= frame_ns; pending -= frame_ns) ++count;
    return count;
}

inline bool keep_software_picture(uint64_t frame, unsigned updates_left, int draw_mode) {
    const unsigned period = unsigned(std::clamp(draw_mode, 0, 2) + 1);
    // A later scheduled picture replaces this one before presentation. The
    // last scheduled draw can precede the last update in a frame-skipped batch.
    for (unsigned ahead = 1; ahead < updates_left; ++ahead)
        if ((frame + ahead) % period == 0) return false;
    return true; // the board still applies the existing draw-mode schedule
}
} // namespace app
