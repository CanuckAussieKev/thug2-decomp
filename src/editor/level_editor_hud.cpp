// NOT ORIGINAL - the level editor's HUD (ours). See level_editor_hud.h.

#include "editor/level_editor_hud.h"

#include <SDL2/SDL.h>

#include <cstdint>
#include <cstdio>
#include <iterator>
#include <map>
#include <array>
#include <string>
#include <vector>

#include "engine/core/checksums.h"
#include "engine/script/cscript.h"
#include "engine/script/param_struct.h"
#include "engine/core/checked_index.h"
#include <gsl/pointers>
#include "engine/core/mem.h"
#include "engine/core/format.h"
#include "engine/core/no_destroy.h"

namespace frontend::level_editor::hud {
namespace {

constexpr size_t kMaxText = 90;
constexpr uint32_t kId = 0x40C698AFu;   // id
constexpr uint32_t kText = 0xC4745838u; // text
// (the white2 sprite's size at scale 1 in the 640 x 480 screen: across, the bottom bar's 200.5 spans 640; down,
// about 8 - measured on screen)
constexpr float kSpriteUnit = 640.0f / 200.5f, kSpriteUnitDown = 8.0f;
constexpr float kLineHeight = 12.5f;

std::map<std::string, std::string>& shown()
{
    static std::map<std::string, std::string> value; // element id -> text
    return value;
}
// NOT ORIGINAL grouping - level_editor_hud.cpp's state (were globals). Made on first use, never destroyed
// (level_editor_hud_state()).
struct LevelEditorHudState {
    std::string message_{};
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

void change_caption(const std::string& id, const std::string& full)
{
    if (!level_editor_hud_state().elements_)
        return;
    const std::string text = full.size() > kMaxText ? full.substr(0, kMaxText) : full;
    auto it = shown().find(id);
    if (it != shown().end() && it->second == text)
        return;
    shown()[id] = text;
    alignas(4) std::array<uint8_t, 8> memory{};
    auto params = memory::construct_in<script::Struct>(memory.data());
    params->put_name_key(kId, checksums::hash_from_text(id.c_str()));
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
    std::array<char, 32> id{};
    text::format_to(id, "level_editor_hud_%d", line);
    change_caption(id.data(), text);
}

void get_message(const std::string& text, double seconds)
{
    level_editor_hud_state().message_ = text;
    level_editor_hud_state().message_until_ = SDL_GetTicks() + static_cast<uint32_t>(seconds * 1000.0);
    change_strand(7, text);
}

void tick()
{
    if (!level_editor_hud_state().message_.empty() && static_cast<int32_t>(SDL_GetTicks() - level_editor_hud_state().message_until_) >= 0) {
        level_editor_hud_state().message_.clear();
        change_strand(7, "");
    }
}

void show_panel(const std::vector<std::string>& lines, float x, float y, float width)
{
    if (!level_editor_hud_state().elements_)
        return;
    if (!level_editor_hud_state().panel_ || level_editor_hud_state().panel_x_ != x || level_editor_hud_state().panel_y_ != y || level_editor_hud_state().panel_width_ != width || level_editor_hud_state().panel_lines_ != lines.size()) {
        alignas(4) std::array<uint8_t, 8> memory{};
        auto params = memory::construct_in<script::Struct>(memory.data());
        const float height = 12.0f + kLineHeight * static_cast<float>(lines.size());
        params->insert_pair(checksums::hash_from_text("pos"), x, y);
        params->insert_pair(checksums::hash_from_text("scale"), width / kSpriteUnit, height / kSpriteUnitDown);
        params->put_int(checksums::hash_from_text("lines"), static_cast<int>(lines.size()));
        script::launch_script("level_editor_panel_show", params, nullptr, false);
        params->get_clear();
        for (auto it = shown().begin(); it != shown().end();)
            it = it->first.starts_with("level_editor_panel_") ? shown().erase(it) : std::next(it);
        level_editor_hud_state().panel_ = true;
        level_editor_hud_state().panel_x_ = x;
        level_editor_hud_state().panel_y_ = y;
        level_editor_hud_state().panel_width_ = width;
        level_editor_hud_state().panel_lines_ = lines.size();
    }
    for (size_t i = 0; i < lines.size(); ++i) {
        std::array<char, 32> id{};
        text::format_to(id, "level_editor_panel_%u", static_cast<unsigned>(i));
        change_caption(id.data(), core::at(lines, i));
    }
}

void hide_panel()
{
    if (!level_editor_hud_state().panel_)
        return;
    script::launch_script("level_editor_panel_hide", nullptr, nullptr, false);
    for (auto it = shown().begin(); it != shown().end();)
        it = it->first.starts_with("level_editor_panel_") ? shown().erase(it) : std::next(it);
    level_editor_hud_state().panel_ = false;
}

bool panel_shown() { return level_editor_hud_state().panel_; }

std::string get_key(const char* keys) { return std::string("\\c1") + keys + "\\c0"; }

} // namespace frontend::level_editor::hud
