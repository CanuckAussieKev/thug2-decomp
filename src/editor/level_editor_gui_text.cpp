#include "editor/level_editor_gui_text.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <string>

namespace frontend::level_editor::gui {

namespace {

// (ImGui's TextV runs the made text through TextEx with NoWidthForLargeClippedText - the same here; the text ends at its
// terminator, as the formatted text did there.)
void put_unformatted(const std::string& made)
{
    ImGui::TextEx(made.c_str(), nullptr, ImGuiTextFlags_NoWidthForLargeClippedText);
}

} // namespace

void put_text(const std::string& made) { put_unformatted(made); }

void put_disabled(const std::string& made)
{
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
    put_unformatted(made);
    ImGui::PopStyleColor();
}

void put_colored(const ImVec4& colour, const std::string& made)
{
    ImGui::PushStyleColor(ImGuiCol_Text, colour);
    put_unformatted(made);
    ImGui::PopStyleColor();
}

void put_wrapped(const std::string& made)
{
    // keep a wrap position already set
    const bool need_backup = ImGui::GetCurrentWindow()->DC.TextWrapPos < 0.0F;
    if (need_backup) {
        ImGui::PushTextWrapPos(0.0F);
    }
    put_unformatted(made);
    if (need_backup) {
        ImGui::PopTextWrapPos();
    }
}

void put_tooltip(const std::string& made)
{
    if (!ImGui::BeginTooltipEx(ImGuiTooltipFlags_OverridePrevious, ImGuiWindowFlags_None)) {
        return;
    }
    put_unformatted(made);
    ImGui::EndTooltip();
}

} // namespace frontend::level_editor::gui
