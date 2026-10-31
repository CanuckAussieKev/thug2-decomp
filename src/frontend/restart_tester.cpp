// The restart tester (frontend/restart_tester.h).

#include "frontend/restart_tester.h"

#include <cstdint>
#include <cstdio>
#include <set>
#include <string>
#include <vector>

#include <SDL2/SDL.h>

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

namespace MenuSystem::RestartTester {

namespace {

constexpr SDL_Scancode kKey = SDL_SCANCODE_F8;

uint32_t NameHash(const char* name) { return Checksums::HashFromText(name); }

// A restart type's name (THUG2's and the ported games' restart_types).
const char* TypeName(uint32_t type)
{
    static const char* const kTypes[] = {"Player1", "Player2", "Multiplayer", "Horse", "CTF", "CTF_1", "CTF_2",
                                         "CTF_3", "CTF_4", "Team", "Generic", "PriorityRestart", "Player1TrickAttack",
                                         "Player1_Level_Event", "KingOfTheHill"};
    for (const char* name : kTypes) {
        if (NameHash(name) == type)
            return name;
    }
    const char* known = Scripts::LookUpHashName(type);
    return known ? known : "?";
}

struct Relaunch {
    uint32_t name;
    std::string text;  // the name as the node array has it (its checksum when it has no string)
    std::string types; // restart_types
    Scripts::VectorValue pos;
};

// The restarts of the loaded node arrays (the node manager's walk: the level's, in their order).
std::vector<Relaunch> LevelRestarts()
{
    std::vector<Relaunch> out;
    const uint32_t kClass = NameHash("Class"), kRestart = NameHash("Restart"), kName = NameHash("Name"),
                   kPos = NameHash("Pos"), kTypes = NameHash("restart_types"), kRestartName = NameHash("RestartName");
    LevelNodes::StartNodeWalk(0);
    while (Scripts::CStruct* node = LevelNodes::NextNode()) {
        uint32_t cls = 0, name = 0;
        if (!node->ReadHashValue(kClass, &cls, 0) || cls != kRestart || !node->ReadHashValue(kName, &name, 0))
            continue;
        Relaunch r{name, {}, {}, {0, 0, 0, 1}};
        // (its name when the game knows it - level names mostly aren't -, else its RestartName text and checksum)
        const char* text = Scripts::LookUpHashName(name);
        const char* label = nullptr;
        node->FetchText(kRestartName, &label, 0);
        char hex[16];
        std::snprintf(hex, sizeof(hex), "#%08X", name);
        r.text = text ? text : (label && *label ? std::string(label) + " (" + hex + ")" : std::string(hex));
        node->FetchVec(kPos, &r.pos, 0);
        Scripts::CArray* types = nullptr;
        if (node->FetchList(kTypes, &types, 0) && types) {
            for (uint32_t t = 0; t < types->m_extent; ++t) {
                const char* type = TypeName(types->FetchPart(t));
                if (!r.types.empty())
                    r.types += " ";
                r.types += type ? type : "?";
            }
        }
        out.push_back(r);
    }
    LevelNodes::EndNodeWalk();
    return out;
}

// A panel message (create_panel_message): `id` replaces the one shown before; `y` its height on the 640 x 480 screen.
// (A text element has no line breaks: a second line is a second message.)
void Message(const char* text, const char* id = "restart_tester_message", float y = 70.0f)
{
    Scripts::CStruct* params = Scripts::SpawnParamBag()->PrepareAll();
    params->AppendWord(NameHash("text"), text);
    params->PutNameKey(NameHash("id"), NameHash(id));
    params->InsertPair(NameHash("pos"), 320.0f, y);
    params->PutInt(NameHash("time"), 4000);
    Scripts::SpawnScript(NameHash("create_panel_message"), params, 0, nullptr, 0, 0, false, false, false, false);
    params->Clear();
    Scripts::DeleteStruct(params);
}

uint32_t s_level;             // the level (its restarts' names) the visited set is for
std::set<uint32_t> s_visited; // its restarts visited this round
bool s_keyUp = true;

void Next()
{
    std::vector<Relaunch> restarts = LevelRestarts();
    // (another level: another set of restarts - the visited ones are forgotten)
    uint32_t level = 0;
    for (const Relaunch& r : restarts)
        level = level * 31 + r.name;
    if (level != s_level) {
        s_level = level;
        s_visited.clear();
    }
    if (restarts.empty()) {
        Log("restart tester: this level has no restarts");
        Message("No restarts in this level");
        return;
    }
    if (s_visited.size() >= restarts.size())
        s_visited.clear(); // (a new round)
    for (size_t i = 0; i < restarts.size(); ++i) {
        const Relaunch& r = restarts[i];
        if (s_visited.count(r.name))
            continue;
        s_visited.insert(r.name);
        Scripts::CStruct* params = Scripts::SpawnParamBag()->PrepareAll();
        params->PutNameKey(NameHash("node_name"), r.name);
        Scripts::LaunchScript(NameHash("ResetSkaters"), params, nullptr, false, "ResetSkaters");
        params->Clear();
        Scripts::DeleteStruct(params);
        Log("restart tester: %u / %u %s at (%.0f, %.0f, %.0f), types [%s]%s", static_cast<unsigned>(s_visited.size()),
            static_cast<unsigned>(restarts.size()), r.text.c_str(), r.pos.x, r.pos.y, r.pos.z, r.types.c_str(),
            s_visited.size() == restarts.size() ? " - all restarts visited" : "");
        char text[160];
        std::snprintf(text, sizeof(text), "Restart %u / %u: %s", static_cast<unsigned>(s_visited.size()),
                      static_cast<unsigned>(restarts.size()), r.text.c_str());
        Message(text);
        if (s_visited.size() == restarts.size())
            Message("All restarts visited", "restart_tester_done", 95.0f);
        return;
    }
}

} // namespace

bool& IsSwitchedOn()
{
    static bool on = ConfigInt("features", "restart_tester", 1) != 0;
    return on;
}

void TickFrame(GameModules::FrontEnd* frontEnd)
{
    if (!IsSwitchedOn())
        return;
    const uint8_t* keys = SDL_GetKeyboardState(nullptr);
    bool down = keys && keys[kKey];
    bool pressed = down && s_keyUp;
    s_keyUp = !down;
    if (!pressed || FrameLoop::FramesRun() == 0 || MenuSystem::LevelEditor::LiveNow() || (frontEnd && frontEnd->m_isHalted) || Engine3D::CurrentLevel() == 0)
        return;
    Next();
}

} // namespace MenuSystem::RestartTester
