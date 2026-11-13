// NOT ORIGINAL - the live pause: which pauses keep the world running (game/front_end/live_pause.h).

#include "game/front_end/live_pause.h"

#include <cstdint>

#include "base/core/checksums.h"
#include "engine/script/context.h"
#include "engine/script/param_struct.h"
#include "game/online/gamenet_manager.h"
#include "game/online/net_game_state.h"
#include "game/park_editing/park_editor.h"
#include "game/skate/game_mode.h"
#include "game/skate/skate_module.h"
#include "platform/run_log.h"

namespace modules::live_pause {

namespace {

constexpr uint32_t kFreeskate = 0x34861A16; // freeskate

} // namespace

bool& is_switched_on()
{
    static bool on = config_int("features", "live_pause", 1) != 0;
    return on;
}

bool& demo_running()
{
    static bool running = false;
    return running;
}

bool applies(script::Context context)
{
    if (!is_switched_on() || demo_running() || !context) {
        return false;
    }
    static const uint32_t kStartPressed = checksums::hash_from_text("handle_start_pressed");
    if (context.script_title() != kStartPressed) {
        return false;
    }
    Skate* skate = Skate::sole();
    if (!skate || !skate->get_home_rider() || Skate::is_multiplayer() != 0 || online::online_hub()->in_net_game()) {
        return false;
    }
    if (goals::game_mode()->get_name_key() != kFreeskate) {
        return false;
    }
    if (const park_editing::ParkBuilderMode* editor = park_editing::ParkBuilderMode::sole();
        editor && editor->editor_state() != park_editing::ParkBuilderMode::kStateOff) {
        return false;
    }
    static const uint32_t kCatPreview = checksums::hash_from_text("in_cat_preview_mode");
    return script::fetch_int_value(kCatPreview) == 0;
}

} // namespace modules::live_pause
