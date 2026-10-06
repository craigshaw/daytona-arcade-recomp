#pragma once
#include "app/performance.h"
#include "imgui.h"

namespace app {
inline void draw_performance(const Performance &perf, bool paused, const char *notice = "") {
    if (paused) {
        // The launcher covers the viewport. Keep export feedback above it,
        // away from its title/tabs, without taking keyboard or mouse focus.
        if (*notice) {
            const auto *view = ImGui::GetMainViewport();
            const auto size = ImGui::CalcTextSize(notice);
            const ImVec2 pos(view->Pos.x + view->Size.x - size.x - 16, view->Pos.y + 12);
            auto *draw = ImGui::GetForegroundDrawList();
            draw->AddRectFilled(ImVec2(pos.x - 6, pos.y - 4), ImVec2(pos.x + size.x + 6, pos.y + size.y + 4), IM_COL32(0, 0, 0, 230));
            draw->AddText(pos, IM_COL32(255, 255, 255, 255), notice);
        }
        return;
    }
    ImGui::SetNextWindowPos(ImVec2(12, 12), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.8f);
    constexpr auto flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoNav;
    ImGui::Begin("Performance", nullptr, flags);
    ImGui::TextUnformatted(*perf.text() ? perf.text() : "Measuring...");
    ImGui::End();
}
} // namespace app
