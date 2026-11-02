// The restart tester (frontend/restart_tester.h).

#include "frontend/restart_tester.h"
#include "platform/keyboard.h"

#include <cstdint>
#include <cstdio>
#include <set>
#include <string>
#include <vector>

#include <SDL2/SDL.h>
#include <array>

#include "engine/core/checksums.h"
#include "frontend/front_end.h"
#include "editor/level_editor.h"
#include "engine/gfx/screen.h"
#include "engine/script/cscript.h"
#include "engine/script/names.h"
#include "engine/script/nodes.h"
#include "engine/script/param_struct.h"
#include "engine/sys/mlp.h"
#include "platform/run_log.h"
#include "engine/core/checked_index.h"
#include <gsl/pointers>
#include "engine/core/format.h"

namespace frontend::restart_tester {

namespace {

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
        if (name_hash(name) == type)
            return name;
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
    const uint32_t class_key = name_hash("Class"), restart = name_hash("Restart"), name_key = name_hash("Name"),
                   pos = name_hash("Pos"), types_key = name_hash("restart_types"),
                   restart_name = name_hash("RestartName");
    level_nodes::start_node_walk(0);
    while (script::Struct* node = level_nodes::get_next_node()) {
        uint32_t cls = 0, name = 0;
        if (!node->read_hash_value(class_key, &cls, 0) || cls != restart || !node->read_hash_value(name_key, &name, 0))
            continue;
        Relaunch r{.name = name, .text = {}, .types = {}, .pos = {.x = 0, .y = 0, .z = 0, .w = 1}};
        // (its name when the game knows it - level names mostly aren't -, else its RestartName text and checksum)
        const char* text = script::look_up_hash_name(name);
        const char* label = nullptr;
        node->fetch_text(restart_name, &label, 0);
        std::array<char, 16> hex{};
        text::format_to(hex, "#%08X", name);
        r.text = text ? text : (label && *label ? std::string(label) + " (" + hex.data() + ")" : std::string(hex.data()));
        node->fetch_vec(pos, &r.pos, 0);
        script::Array* types = nullptr;
        if (node->fetch_list(types_key, &types, 0) && types) {
            for (uint32_t t = 0; t < types->extent_; ++t) {
                const char* type = current_type_name(types->fetch_part(t));
                if (!r.types.empty())
                    r.types += " ";
                r.types += type ? type : "?";
            }
        }
        out.push_back(r);
    }
    level_nodes::end_node_walk();
    return out;
}

// A panel message (create_panel_message): `id` replaces the one shown before; `y` its height on the 640 x 480 screen.
// (A text element has no line breaks: a second line is a second message.)
void get_message(const char* text, const char* id = "restart_tester_message", float y = 70.0f)
{
    gsl::owner<script::Struct*> params = script::new_struct();
    params->append_word(name_hash("text"), text);
    params->put_name_key(name_hash("id"), name_hash(id));
    params->insert_pair(name_hash("pos"), 320.0f, y);
    params->put_int(name_hash("time"), 4000);
    script::spawn_script(name_hash("create_panel_message"), params, 0, nullptr, 0, 0, false, false, false, false);
    params->get_clear();
    script::delete_struct(params);
}

uint32_t s_level;             // the level (its restarts' names) the visited set is for
std::set<uint32_t>& visited_state()
{
    static std::set<uint32_t> value; // its restarts visited this round
    return value;
}
bool s_key_up = true;

void get_next()
{
    std::vector<Relaunch> restarts = level_restarts();
    // (another level: another set of restarts - the visited ones are forgotten)
    uint32_t level = 0;
    for (const Relaunch& r : restarts)
        level = level * 31 + r.name;
    if (level != s_level) {
        s_level = level;
        visited_state().clear();
    }
    if (restarts.empty()) {
        get_log("restart tester: this level has no restarts");
        get_message("No restarts in this level");
        return;
    }
    if (visited_state().size() >= restarts.size())
        visited_state().clear(); // (a new round)
    for (size_t i = 0; i < restarts.size(); ++i) {
        const Relaunch& r = core::at(restarts, i);
        if (visited_state().count(r.name))
            continue;
        visited_state().insert(r.name);
        gsl::owner<script::Struct*> params = script::new_struct();
        params->put_name_key(name_hash("node_name"), r.name);
        script::launch_script(name_hash("ResetSkaters"), params, nullptr, false, "ResetSkaters");
        params->get_clear();
        script::delete_struct(params);
        get_log("restart tester: %u / %u %s at (%.0f, %.0f, %.0f), types [%s]%s",
                static_cast<unsigned>(visited_state().size()), static_cast<unsigned>(restarts.size()), r.text.c_str(),
                r.pos.x, r.pos.y, r.pos.z, r.types.c_str(),
                visited_state().size() == restarts.size() ? " - all restarts visited" : "");
        std::array<char, 160> text{};
        text::format_to(text, "Restart %u / %u: %s", static_cast<unsigned>(visited_state().size()),
                        static_cast<unsigned>(restarts.size()), r.text.c_str());
        get_message(text.data());
        if (visited_state().size() == restarts.size())
            get_message("All restarts visited", "restart_tester_done", 95.0f);
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
    if (!is_switched_on())
        return;
    const platform::KeyState keys = platform::keyboard_keys();
    const bool down = !keys.empty() && core::at(keys, kKey);
    const bool pressed = down && s_key_up;
    s_key_up = !down;
    if (!pressed || frame_loop::frames_run() == 0 || frontend::level_editor::live_now() ||
        (front_end && front_end->is_halted_) || render::current_level() == 0)
        return;
    get_next();
}

} // namespace frontend::restart_tester
