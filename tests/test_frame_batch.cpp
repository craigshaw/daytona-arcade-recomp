#include "app/frame_batch.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>

static void check(bool ok, const char *message) {
    if (!ok) { std::fprintf(stderr, "%s\n", message); std::exit(1); }
}

int main() {
    constexpr double step = 1e9 / (16000000.0 / (656.0 * 424.0));
    for (unsigned n = 0; n <= 4; ++n) for (double offset : {-1.0, 0.0, 1.0}) {
        double pending = n * step + offset;
        unsigned expected = 0;
        while (pending >= step) { pending -= step; ++expected; }
        check(app::pending_updates(n * step + offset, step) == expected, "batch count differs from pacing loop");
    }
    check(app::pending_updates(std::nextafter(step, 0.0), step) == 0, "rounding must not invent an update");
    check(app::pending_updates(step, step) == 1, "exactly due update must run");
    for (int mode = 0; mode < 3; ++mode) for (uint64_t first = 0; first < 12; ++first)
        for (unsigned batch = 1; batch <= 4; ++batch) {
            int original_picture = -1, last_picture = -1, draws = 0;
            for (unsigned i = 0; i < batch; ++i) {
                const uint64_t frame = first + i;
                if (frame % unsigned(mode + 1) == 0) {
                    original_picture = int(frame);
                    if (app::keep_software_picture(frame, batch - i, mode)) { last_picture = int(frame); ++draws; }
                }
            }
            check(last_picture == original_picture, "catch-up changed the selected picture");
            check(draws <= 1, "catch-up rendered a superseded picture");
        }
    check(app::keep_software_picture(0, 2, 2), "keep early picture when final update is not scheduled to draw");
    check(!app::keep_software_picture(0, 4, 2), "discard early picture when a later scheduled picture replaces it");
    std::puts("PASS: catch-up count and last scheduled picture in all draw modes");
}
