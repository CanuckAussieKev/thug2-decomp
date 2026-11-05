// The restart tester (devtools/restart_tester.h).

#include "devtools/restart_tester.h"
#include "engine/pad_input/input_source.h"

#include <cstdint>
#include <cstdio>
#include <set>
#include <string>
#include <vector>

#include <SDL2/SDL.h>
#include <array>

#include "base/core/checksums.h"
#include "game/front_end/front_end.h"
#include "engine/ext/extension.h"
#include "engine/gfx/screen.h"
#include "engine/script/cscript.h"
#include "engine/script/names.h"
#include "engine/script/nodes.h"
#include "engine/script/param_struct.h"
#include "engine/sys/mlp.h"
#include "platform/run_log.h"
#include "base/core/checked_index.h"
#include <gsl/pointers>
#include "base/core/format.h"
#include "base/core/no_destroy.h"

namespace frontend::restart_tester {

namespace {

constexpr std::size_t kHexTextSize = 16;
constexpr float kMessageStartY = 70.0F;
constexpr int kMessageMilliseconds = 4000;
constexpr int kNameHashMultiplier = 31;        // a polynomial hash over the restarts' names
constexpr std::size_t kMessageSize = 160;
constexpr float kDoneMessageY = 95.0F;

constexpr SDL_Scancode kKey = SDL_SCANCODE_F8;

uint32_t name_hash(const char* name) { return checksums::hash_from_text(name); }

// A restart type's name (THUG2's and the ported games' restart_types).
const char* current_type_name(uint32_t type)
{
    static const auto kTypesKey = std::to_array<const char*>({"Player1",
                                            "Player2",
                                            "Multiplayer",
                                            "Horse",
                                            "CTF",
                                            "CTF_1",
                                            "CTF_2",
                                            "CTF_3",
                                            "CTF_4",
                                            "Team",
                                            "Generic",
                                            "PriorityRestart",
                                            "Player1TrickAttack",
                                            "Player1_Level_Event",
                                            "KingOfTheHill"});
    for (const char* name : kTypesKey) {
        if (name_hash(name) == type) {
            return name;
        }
    }
    const char* known = script::look_up_hash_name(type);
    return known ? known : "?";
}

struct Relaunch {
    uint32_t name;
    std::string text;  // the name as the node array has it (its checksum when it has no string)
    std::string types; // restart_types
    script::VectorValue pos;
};

// The restarts of the loaded node arrays (the node manager's walk: the level's, in their order).
std::vector<Relaunch> level_restarts()
{
    std::vector<Relaunch> out;
    const uint32_t class_key = name_hash("Class");
    const uint32_t restart = name_hash("Restart");
    const uint32_t name_key = name_hash("Name");
    const uint32_t pos = name_hash("Pos");
    const uint32_t types_key = name_hash("restart_types");
    const uint32_t restart_name = name_hash("RestartName");
    level_nodes::start_node_walk(0);
    while (script::Struct* node = level_nodes::get_next_node()) {
        uint32_t cls = 0;
        uint32_t name = 0;
        if (!node->read_hash_value(class_key, &cls, 0) || cls != restart ||
            !node->read_hash_value(name_key, &name, 0)) {
            continue;
        }
        Relaunch relaunch{.name = name, .text = {}, .types = {}, .pos = {.x = 0, .y = 0, .z = 0, .w = 1}};
        // (its name when the game knows it - level names mostly aren't -, else its RestartName text and checksum)
        const char* text = script::look_up_hash_name(name);
        const char* label = nullptr;
        node->fetch_text(restart_name, &label, 0);
        std::array<char, kHexTextSize> hex{};
        text::format_to(hex, "#%08X", name);
        if (text) {
            relaunch.text = text;
        } else if (label && (*label != '\0')) {
            relaunch.text = std::string(label) + " (" + hex.data() + ")";
        } else {
            relaunch.text = std::string(hex.data());
        }
        node->fetch_vec(pos, &relaunch.pos, 0);
        script::Array* types = nullptr;
        if (node->fetch_list(types_key, &types, 0) && types) {
            for (uint32_t index = 0; index < types->extent(); ++index) {
                const char* type = current_type_name(types->fetch_part(index));
                if (!relaunch.types.empty()) {
                    relaunch.types += " ";
                }
                relaunch.types += type ? type : "?";
            }
        }
        out.push_back(relaunch);
    }
    level_nodes::end_node_walk();
    return out;
}

// A panel message (create_panel_message): `id` replaces the one shown before; `y` its height on the 640 x 480 screen.
// (A text element has no line breaks: a second line is a second message.)
// Where a message goes: its element's id and its height on the screen.
struct MessagePlace {
    const char* id = "restart_tester_message";
    float y = kMessageStartY;
};

void get_message(const char* text, MessagePlace place = {})
{
    const char* id = place.id;
    const float y = place.y;
    gsl::owner<script::Struct*> params = script::new_struct();
    params->append_word(name_hash("text"), text);
    params->put_name_key(checksums::Name{name_hash("id")}, name_hash(id));
    params->insert_pair(checksums::Name{name_hash("pos")}, {.x = render::kLayoutCentreX, .y = y});
    params->put_int(checksums::Name{name_hash("time")}, kMessageMilliseconds);
    script::spawn_script(name_hash("create_panel_message"), params);
    params->get_clear();
    script::delete_struct(params);
}

// NOT ORIGINAL grouping - restart_tester.cpp's state (the original's globals). Made on first use, never destroyed
// (restart_tester_state()).
struct RestartTesterState {
    uint32_t level_{}; // the level (its restarts' names) the visited set is for
    bool key_up_ = true;
};

RestartTesterState& restart_tester_state()
{
    static core::NoDestroy<RestartTesterState> state;
    return state.get();
}

std::set<uint32_t>& visited_state()
{
    static std::set<uint32_t> value; // its restarts visited this round
    return value;
}

void get_next()
{
    std::vector<Relaunch> restarts = level_restarts();
    // (another level: another set of restarts - the visited ones are forgotten)
    uint32_t level = 0;
    for (const Relaunch& restart : restarts) {
        level = (level * kNameHashMultiplier) + restart.name;
    }
    if (level != restart_tester_state().level_) {
        restart_tester_state().level_ = level;
        visited_state().clear();
    }
    if (restarts.empty()) {
        get_log("restart tester: this level has no restarts");
        get_message("No restarts in this level");
        return;
    }
    if (visited_state().size() >= restarts.size()) {
        visited_state().clear(); // (a new round)
    }
    for (size_t i = 0; i < restarts.size(); ++i) {
        const Relaunch& restart = core::at(restarts, i);
        if (visited_state().contains(restart.name)) {
            continue;
        }
        visited_state().insert(restart.name);
        gsl::owner<script::Struct*> params = script::new_struct();
        params->put_name_key(checksums::Name{name_hash("node_name")}, restart.name);
        script::launch_script(name_hash("ResetSkaters"), params, nullptr, false, "ResetSkaters");
        params->get_clear();
        script::delete_struct(params);
        get_log("restart tester: %u / %u %s at (%.0f, %.0f, %.0f), types [%s]%s",
                static_cast<unsigned>(visited_state().size()), static_cast<unsigned>(restarts.size()),
                restart.text.c_str(), restart.pos.x, restart.pos.y, restart.pos.z, restart.types.c_str(),
                visited_state().size() == restarts.size() ? " - all restarts visited" : "");
        std::array<char, kMessageSize> text{};
        text::format_to(text, "Restart %u / %u: %s", static_cast<unsigned>(visited_state().size()),
                        static_cast<unsigned>(restarts.size()), restart.text.c_str());
        get_message(text.data());
        if (visited_state().size() == restarts.size()) {
            get_message("All restarts visited", {.id = "restart_tester_done", .y = kDoneMessageY});
        }
        return;
    }
}

} // namespace

bool& is_switched_on()
{
    static bool on = config_int("features", "restart_tester", 1) != 0;
    return on;
}

void tick_frame(modules::FrontEnd* front_end)
{
    if (!is_switched_on()) {
        return;
    }
    const input::KeyState keys = input::keys();
    const bool down = !keys.empty() && (core::at(keys, kKey) != 0U);
    const bool pressed = down && restart_tester_state().key_up_;
    restart_tester_state().key_up_ = !down;
    if (!pressed || frame_loop::frames_run() == 0 || (ext::game_owner() != nullptr) ||
        (front_end && front_end->is_halted()) || render::current_level() == 0) {
        return;
    }
    get_next();
}

} // namespace frontend::restart_tester
