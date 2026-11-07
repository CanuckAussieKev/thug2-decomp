// NOT ORIGINAL - the level editor as an extension (level_editor_extension.h).

#include "editor/level_editor_extension.h"

#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <vector>

#include "editor/level_editor.h"
#include "editor/level_editor_hud.h"
#include "editor/level_editor_layout.h"
#include "editor/level_editor_open.h"
#include "editor/level_editor_play.h"
#include "editor/level_editor_playtest.h"
#include "editor/level_editor_triggers.h"
#include "editor/level_editor_ui.h"
#include "editor/level_editor_ui_app.h"
#include "engine/ext/extension.h"

namespace frontend::level_editor {

namespace {

class LevelEditorExtension final : public ext::Extension {
public:
    void register_script_commands() override { register_script_functions(); }
    void prepare_input() override { prepare_all(); }

    // While it runs: its own input logic (a requested exit - Escape - runs level_editor_exit); otherwise play from
    // here's way back to the editor (F5 / Ctrl+P) and play testing (the skater's trail, a restart's play from here).
    void menu_frame(modules::FrontEnd* front_end, bool game_owned) override
    {
        if (game_owned) {
            input_logic();
            return;
        }
        perform::tick_frame(front_end);
        play_test::tick_frame(front_end);
    }

    bool owns_game() override { return live_now(); }
    // ([features] level_editor: the pause menu's LEVEL EDITOR offered)
    bool* feature_switch(const char* key) override
    {
        return std::strcmp(key, "level_editor") == 0 ? &is_switched_on() : nullptr;
    }
    bool pass_pause_key(bool down) override { return level_editor::pass_pause_key(down); }
    // (playing in the Scene panel: the game's keys and controller only while the panel has the focus)
    bool blocks_game_input() override { return !live_now() && ui::app::blocks_game_input(); }

    void before_draw_world() override { tick_frame(); }
    // (any viewport: the editor takes the first camera it is given while it runs)
    void place_camera(gfx::ViewCam* camera, int /*viewport*/) override { level_editor::place_camera(camera); }
    float near_plane(gfx::ViewCam* camera, float near_z) override { return get_near_plane(camera, near_z); }
    float view_angle(gfx::ViewCam* camera, float angle) override { return adjust_view_angle(camera, angle); }
    void view_drawn(const ext::ViewNote& view) override
    {
        note_view(view.camera, view.angle, view.aspect, std::span<const uint32_t, 4>(view.rect));
    }
    void draw_over_frame() override { blit(); }
    void device_lost() override { ui::device_lost(); }

    // (-edit: opened in the level start level loads)
    void open_editor_when_up() override { open_in_level::begin(); }
    void editor_opening_frame(modules::FrontEnd* front_end) override { open_in_level::tick_frame(front_end); }

    void scene_loaded(const char* name, bool is_net) override { layout::on_scene_loaded(name, is_net); }
    bool plays_edited_copy() override { return layout::editing_copy(); }
    bool added_gaps(const goals::GapTally* checklist, std::vector<std::string>* names,
                    std::vector<std::string>* level_names) override
    {
        return triggers::get_editor_gaps(checklist, names, level_names);
    }

    bool describe(ext::Description* out) override
    {
        if (!live_now()) {
            return false;
        }
        out->title = "level editor";
        for (auto& [key, value] : describe_for_screenshot()) {
            out->fields.emplace_back(key, value);
        }
        return true;
    }
    bool show_message(const std::string& text, double seconds) override
    {
        if (!live_now()) {
            return false;
        }
        hud::get_message(text, seconds);
        return true;
    }
};

} // namespace

ext::Extension& extension()
{
    static LevelEditorExtension editor;
    return editor;
}

} // namespace frontend::level_editor
