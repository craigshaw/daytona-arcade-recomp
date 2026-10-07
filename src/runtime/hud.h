// Host-only placement of Daytona's race overlays, in native 496x384 coordinates.
#pragma once

#include "runtime/geo.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace rt {

class RaceHud {
public:
    struct Box {
        float x0, x1, y0, y1;
        bool contains(const Box &b, float tolerance = 0) const {
            return b.x0 >= x0 - tolerance && b.x1 <= x1 + tolerance &&
                   b.y0 >= y0 - tolerance && b.y1 <= y1 + tolerance;
        }
        bool operator==(const Box &) const = default;
    };

    // Shared by the software and hardware renderers. A panel alone gates the
    // entire layout; neither a near sort bucket nor a screen region is enough.
    bool update(bool enabled, int margin, const std::vector<GeoPoly> &polys, int crtc_x, int crtc_y) {
        const int old_shift = shift_;
        const Box old_panel = panel_;
        clear();
        if (enabled && margin) for (const auto &poly : polys) {
            if (poly.z != 0x0600 || poly.texheader[0] != 0x8000 || !panel_depth(poly)) continue;
            const Box b = project(poly, crtc_x, crtc_y);
            // Revision A's panel is 16.8 pixels lower than Deluxe '93's.
            // Keep both known outlines explicit rather than enlarging the gate.
            const bool x = std::abs(b.x0 - 384.8f) <= 2 && std::abs(b.x1 - 462.1f) <= 2;
            const bool a = std::abs(b.y0 - 84.2f) <= 2 && std::abs(b.y1 - 165.9f) <= 2;
            const bool deluxe = std::abs(b.y0 - 67.4f) <= 2 && std::abs(b.y1 - 149.1f) <= 2;
            if (x && (a || deluxe)) { panel_ = b; shift_ = margin; break; }
        }
        return shift_ != old_shift || panel_ != old_panel;
    }
    void clear() { shift_ = 0; panel_ = {}; }
    bool active() const { return shift_ != 0; }

    int polygon_shift(const GeoPoly &poly, int crtc_x, int crtc_y) const {
        if (!shift_) return 0;
        if (poly.z == 0x0600 && panel_depth(poly))
            return panel_.contains(project(poly, crtc_x, crtc_y), 1.5f) ? shift_ : 0;
        // Course outline and moving car markers are solid, flat overlays at
        // tiny positive depth (roughly 0.00009..0.00011), in sort bucket zero.
        // Require that signature as well as their region to exclude bodywork.
        if (poly.z == 0 && poly.texheader[0] == 0 && poly.texheader[1] == 0 && poly.texheader[2] == 0 &&
            flat_depth(poly, 0.00001f, 0.001f) &&
            Box{336, 496, 150, 304}.contains(project(poly, crtc_x, crtc_y))) return shift_;
        return 0;
    }

private:
    int shift_ = 0;
    Box panel_{};
    static bool flat_depth(const GeoPoly &poly, float lo, float hi) {
        if (poly.num_vertices < 3 || poly.num_vertices > 8) return false;
        const float depth = poly.v[0].p[0];
        if (!(depth >= lo && depth <= hi)) return false;
        for (int i = 1; i < poly.num_vertices; ++i) if (poly.v[i].p[0] != depth) return false;
        return true;
    }
    static bool panel_depth(const GeoPoly &poly) { return flat_depth(poly, 1.49f, 1.51f); }
    static Box project(const GeoPoly &poly, int crtc_x, int crtc_y) {
        Box b{1e9f, -1e9f, 1e9f, -1e9f};
        for (int i = 0; i < poly.num_vertices; ++i) {
            const auto &v = poly.v[i];
            const float z = v.p[0] + std::numeric_limits<float>::min();
            const float x = float(crtc_x + poly.center[0]) + v.x / z;
            const float y = float(384 - poly.center[1] + crtc_y) - v.y / z;
            if (!std::isfinite(x) || !std::isfinite(y)) return {1e9f, 1e9f, 1e9f, 1e9f};
            b.x0 = std::min(b.x0, x); b.x1 = std::max(b.x1, x);
            b.y0 = std::min(b.y0, y); b.y1 = std::max(b.y1, y);
        }
        return b;
    }
};

} // namespace rt
