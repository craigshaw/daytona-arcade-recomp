#include "app/performance.h"
#include <algorithm>
#include <cstdio>
#include <iomanip>
#include <ostream>

namespace app {
void Performance::Sample::update(const rt::FrameProfile &f, const rt::VideoProfile &v, double budget_ns) {
    ++updates;
    game.total += f.total; game.geometry += f.geometry; game.video += f.video; game.sound += f.sound;
    video.tile_cache += v.tile_cache; video.tile_draw += v.tile_draw;
    video.raster += v.raster; video.composite += v.composite;
    video.tiles_rebuilt += v.tiles_rebuilt; video.characters_changed += v.characters_changed;
    max_update = std::max(max_update, f.total);
    slow += double(f.total) > budget_ns;
}

void Performance::clear() {
    next_ = count_ = 0;
    refresh_elapsed_ = 0;
    text_[0] = '\0';
}

const Performance::Sample &Performance::at(size_t index) const {
    return samples_[(next_ + Capacity - count_ + index) % Capacity];
}

void Performance::push(const Sample &sample) {
    if (!enabled()) return;
    samples_[next_] = sample;
    next_ = (next_ + 1) % Capacity;
    count_ = std::min(count_ + 1, Capacity);
    refresh_elapsed_ += sample.wall;
    if (refresh_elapsed_ >= 250000000 || count_ == 1) {
        refresh();
        refresh_elapsed_ = 0;
    }
}

void Performance::refresh() {
    Sample sum;
    uint64_t wall = 0, worst = 0, max_update = 0;
    size_t loops = 0;
    for (size_t i = count_; i && wall < 1000000000; --i) {
        const auto &s = at(i - 1);
        wall += s.wall; worst = std::max(worst, s.wall); max_update = std::max(max_update, s.max_update);
        sum.updates += s.updates; sum.presents += s.presents;
        sum.game.total += s.game.total; sum.game.geometry += s.game.geometry;
        sum.game.video += s.game.video; sum.game.sound += s.game.sound;
        sum.video.tile_cache += s.video.tile_cache; sum.video.tile_draw += s.video.tile_draw;
        sum.video.raster += s.video.raster; sum.video.composite += s.video.composite;
        sum.render += s.render; sum.overlay += s.overlay; sum.wait += s.wait;
        ++loops;
    }
    const double fps = wall ? double(sum.updates) * 1e9 / double(wall) : 0;
    const double per_update = sum.updates ? 1e-6 / sum.updates : 0;
    const double per_loop = loops ? 1e-6 / loops : 0;
    if (!detailed()) {
        std::snprintf(text_.data(), text_.size(), "Game %.1f / %.2f FPS (%.0f%%)  |  F10: timings/off", fps, hz_, fps * 100 / hz_);
        return;
    }
    std::snprintf(text_.data(), text_.size(),
        "Game %.1f / %.2f FPS (%.0f%%)  |  Present submits %.1f/s\n"
        "CPU/update avg %.2f max %.2f ms  |  Loop max %.2f ms\n"
        "Core %.2f  Geo %.2f  Sound %.2f  Video %.2f ms/update\n"
        "Video: prep %.2f  tiles %.2f  raster %.2f  compose %.2f ms\n"
        "Render/upload %.2f  Wait %.2f  Overlay %.3f ms/loop\n"
        "F10: off  |  F9: save recent timings and pause",
        fps, hz_, fps * 100 / hz_, wall ? double(sum.presents) * 1e9 / double(wall) : 0,
        sum.game.total * per_update, max_update * 1e-6, worst * 1e-6,
        sum.game.core() * per_update, sum.game.geometry * per_update, sum.game.sound * per_update, sum.game.video * per_update,
        sum.video.tile_cache * per_update, sum.video.tile_draw * per_update, sum.video.raster * per_update, sum.video.composite * per_update,
        sum.render * per_loop, sum.wait * per_loop, sum.overlay * per_loop);
}

void Performance::write_csv(std::ostream &out, const char *romset, const char *gpu) const {
    out << "# romset=" << romset << " gpu=" << gpu << " mode=" << (detailed() ? "timings" : enabled() ? "fps" : "off") << '\n'
        << "# One row per window iteration; stage times sum all updates in that row. Video sub-stages are included in video_ms.\n"
        << "# Wait is host swapchain wait, not GPU execution time. Sound measures reference audio, not native callbacks.\n"
        << "frame,wall_ms,updates,presents,max_update_ms,slow_updates,discarded_ms,game_ms,core_ms,geometry_ms,video_ms,sound_ms,"
           "prep_ms,tiles_ms,raster_ms,compose_ms,render_ms,overlay_ms,wait_ms,submit_ms,tiles_rebuilt,characters_changed,"
           "width,hardware,course,panorama,hud_edges,draw_distance,draw_budget,draw_mode,supersampling,native_audio,polygons\n";
    out << std::fixed << std::setprecision(6);
    for (size_t i = 0; i < count_; ++i) {
        const auto &s = at(i);
        out << s.frame << ',' << s.wall * 1e-6 << ',' << s.updates << ',' << s.presents << ','
            << s.max_update * 1e-6 << ',' << s.slow << ',' << s.discarded * 1e-6 << ','
            << s.game.total * 1e-6 << ',' << s.game.core() * 1e-6 << ',' << s.game.geometry * 1e-6 << ','
            << s.game.video * 1e-6 << ',' << s.game.sound * 1e-6 << ',' << s.video.tile_cache * 1e-6 << ','
            << s.video.tile_draw * 1e-6 << ',' << s.video.raster * 1e-6 << ',' << s.video.composite * 1e-6 << ','
            << s.render * 1e-6 << ',' << s.overlay * 1e-6 << ',' << s.wait * 1e-6 << ',' << s.submit * 1e-6 << ','
            << s.video.tiles_rebuilt << ',' << s.video.characters_changed << ',' << s.width << ',' << s.hardware << ','
            << s.course << ',' << s.panorama << ',' << s.hud << ',' << s.distance << ',' << s.budget << ',' << s.skip << ','
            << s.scale << ',' << s.native_audio << ',' << s.polygons << '\n';
    }
}
} // namespace app
