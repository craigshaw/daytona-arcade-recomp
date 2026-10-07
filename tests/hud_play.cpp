// Local-ROM regression: complete overlays, renderer/aspect/toggle transitions,
// and unchanged pixels outside the old/new HUD footprints. No guest writes.
#include "app/gpu/gpu_renderer.h"
#include "runtime/game_loop.h"
#include "../tools/common/input_script.h"
#include "../tools/common/nvram.h"
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace {
std::string context;
void require(bool ok, const char *message) { if (!ok) throw std::runtime_error(context + message); }
bool footprint(int x, int y, int margin) {
    for (int left : {0, margin}) if (x >= left && x < left + 125 && y < 130) return true;
    for (int left : {margin, 2 * margin}) if (x >= left + 336 && x < left + 496 && y < 304) return true;
    return false;
}
void unchanged_outside(const std::vector<uint32_t> &off, const std::vector<uint32_t> &on, int width, bool active) {
    size_t changed = 0;
    for (int y = 0; y < 384; ++y) for (int x = 0; x < width; ++x) {
        if (off[size_t(y) * width + x] == on[size_t(y) * width + x]) continue;
        ++changed;
        require(active && footprint(x, y, (width - 496) / 2), "HUD changed pixels outside its footprints");
    }
    require(!active || changed > 100, "active HUD relocation made no visible change");
}
}

void check_play_hud(SDL_GPUDevice *device, app::GpuRenderer &gpu, SDL_GPUTexture *target,
                    SDL_GPUTransferBuffer *download, const char *images, const char *nvram,
                    const char *captures, int count, char **inputs) {
    unsigned samples = 0;
    for (int course = 0; course < count; ++course) {
        tools::Script script; script.load(inputs[course]);
        // Exercise all four views for Beginner as well as the two harder courses.
        for (unsigned view = 1; view <= 4; ++view)
            script.lines.push_back({3200 + (view - 1) * 600, 3209 + (view - 1) * 600, "vr" + std::to_string(view), 1});
        rt::GameLoop game(images); tools::load_nvram(game, nvram);
        auto &video = game.board().video();
        game.set_aspect(32.0 / 9.0); game.set_hud_edges(true);
        video.panorama().original = true; video.set_external_3d(true, true);
        unsigned active_frames = 0, map_polys = 0, panel_polys = 0, samples_before = samples;
        bool previous = false, saw_exit = false;
        for (unsigned frame = 1; frame <= 18000; ++frame) {
            context = std::string(M2_ROMSET) + " course " + std::to_string(course) + " frame " + std::to_string(frame) + ": ";
            game.run_frame(script.at(game.board().frame()));
            if (game.sound()) { game.sound()->take_fm(); game.sound()->take_pcm(); }
            const bool active = video.cpu_front();
            active_frames += active;
            saw_exit |= previous && !active;
            const auto &polys = video.gpu_polys();
            const auto mem = video.gpu_mem(); const int windows = video.gpu_windows();
            if (active) for (const auto &p : polys) {
                const int shift = video.raster().hud().polygon_shift(p, video.crtc_x(), video.crtc_y());
                // Independent coverage assertion: every tiny-depth map polygon
                // must move, including markers and Expert's leftmost section.
                if (p.z == 0 && p.v[0].p[0] > 0 && p.v[0].p[0] < .001f) {
                    require(shift == video.margin(), "incomplete course map relocation"); ++map_polys;
                }
                if (shift && p.z == 0x600) ++panel_polys;
            }
            const bool sample = frame % 200 == 0 || active != previous || (frame >= 2400 && frame <= 3000 && frame % 20 == 0);
            previous = active;
            if (!sample) continue;
            for (int margin : {0, 93, 435}) {
                video.set_wide_margin(margin);
                const int width = video.width();
                auto cpu = [&](bool edges) {
                    video.set_hud_edges(edges); video.set_external_3d(false);
                    video.screen_update(polys, windows, mem);
                    return video.screen();
                };
                auto hardware = [&](bool edges) {
                    video.set_hud_edges(edges); video.set_external_3d(true, true);
                    video.screen_update(polys, windows, mem);
                    auto *command = SDL_AcquireGPUCommandBuffer(device);
                    require(command != nullptr, SDL_GetError());
                    gpu.render(command, target, width, 384, video);
                    auto *copy = SDL_BeginGPUCopyPass(command);
                    SDL_GPUTextureRegion source{}; source.texture = target; source.w = width; source.h = 384; source.d = 1;
                    SDL_GPUTextureTransferInfo dest{}; dest.transfer_buffer = download; dest.pixels_per_row = width;
                    SDL_DownloadFromGPUTexture(copy, &source, &dest); SDL_EndGPUCopyPass(copy);
                    auto *fence = SDL_SubmitGPUCommandBufferAndAcquireFence(command);
                    require(fence != nullptr, SDL_GetError());
                    require(SDL_WaitForGPUFences(device, true, &fence, 1), SDL_GetError()); SDL_ReleaseGPUFence(device, fence);
                    auto *pixels = static_cast<const uint32_t *>(SDL_MapGPUTransferBuffer(device, download, false));
                    require(pixels != nullptr, SDL_GetError());
                    std::vector<uint32_t> result(pixels, pixels + width * 384);
                    SDL_UnmapGPUTransferBuffer(device, download);
                    return result;
                };
                const auto off = cpu(false), on = cpu(true);
                require(video.cpu_front() == (active && margin != 0), "aspect/toggle lost HUD detection");
                unchanged_outside(off, on, width, video.cpu_front());
                const auto gpu_off = hardware(false), gpu_on = hardware(true);
                unchanged_outside(gpu_off, gpu_on, width, video.cpu_front());
                require(cpu(true) == on, "hardware to software left stale offsets");
                require(cpu(false) == off, "turning HUD off did not restore the original pixels");
                require(cpu(true) == on, "turning HUD back on did not restore the edge layout");
                if (frame == 3600 && margin == 435 && std::string(captures) != "-") {
                    std::filesystem::create_directories(captures);
                    const auto prefix = std::filesystem::path(captures) / (std::string(M2_ROMSET) + "-" + std::to_string(course));
                    for (bool edges : {false, true}) {
                        const auto &pixels = edges ? on : off;
                        std::ofstream f(prefix.string() + (edges ? "-on.rgb" : "-off.rgb"), std::ios::binary);
                        f.write(reinterpret_cast<const char *>(pixels.data()), std::streamsize(pixels.size() * 4));
                        require(bool(f), "cannot save HUD capture");
                    }
                }
                ++samples;
            }
            video.set_wide_margin(435); video.set_hud_edges(true); video.set_external_3d(true, true);
        }
        require(active_frames > 3000 && map_polys && panel_polys && saw_exit, "replay did not cover a race and HUD exit");
        std::printf("%s %s: %u active frames, %u map/%u panel polygons, %u aspect samples; race exit covered\n",
            M2_ROMSET, inputs[course], active_frames, map_polys, panel_polys, samples - samples_before);
        std::fflush(stdout);
    }
    std::printf("PASS: %u HUD samples, native/16:9/32:9, CPU/GPU toggles and renderer transitions\n", samples);
}
