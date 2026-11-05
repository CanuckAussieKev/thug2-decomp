// NOT ORIGINAL - the level editor's HUD (ours). See level_editor_hud.h.

#include "editor/level_editor_hud.h"

#include <SDL2/SDL.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <iterator>
#include <map>
#include <array>
#include <string>
#include <string_view>
#include <vector>

#include "base/core/checksums.h"
#include "engine/script/cscript.h"
#include "engine/script/param_struct.h"
#include "base/core/checked_index.h"
#include <gsl/pointers>
#include "engine/memory/mem.h"
#include "base/core/format.h"
#include "base/core/no_destroy.h"

namespace frontend::level_editor::hud {
namespace {

constexpr std::size_t kStrandParamsSize = 8; // the script structure's storage for a caption's parameters
constexpr std::size_t kStrandIdSize = 32; // "level_editor_hud_<n>"
constexpr double kMillisecondsPerSecond = 1000.0;
constexpr int kMessageStrand = 7; // the caption line the status message uses
constexpr std::size_t kPanelParamsSize = 8; // the script structure's storage for the panel's parameters
constexpr std::size_t kPanelLineIdSize = 32; // "level_editor_panel_<n>"

constexpr size_t kMaxText = 90;
constexpr uint32_t kId = 0x40C698AFU;   // id
constexpr uint32_t kText = 0xC4745838U; // text
// (the white2 sprite's size at scale 1 in the 640 x 480 screen: across, the bottom bar's 200.5 spans 640; down,
// about 8 - measured on screen)
constexpr float kSpriteUnit = 640.0F / 200.5F, kSpriteUnitDown = 8.0F;
constexpr float kLineHeight = 12.5F;

std::map<std::string, std::string>& shown()
{
    static std::map<std::string, std::string> value; // element id -> text
    return value;
}
// NOT ORIGINAL grouping - level_editor_hud.cpp's state (were globals). Made on first use, never destroyed
// (level_editor_hud_state()).
struct LevelEditorHudState {
    std::string message_;
    uint32_t message_until_{};
    bool panel_{};
    bool elements_ = true; // (false while the UI shows the editor)
    float panel_x_{};
    float panel_y_{};
    float panel_width_{};
    size_t panel_lines_{};
};

LevelEditorHudState& level_editor_hud_state()
{
    static core::NoDestroy<LevelEditorHudState> state;
    return state.get();
}

// A caption's element id and its full text.
struct Caption {
    std::string_view id;
    std::string_view full;
};

void change_caption(Caption caption)
{
    const std::string id(caption.id);
    const std::string full(caption.full);
    if (!level_editor_hud_state().elements_) {
        return;
    }
    const std::string text = full.size() > kMaxText ? full.substr(0, kMaxText) : full;
    auto it = shown().find(id);
    if (it != shown().end() && it->second == text) {
        return;
    }
    shown()[id] = text;
    alignas(4) std::array<uint8_t, kStrandParamsSize> memory{};
    gsl::owner<script::Struct*> params{memory::construct_in<script::Struct>(memory.data())};
    params->put_name_key(checksums::Name{kId}, checksums::hash_from_text(id.c_str()));
    params->append_word(kText, text.c_str());
    script::launch_script("level_editor_hud_set", params, nullptr, false);
    params->get_clear();
}

} // namespace

void get_reset(bool screen_elements)
{
    shown().clear();
    level_editor_hud_state().message_.clear();
    level_editor_hud_state().message_until_ = 0;
    level_editor_hud_state().panel_ = false;
    level_editor_hud_state().elements_ = screen_elements;
}

const std::string& current_message() { return level_editor_hud_state().message_; }

void change_strand(int line, const std::string& text)
{
    std::array<char, kStrandIdSize> id{};
    text::format_to(id, "level_editor_hud_%d", line);
    change_caption({.id = id.data(), .full = text});
}

void get_message(const std::string& text, double seconds)
{
    level_editor_hud_state().message_ = text;
    level_editor_hud_state().message_until_ = SDL_GetTicks() + static_cast<uint32_t>(seconds * kMillisecondsPerSecond);
    change_strand(kMessageStrand, text);
}

void tick()
{
    if (!level_editor_hud_state().message_.empty() && static_cast<int32_t>(SDL_GetTicks() - level_editor_hud_state().message_until_) >= 0) {
        level_editor_hud_state().message_.clear();
        change_strand(kMessageStrand, "");
    }
}

void show_panel(const std::vector<std::string>& lines, float x, float y, float width)
{
    if (!level_editor_hud_state().elements_) {
        return;
    }
    if (!level_editor_hud_state().panel_ || level_editor_hud_state().panel_x_ != x || level_editor_hud_state().panel_y_ != y || level_editor_hud_state().panel_width_ != width || level_editor_hud_state().panel_lines_ != lines.size()) {
        alignas(4) std::array<uint8_t, kPanelParamsSize> memory{};
        gsl::owner<script::Struct*> params{memory::construct_in<script::Struct>(memory.data())};
        const float height = 12.0F + (kLineHeight * static_cast<float>(lines.size()));
        params->insert_pair(checksums::Name{checksums::hash_from_text("pos")}, {.x = x, .y = y});
        params->insert_pair(checksums::Name{checksums::hash_from_text("scale")}, {.x = width / kSpriteUnit, .y = height / kSpriteUnitDown});
        params->put_int(checksums::Name{checksums::hash_from_text("lines")}, static_cast<int>(lines.size()));
        script::launch_script("level_editor_panel_show", params, nullptr, false);
        params->get_clear();
        for (auto it = shown().begin(); it != shown().end();) {
            it = it->first.starts_with("level_editor_panel_") ? shown().erase(it) : std::next(it);
        }
        level_editor_hud_state().panel_ = true;
        level_editor_hud_state().panel_x_ = x;
        level_editor_hud_state().panel_y_ = y;
        level_editor_hud_state().panel_width_ = width;
        level_editor_hud_state().panel_lines_ = lines.size();
    }
    for (size_t i = 0; i < lines.size(); ++i) {
        std::array<char, kPanelLineIdSize> id{};
        text::format_to(id, "level_editor_panel_%u", static_cast<unsigned>(i));
        change_caption({.id = id.data(), .full = core::at(lines, i)});
    }
}

void hide_panel()
{
    if (!level_editor_hud_state().panel_) {
        return;
    }
    script::launch_script("level_editor_panel_hide", nullptr, nullptr, false);
    for (auto it = shown().begin(); it != shown().end();) {
        it = it->first.starts_with("level_editor_panel_") ? shown().erase(it) : std::next(it);
    }
    level_editor_hud_state().panel_ = false;
}

bool panel_shown() { return level_editor_hud_state().panel_; }

std::string get_key(const char* keys) { return std::string("\\c1") + keys + "\\c0"; }

} // namespace frontend::level_editor::hud
