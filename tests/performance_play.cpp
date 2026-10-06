// Explicit local-ROM benchmark. Same replay/settings and overlay as the app;
// offscreen upload + UI with one GPU frame in flight, no vsync/audio device.
// Fences belong to this fixture only, never to the playable profiler.
#include "app/performance_overlay.h"
#include "app/video_settings.h"
#include "../tools/common/input_script.h"
#include "../tools/common/nvram.h"
#include <SDL3/SDL.h>
#include "backends/imgui_impl_sdl3.h"
#include "backends/imgui_impl_sdlgpu3.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <numeric>
#include <stdexcept>

static void require(bool ok) { if (!ok) throw std::runtime_error(SDL_GetError()); }
int main(int argc, char **argv) {
    if (argc < 7) {
        std::fprintf(stderr, "usage: m2perfcheck IMAGES NVRAM INPUTS ASPECT off|fps|timings OUTPUT_PREFIX [no-panorama|no-hud|skip] [--budget N] [--from N]\n");
        return 2;
    }
    try {
        constexpr double hz = 16000000.0 / (656.0 * 424.0);
        constexpr size_t count = app::Performance::Capacity;
        size_t warmup = 3000;
        tools::Script script; script.load(argv[3]);
        app::Config cfg; cfg.aspect = argv[4]; cfg.renderer = "software"; cfg.hud_edges = true;
        std::string control;
        bool budget_check = false;
        for (int i = 7; i < argc; ++i) {
            const std::string arg = argv[i];
            if (arg == "--budget") {
                if (++i == argc || !rt::parse_scenery_budget(argv[i], cfg.draw_budget)) throw std::runtime_error("invalid budget");
                budget_check = true;
            } else if (arg == "--from") {
                if (++i == argc) throw std::runtime_error("missing first frame");
                const auto first = std::stoull(argv[i]);
                if (!first || first > 1000000) throw std::runtime_error("invalid first frame");
                warmup = size_t(first - 1);
            } else if (arg == "no-hud" || arg == "no-panorama" || arg == "skip") control = arg;
            else throw std::runtime_error("unknown control: " + arg);
        }
        if (control == "no-hud") cfg.hud_edges = false;
        if (control == "skip") cfg.draw_mode = 2;
        rt::GameLoop game(argv[1]); tools::load_nvram(game, argv[2]);
        app::apply_video_settings(game, cfg, false);
        auto &video = game.board().video();
        game.board().scenery()->diagnostics = budget_check;
        if (control == "no-panorama") video.panorama().original = false;
        app::Performance perf(hz);
        if (!std::strcmp(argv[5], "fps")) perf.cycle();
        else if (!std::strcmp(argv[5], "timings")) { perf.cycle(); perf.cycle(); }
        else if (std::strcmp(argv[5], "off")) throw std::runtime_error("invalid mode");
        const auto clock = +[]() -> uint64_t { return SDL_GetTicksNS(); };
        game.set_profile_clock(perf.detailed() ? clock : nullptr);
        video.set_profile_clock(perf.detailed() ? clock : nullptr);
        require(SDL_Init(SDL_INIT_VIDEO));
        const int width = game.screen_width(), height = rt::Video::H;
        auto *window = SDL_CreateWindow("Performance fixture", width, height, SDL_WINDOW_HIDDEN);
        require(window != nullptr);
        auto *device = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_DXIL | SDL_GPU_SHADERFORMAT_MSL, false, nullptr);
        require(device != nullptr);
        ImGui::CreateContext(); ImGui::GetIO().IniFilename = nullptr;
        ImGui_ImplSDL3_InitForSDLGPU(window);
        ImGui_ImplSDLGPU3_InitInfo ii{};
        ii.Device = device; ii.ColorTargetFormat = SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM;
        require(ImGui_ImplSDLGPU3_Init(&ii));
        SDL_GPUTextureCreateInfo ti{};
        ti.type = SDL_GPU_TEXTURETYPE_2D; ti.format = ii.ColorTargetFormat;
        ti.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
        ti.width = width; ti.height = height; ti.layer_count_or_depth = ti.num_levels = 1;
        auto *target = SDL_CreateGPUTexture(device, &ti); require(target != nullptr);
        SDL_GPUTransferBufferCreateInfo bi{}; bi.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD; bi.size = width * height * 4;
        auto *upload = SDL_CreateGPUTransferBuffer(device, &bi); require(upload != nullptr);
        std::array<double, count> elapsed{};
        struct BudgetSample {
            uint64_t hash = 0, instructions = 0, tgp = 0, cells = 0;
            uint32_t frame = 0, budget = 0, cost = 0, checks = 0, rejected = 0, selected = 0;
            bool hud = false;
        };
        std::array<BudgetSample, count> budget_samples{};
        uint64_t fingerprint = 0;
        for (size_t frame = 0; frame < warmup + count; ++frame) {
            SDL_PumpEvents();
            if (frame == warmup) perf.clear();
            const auto instructions = game.instructions(), tgp = game.board().tgp().tgp_instructions();
            const auto begin = clock();
            app::Performance::Sample row;
            game.run_frame(script.at(game.board().frame()));
            if (perf.detailed()) row.update(game.last_profile(), video.last_profile(), 1e9 / hz);
            else row.updates = 1;
            // Drain generated audio; this fixture has no playback device.
            if (game.sound()) { game.sound()->take_fm(); game.sound()->take_pcm(); }
            auto before = clock();
            auto *command = SDL_AcquireGPUCommandBuffer(device); require(command != nullptr);
            auto *data = SDL_MapGPUTransferBuffer(device, upload, true); require(data != nullptr);
            std::memcpy(data, game.screen().data(), size_t(width) * height * 4);
            SDL_UnmapGPUTransferBuffer(device, upload);
            auto *copy = SDL_BeginGPUCopyPass(command);
            SDL_GPUTextureTransferInfo source{}; source.transfer_buffer = upload; source.pixels_per_row = width;
            SDL_GPUTextureRegion dest{}; dest.texture = target; dest.w = width; dest.h = height; dest.d = 1;
            SDL_UploadToGPUTexture(copy, &source, &dest, true); SDL_EndGPUCopyPass(copy);
            row.render = clock() - before;
            before = clock();
            if (perf.enabled()) {
                ImGui_ImplSDLGPU3_NewFrame(); ImGui_ImplSDL3_NewFrame(); ImGui::NewFrame();
                app::draw_performance(perf, false); ImGui::Render();
                ImGui_ImplSDLGPU3_PrepareDrawData(ImGui::GetDrawData(), command);
                SDL_GPUColorTargetInfo ct{}; ct.texture = target;
                ct.load_op = SDL_GPU_LOADOP_LOAD; ct.store_op = SDL_GPU_STOREOP_STORE;
                auto *pass = SDL_BeginGPURenderPass(command, &ct, 1, nullptr);
                ImGui_ImplSDLGPU3_RenderDrawData(ImGui::GetDrawData(), command, pass); SDL_EndGPURenderPass(pass);
            }
            row.overlay = clock() - before;
            before = clock();
            auto *fence = SDL_SubmitGPUCommandBufferAndAcquireFence(command); require(fence != nullptr);
            row.submit = clock() - before;
            before = clock();
            require(SDL_WaitForGPUFences(device, true, &fence, 1)); SDL_ReleaseGPUFence(device, fence);
            row.wait = clock() - before;
            row.wall = clock() - begin; row.frame = game.frames(); row.width = width;
            row.course = game.board().read_byte(0x501460); row.panorama = video.panorama_active();
            row.hud = cfg.hud_edges; row.skip = cfg.draw_mode;
            row.budget = cfg.draw_budget;
            row.polygons = uint32_t(video.gpu_polys().size());
            perf.push(row);
            if (frame >= warmup) {
                elapsed[frame - warmup] = double(clock() - begin) * 1e-6; // includes sampling/formatting
                if (budget_check) {
                    const auto &m = game.board().scenery()->measured;
                    auto &b = budget_samples[frame - warmup];
                    b.frame = uint32_t(game.frames()); b.budget = game.board().read_dword(0x5010f4);
                    b.cost = m.cost_peak; b.checks = m.budget_checks; b.rejected = m.budget_rejections; b.selected = m.selected_cells;
                    b.instructions = game.instructions() - instructions; b.tgp = game.board().tgp().tgp_instructions() - tgp;
                    b.hud = video.cpu_front();
                    // Hash every CPU pixel after timing; no images or live I/O.
                    b.hash = video.screen_hash();
                    for (unsigned c = 0; c < game.board().read_byte(0x5016c0) && c < 63; ++c)
                        b.cells = b.cells * 1099511628211ull ^ game.board().read_byte(0x5016c1 + c);
                }
                if (frame % 128 == 0) fingerprint = (fingerprint * 1099511628211ull) ^ video.screen_hash();
                if (cfg.draw_mode && frame % 3 && perf.detailed()) {
                    if (row.video.tile_cache || row.video.raster || row.video.composite)
                        throw std::runtime_error("skipped video frame retained stale timings");
                }
            }
        }
        std::ofstream capture(std::string(argv[6]) + ".csv");
        capture << "# Offscreen fixture: wait_ms is a completion fence; no vsync/audio device.\n";
        perf.write_csv(capture, M2_ROMSET, SDL_GetGPUDeviceDriver(device));
        if (budget_check) {
            std::ofstream checks(std::string(argv[6]) + ".budget.csv");
            checks << "frame,budget,cost_peak,checks,rejections,selected_cells,hud_active,screen_hash,i960_instructions,tgp_instructions,cells_hash\n";
            for (const auto &b : budget_samples)
                checks << b.frame << ',' << b.budget << ',' << b.cost << ',' << b.checks << ',' << b.rejected << ',' << b.selected << ','
                       << b.hud << ',' << b.hash << ',' << b.instructions << ',' << b.tgp << ',' << b.cells << '\n';
        }
        const auto mean = std::accumulate(elapsed.begin(), elapsed.end(), 0.0) / count;
        std::sort(elapsed.begin(), elapsed.end());
        std::printf("%s %s %s %s: %zu updates, mean %.4f p50 %.4f p95 %.4f p99 %.4f max %.4f ms; pixels %016llx; buffer %zu bytes\n",
            M2_ROMSET, argv[4], argv[5], control.c_str(), count, mean, elapsed[count / 2], elapsed[count * 95 / 100],
            elapsed[count * 99 / 100], elapsed.back(), static_cast<unsigned long long>(fingerprint), sizeof perf);
        SDL_ReleaseGPUTransferBuffer(device, upload); SDL_ReleaseGPUTexture(device, target);
        ImGui_ImplSDLGPU3_Shutdown(); ImGui_ImplSDL3_Shutdown(); ImGui::DestroyContext();
        SDL_DestroyGPUDevice(device); SDL_DestroyWindow(window); SDL_Quit();
    } catch (const std::exception &e) { std::fprintf(stderr, "m2perfcheck: %s\n", e.what()); return 1; }
}
