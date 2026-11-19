// NOT ORIGINAL - (round CS) the level loaded again where the player stands (level_reload.h).

#include "game/front_end/level_reload.h"

#include <cmath>
#include <cstdint>
#include <numbers>
#include <string>

#include "base/core/checked_index.h"
#include "base/core/no_destroy.h"
#include "base/maths/vec4.h"
#include "engine/ext/extension.h"
#include "engine/gfx/loading_bar_cfuncs.h"
#include "engine/gfx/screen.h"
#include "engine/net/session_query.h"
#include "engine/objects/entity.h"
#include "game/front_end/front_end.h"
#include "game/front_end/start_level.h"
#include "game/rider/rider.h"
#include "game/rider/test_view.h"
#include "game/skate/skate_module.h"
#include "platform/run_log.h"

namespace frontend::level_reload {

namespace {

constexpr uint32_t kLoadMainMenu = 0xE92ECAFE; // load_mainmenu (the skate module's level in the front end)
constexpr int kSettleFrames = 60;              // (the level up, unpaused, before the skater is put back: as -at)
constexpr int kGiveUpFrames = 60 * 60;         // (a minute without the level coming up: the skater left where it is)

enum class Step : uint8_t { kIdle, kWaitResume, kWaitLevel };

struct ReloadState {
    Step step = Step::kIdle;
    uint32_t load = 0;
    test_view::Vec3 at{};
    float heading = 0.0F;
    bool have_spot = false;
    int frames = 0;
    int settled = 0; // (frames up and unpaused in a row)
};

ReloadState& reload_state()
{
    static core::NoDestroy<ReloadState> state;
    return state.get();
}

uint32_t level_now() { return modules::Skate::sole() != nullptr ? modules::Skate::sole()->level() : 0U; }

bool loading() { return render::loading_screen_shown() != 0; }

// Why the level can't be loaded again now ("" when it can).
std::string refusal()
{
    const uint32_t load = level_now();
    if (load == 0U || load == kLoadMainMenu || render::current_level() == 0) {
        return "not in a level";
    }
    if (net::in_net_game()) {
        return "not in an online game";
    }
    if (ext::game_owner() != nullptr) {
        return "not with the level editor open";
    }
    return {};
}

// Where the skater stands and faces now (the screenshot sidecar's heading: 0 = +z, 90 = +x).
void note_spot(ReloadState* state)
{
    state->have_spot = false;
    const objects::GameEntity* skater = modules::Skate::sole() != nullptr ? modules::Skate::sole()->get_home_rider() : nullptr;
    if (skater == nullptr) {
        return;
    }
    state->at = {.x = skater->at_point().x, .y = skater->at_point().y, .z = skater->at_point().z};
    const maths::Vec4& facing = core::at(skater->basis_frame().row, 2);
    state->heading = static_cast<float>(std::atan2(static_cast<double>(facing.x), static_cast<double>(facing.z)) * 180.0 /
                                        std::numbers::pi);
    state->have_spot = true;
}

void begin_load(ReloadState* state)
{
    const std::string why = refusal();
    if (!why.empty()) {
        get_log("level reload: dropped - %s", why.c_str());
        state->step = Step::kIdle;
        return;
    }
    state->load = level_now();
    note_spot(state);
    std::string failed;
    if (!begin_level::load_again(state->load, &failed)) {
        get_log("level reload: %s", failed.c_str());
        state->step = Step::kIdle;
        return;
    }
    state->step = Step::kWaitLevel;
    state->frames = 0;
}

void tick(modules::FrontEnd* front_end)
{
    ReloadState& state = reload_state();
    const bool paused = front_end != nullptr && front_end->is_paused();
    if (state.step == Step::kWaitResume) {
        if (!paused && !loading()) {
            begin_load(&state);
        }
        return;
    }
    if (state.step != Step::kWaitLevel) {
        return;
    }
    const bool up = render::current_level() != 0 && level_now() == state.load && !loading() &&
                    modules::Skate::sole() != nullptr && modules::Skate::sole()->get_home_rider() != nullptr;
    ++state.frames;
    if (state.frames > kGiveUpFrames) {
        get_log("level reload: the level didn't come up - the skater left where it is");
        state.step = Step::kIdle;
        return;
    }
    // (counted again from 0 while the level isn't up or is paused: a second of it up before the skater is placed)
    state.settled = up && !paused ? state.settled + 1 : 0;
    if (state.settled < kSettleFrames) {
        return;
    }
    state.settled = 0;
    state.step = Step::kIdle;
    if (!state.have_spot) {
        get_log("level reload: done");
        return;
    }
    test_view::Vec3 placed{};
    const std::string failed = test_view::jump_to(state.at, true, state.heading, &placed);
    if (!failed.empty()) {
        get_log("level reload: done - %s, the skater stays where the level put it", failed.c_str());
    } else {
        get_log("level reload: done - the skater stands at (%.1f, %.1f, %.1f) facing %.1f degrees", placed.x, placed.y,
                placed.z, static_cast<double>(state.heading));
    }
}

class LevelReloadExtension final : public ext::Extension {
public:
    void menu_frame(modules::FrontEnd* front_end, bool game_owned) override
    {
        if (!game_owned) {
            tick(front_end);
        }
    }
};

} // namespace

bool request(std::string* why)
{
    ReloadState& state = reload_state();
    if (state.step != Step::kIdle) {
        return true; // (one load for several changes)
    }
    const std::string refused = refusal();
    if (!refused.empty()) {
        if (why != nullptr) {
            *why = refused;
        }
        return false;
    }
    state.step = Step::kWaitResume; // (at the next frame the game isn't paused)
    get_log("level reload: asked for - the level loads again once the game runs");
    return true;
}

bool pending() { return reload_state().step != Step::kIdle; }

ext::Extension& extension()
{
    static LevelReloadExtension extension;
    return extension;
}

} // namespace frontend::level_reload
