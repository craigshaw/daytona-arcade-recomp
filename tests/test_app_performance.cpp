#include "app/performance.h"
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <string>
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "%d: %s\n", __LINE__, #x); std::exit(1); } } while (0)

int main() {
    app::Performance perf(50); // exact arithmetic: two game updates per 40 ms
    app::Performance::Sample row;
    row.wall = 40000000; row.presents = 1;
    row.update({10000000, 2000000, 3000000, 1000000}, {100, 200, 300, 400, 5, 6, true}, 20000000);
    row.update({25000000, 4000000, 6000000, 2000000}, {200, 400, 600, 800, 10, 12, true}, 20000000);
    CHECK(row.updates == 2 && row.slow == 1 && row.max_update == 25000000);
    CHECK(row.game.total == 35000000 && row.game.core() == 17000000);
    CHECK(row.video.tile_cache == 300 && row.video.tiles_rebuilt == 15 && row.video.characters_changed == 18);
    perf.push(row);
    CHECK(perf.size() == 0 && !perf.enabled());
    perf.cycle(); perf.push(row);
    CHECK(std::string(perf.text()).find("Game 50.0 / 50.00 FPS (100%)") != std::string::npos);
    CHECK(std::string(perf.text()).find("Video") == std::string::npos);
    perf.cycle();
    CHECK(perf.detailed() && perf.size() == 0 && *perf.text() == 0);
    perf.push(row);
    CHECK(std::string(perf.text()).find("Present submits 25.0/s") != std::string::npos);
    CHECK(std::string(perf.text()).find("avg 17.50 max 25.00 ms") != std::string::npos);
    // The newest capacity rows survive, in time order, after wrapping twice.
    for (size_t i = 1; i <= 2 * perf.Capacity; ++i) { row.frame = i; perf.push(row); }
    CHECK(perf.size() == perf.Capacity);
    CHECK(perf.at(0).frame == perf.Capacity + 1 && perf.at(perf.size() - 1).frame == 2 * perf.Capacity);
    // Pauses/settings changes start a new window; no stale rows enter an export.
    perf.clear(); row.frame = 99; perf.push(row);
    std::ostringstream out;
    perf.write_csv(out, "test", "test-gpu");
    const auto csv = out.str();
    CHECK(csv.find("# romset=test gpu=test-gpu mode=timings") != std::string::npos);
    CHECK(csv.find("\n99,40.000000,2,1,25.000000,1,0.000000,35.000000,17.000000,") != std::string::npos);
    CHECK(csv.find(",0.000300,0.000600,0.000900,0.001200,") != std::string::npos);
    perf.cycle(); perf.push(row);
    CHECK(!perf.enabled() && perf.size() == 0);
    std::printf("PASS: timing aggregation, game/present FPS, ring wrap, reset and CSV units (%zu bytes)\n", sizeof perf);
}
