#include "runtime/hud.h"
#include <cstdio>
#include <cstdlib>

static void check(bool ok, const char *message) {
    if (!ok) { std::fprintf(stderr, "%s\n", message); std::exit(1); }
}

static rt::GeoPoly quad(float x0, float x1, float y0, float y1, float depth, unsigned z, unsigned material) {
    rt::GeoPoly p{};
    p.num_vertices = 4; p.center[0] = 248; p.center[1] = 316;
    p.z = uint16_t(z); p.texheader[0] = uint16_t(material);
    const float xs[] = {x0, x1, x1, x0}, ys[] = {y0, y0, y1, y1};
    for (int i = 0; i < 4; ++i) { p.v[i].x = (xs[i] - 248) * depth; p.v[i].y = (196 - ys[i]) * depth; p.v[i].p[0] = depth; }
    return p;
}

int main() {
    rt::RaceHud hud;
    for (float top : {84.2f, 67.4f}) {
        auto panel = quad(384.8f, 462.1f, top, top + 81.7f, 1.5f, 0x600, 0x8000);
        auto map = quad(342, 482, 172, 268, 0.00011f, 0, 0); // Revision A Expert's full outline
        auto car = quad(421, 426, top + 36, top + 44, 1.49997f, 0x600, 0);
        for (int margin : {59, 93, 200, 435}) {
            hud.update(true, margin, {panel}, 0, 128);
            check(hud.active(), "both revisions must activate");
            check(hud.polygon_shift(panel, 0, 128) == margin, "panel must move");
            check(hud.polygon_shift(car, 0, 128) == margin, "traffic icons must follow the panel");
            check(hud.polygon_shift(map, 0, 128) == margin, "the complete Expert map must move");
            check(!hud.update(true, margin, {panel}, 0, 128), "stable placement should not invalidate the raster cache");
            auto reject = [&](rt::GeoPoly p, const char *reason) { check(!hud.polygon_shift(p, 0, 128), reason); };
            auto bad = map; bad.z = 1; reject(bad, "map region with another sort bucket is not HUD");
            bad = map; bad.texheader[0] = 0x4000; reject(bad, "textured scenery must not move");
            bad = map; bad.v[1].p[0] *= 2; reject(bad, "perspective geometry must not move");
            reject(quad(342, 482, 172, 268, 10, 0, 0), "sort zero alone must not move bodywork");
            reject(quad(150, 200, 172, 268, 0.00011f, 0, 0), "centre overlay must stay centred");
            reject(quad(385, 462, 280, 350, 1.5f, 0x600, 0), "panel bucket outside the panel must not move");
            hud.update(false, margin, {panel}, 0, 128);
            check(!hud.active() && !hud.polygon_shift(map, 0, 128), "toggle off clears polygon placement");
            hud.update(true, margin, {map}, 0, 128);
            check(!hud.active(), "map alone must not activate race HUD");
        }
        hud.update(true, 0, {panel}, 0, 128);
        check(!hud.active(), "native aspect must disable relocation");
        for (int variant = 0; variant < 5; ++variant) {
            auto bad = panel;
            if (variant == 0) bad.z = 0x601;
            if (variant == 1) bad.texheader[0] = 0;
            if (variant == 2) for (auto &v : bad.v) v.x -= 40;
            if (variant == 3) bad.v[0].p[0] = 0.5f;
            if (variant == 4) bad.num_vertices = 2;
            hud.update(true, 435, {bad}, 0, 128);
            check(!hud.active(), "similar scenery must not open the race HUD gate");
        }
    }
    std::puts("PASS: both HUD layouts, complete map, scenery rejection, aspect/toggle reset");
}
