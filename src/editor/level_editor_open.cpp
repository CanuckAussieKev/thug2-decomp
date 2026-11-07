// NOT ORIGINAL - the level editor opened in the level -edit asked for (level_editor_open.h).

#include "editor/level_editor_open.h"

#include <cstdint>
#include <string>

#include "base/core/checksums.h"
#include "base/core/no_destroy.h"
#include "editor/level_editor.h"
#include "editor/level_editor_layout.h"
#include "engine/gfx/screen.h"
#include "engine/objects/entity.h"
#include "engine/script/cscript.h"
#include "engine/script/param_struct.h"
#include "game/front_end/front_end.h"
#include "game/front_end/menu_pad_event.h"
#include "platform/run_log.h"

namespace frontend::level_editor::open_in_level {

namespace {

uint32_t name_hash(const char* name) { return checksums::hash_from_text(name); }

bool is_script(uint32_t name)
{
    const script::SymbolTableEntry* symbol = script::resolve_symbol(name);
    return (symbol != nullptr) && symbol->type() == script::QbValueKind::kScriptBody;
}

constexpr uint32_t kSkater = 0x5B8AB877; // Skater (the local skater's alias)
constexpr int kLevelSettleFrames = 90;   // (in the level, unpaused, before Start is pressed)
constexpr int kPauseWaitFrames = 120;    // (for the pause menu, before Start is pressed again)
constexpr int kPauseMenuFrames = 20;     // (the pause menu up this long before LEVEL EDITOR is chosen)
constexpr int kStartPresses = 5;
enum class Edit : uint8_t { kDisengaged, kWaitLevel, kWaitPause, kDone };
// NOT ORIGINAL grouping - the editor start: its presses and frames (the original's globals). Made on first use, never
// destroyed (edit_start()).
struct EditStart {
    Edit edit_ = Edit::kDisengaged;
    int edit_frames_{};
    int presses_{};
    std::string level_; // (for the log)
};

EditStart& edit_start()
{
    static core::NoDestroy<EditStart> state;
    return state.get();
}

void edit_update(modules::FrontEnd* front_end)
{
    const bool in_level = render::current_level() != 0 && objects::find_object(kSkater) != nullptr;
    const bool paused = (front_end != nullptr) && front_end->is_halted();
    if (edit_start().edit_ == Edit::kWaitLevel) {
        edit_start().edit_frames_ = in_level && !paused ? edit_start().edit_frames_ + 1 : 0;
        if (edit_start().edit_frames_ < kLevelSettleFrames) {
            return;
        }
        if (!is_switched_on()) {
            get_log("edit level: the level editor runs only in the editor mode - free skate in %s, as -level",
                    edit_start().level_.c_str());
            edit_start().edit_ = Edit::kDone;
            return;
        }
        if (!is_script(name_hash("level_editor_enter"))) {
            get_log("edit level: no level_editor_enter script (the data overlay isn't installed - "
                    "tools/pipeline/menu_level/make_menu_level.ts) - free skate in %s, as -level",
                    edit_start().level_.c_str());
            edit_start().edit_ = Edit::kDone;
            return;
        }
        // (the level is loaded in free skate - Load - so this is only a safeguard: never in a network session or a
        // career-type mode - level_editor_layout.h)
        std::string why;
        if (layout::get_refused(&why)) {
            get_log("edit level: the level editor isn't offered here (%s) - %s as it is", why.c_str(), edit_start().level_.c_str());
            layout::panel_message(("The level editor isn't offered here - " + why).c_str());
            edit_start().edit_ = Edit::kDone;
            return;
        }
        // Start, as the player presses it: handle_start_pressed pauses and makes the pause menu.
        get_log("edit level: %s is up - pausing (Start) to open the level editor", edit_start().level_.c_str());
        modules::send_menu_pad_event(front_end, modules::FrontEnd::kPadStart);
        ++edit_start().presses_;
        edit_start().edit_ = Edit::kWaitPause;
        edit_start().edit_frames_ = 0;
        return;
    }
    if (edit_start().edit_ != Edit::kWaitPause) {
        return;
    }
    if (!in_level) {
        edit_start().edit_frames_ = 0;
        return;
    }
    if (!paused) {
        // (not taken - a level's intro camera, menu input blocked a moment: pressed again a little later)
        if (++edit_start().edit_frames_ < kPauseWaitFrames) {
            return;
        }
        if (edit_start().presses_ >= kStartPresses) {
            get_log("edit level: the game didn't pause after %d presses of Start - free skate in %s (the pause menu's "
                    "LEVEL EDITOR opens the editor)",
                    edit_start().presses_, edit_start().level_.c_str());
            edit_start().edit_ = Edit::kDone;
            return;
        }
        modules::send_menu_pad_event(front_end, modules::FrontEnd::kPadStart);
        ++edit_start().presses_;
        edit_start().edit_frames_ = 0;
        return;
    }
    if (++edit_start().edit_frames_ < kPauseMenuFrames) {
        return;
    }
    edit_start().edit_ = Edit::kDone;
    get_log("edit level: running level_editor_enter (the pause menu's LEVEL EDITOR)");
    script::launch_script(name_hash("level_editor_enter"), nullptr, nullptr, false, "level_editor_enter");
}

} // namespace

void begin(const std::string& level)
{
    edit_start().edit_ = Edit::kWaitLevel;
    edit_start().edit_frames_ = 0;
    edit_start().presses_ = 0;
    edit_start().level_ = level;
    get_log("edit level: the level editor opens once %s is up", edit_start().level_.c_str());
}

bool pending() { return edit_start().edit_ == Edit::kWaitLevel || edit_start().edit_ == Edit::kWaitPause; }

void tick_frame(modules::FrontEnd* front_end)
{
    if (edit_start().edit_ != Edit::kDisengaged && edit_start().edit_ != Edit::kDone) {
        edit_update(front_end);
    }
}

} // namespace frontend::level_editor::open_in_level
