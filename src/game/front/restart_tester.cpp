// The restart tester (game/front/restart_tester.h).

#include "game/front/restart_tester.h"

#include <cstdint>
#include <cstdio>
#include <set>
#include <string>
#include <vector>

#include <SDL2/SDL.h>

#include "game/core/crc.h"
#include "game/front/front_end.h"
#include "game/front/level_editor.h"
#include "game/gfx/screen.h"
#include "game/script/cscript.h"
#include "game/script/names.h"
#include "game/script/nodes.h"
#include "game/script/struct.h"
#include "hook/log.h"

namespace Front::RestartTester {

namespace {

constexpr SDL_Scancode kKey = SDL_SCANCODE_F8;

uint32_t Checksum(const char* name) { return Crc::GenerateCRCFromString(name); }

// A restart type's name (THUG2's and the ported games' restart_types).
const char* TypeName(uint32_t type)
{
    static const char* const kTypes[] = {"Player1", "Player2", "Multiplayer", "Horse", "CTF", "CTF_1", "CTF_2",
                                         "CTF_3", "CTF_4", "Team", "Generic", "PriorityRestart", "Player1TrickAttack",
                                         "Player1_Level_Event", "KingOfTheHill"};
    for (const char* name : kTypes) {
        if (Checksum(name) == type)
            return name;
    }
    const char* known = Script::FindChecksumName(type);
    return known ? known : "?";
}

struct Restart {
    uint32_t name;
    std::string text;  // the name as the node array has it (its checksum when it has no string)
    std::string types; // restart_types
    Script::VectorValue pos;
};

// The restarts of the loaded node arrays (the node manager's walk: the level's, in their order).
std::vector<Restart> LevelRestarts()
{
    std::vector<Restart> out;
    const uint32_t kClass = Checksum("Class"), kRestart = Checksum("Restart"), kName = Checksum("Name"),
                   kPos = Checksum("Pos"), kTypes = Checksum("restart_types"), kRestartName = Checksum("RestartName");
    SkateScript::StartNodeWalk(0);
    while (Script::CStruct* node = SkateScript::NextNode()) {
        uint32_t cls = 0, name = 0;
        if (!node->GetChecksum(kClass, &cls, 0) || cls != kRestart || !node->GetChecksum(kName, &name, 0))
            continue;
        Restart r{name, {}, {}, {0, 0, 0, 1}};
        // (its name when the game knows it - level names mostly aren't -, else its RestartName text and checksum)
        const char* text = Script::FindChecksumName(name);
        const char* label = nullptr;
        node->GetString(kRestartName, &label, 0);
        char hex[16];
        std::snprintf(hex, sizeof(hex), "#%08X", name);
        r.text = text ? text : (label && *label ? std::string(label) + " (" + hex + ")" : std::string(hex));
        node->GetVector(kPos, &r.pos, 0);
        Script::CArray* types = nullptr;
        if (node->GetArray(kTypes, &types, 0) && types) {
            for (uint32_t t = 0; t < types->m_size; ++t) {
                const char* type = TypeName(types->GetElement(t));
                if (!r.types.empty())
                    r.types += " ";
                r.types += type ? type : "?";
            }
        }
        out.push_back(r);
    }
    SkateScript::EndNodeWalk();
    return out;
}

// A panel message (create_panel_message): `id` replaces the one shown before; `y` its height on the 640 x 480 screen.
// (A text element has no line breaks: a second line is a second message.)
void Message(const char* text, const char* id = "restart_tester_message", float y = 70.0f)
{
    Script::CStruct* params = Script::NewStruct()->Init();
    params->AddString(Checksum("text"), text);
    params->AddChecksum(Checksum("id"), Checksum(id));
    params->AddPair(Checksum("pos"), 320.0f, y);
    params->AddInteger(Checksum("time"), 4000);
    Script::SpawnScript(Checksum("create_panel_message"), params, 0, nullptr, 0, 0, false, false, false, false);
    params->Clear();
    Script::DeleteStruct(params);
}

uint32_t s_level;             // the level (its restarts' names) the visited set is for
std::set<uint32_t> s_visited; // its restarts visited this round
bool s_keyUp = true;

void Next()
{
    std::vector<Restart> restarts = LevelRestarts();
    // (another level: another set of restarts - the visited ones are forgotten)
    uint32_t level = 0;
    for (const Restart& r : restarts)
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
        const Restart& r = restarts[i];
        if (s_visited.count(r.name))
            continue;
        s_visited.insert(r.name);
        Script::CStruct* params = Script::NewStruct()->Init();
        params->AddChecksum(Checksum("node_name"), r.name);
        Script::RunScript(Checksum("ResetSkaters"), params, nullptr, false, "ResetSkaters");
        params->Clear();
        Script::DeleteStruct(params);
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

bool& Enabled()
{
    static bool on = ConfigInt("features", "restart_tester", 1) != 0;
    return on;
}

void Update(Mdl::FrontEnd* frontEnd)
{
    if (!Enabled())
        return;
    const uint8_t* keys = SDL_GetKeyboardState(nullptr);
    bool down = keys && keys[kKey];
    bool pressed = down && s_keyUp;
    s_keyUp = !down;
    if (!pressed || Front::LevelEditor::Active() || (frontEnd && frontEnd->m_paused) || Nx::CurrentLevel() == 0)
        return;
    Next();
}

} // namespace Front::RestartTester
