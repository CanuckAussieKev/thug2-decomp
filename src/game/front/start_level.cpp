// Start in a level (game/front/start_level.h).

#include "game/front/start_level.h"

#include <windows.h>

#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <string>

#include "game/core/crc.h"
#include "game/file/file.h"
#include "game/front/front_end.h"
#include "game/front/level_editor.h"
#include "game/front/level_editor_layout.h"
#include "game/front/mouse_menus.h"
#include "game/front/test_view.h"
#include "game/gfx/screen.h"
#include "game/obj/object.h"
#include "game/script/cscript.h"
#include "game/script/struct.h"
#include "game/sys/mlp.h"
#include "hook/log.h"

namespace Front::StartLevel {

namespace {

uint32_t Checksum(const char* name) { return Crc::GenerateCRCFromString(name); }

bool IsScript(uint32_t name)
{
    Script::CSymbolTableEntry* symbol = Script::LookUpSymbol(name);
    return symbol && symbol->type == Script::ESYMBOLTYPE_QSCRIPT;
}

// The command line as the DLL found it, read while the DLL loads: the game's own parsing cuts the process's command
// line buffer into words later (a 0 after the first word, "... -level" - seen with the stock THUG2.exe, 2026-10-20).
const std::string g_commandLine = GetCommandLineA();

std::string FromIni(const char* key)
{
    char value[128] = {};
    GetPrivateProfileStringA("features", key, "", value, sizeof(value), GameFolderPath("thug2-decomp.ini").c_str());
    return value;
}

// The level and where it came from: -edit, -level, edit_level=, start_level= (the first given).
struct Request {
    std::string level;
    bool edit = false;
};
const Request& TheRequest()
{
    static const Request request = [] {
        Request r;
        if (!(r.level = OptionValue(g_commandLine, "edit")).empty())
            r.edit = true;
        else if (!(r.level = OptionValue(g_commandLine, "level")).empty())
            r.edit = false;
        else if (!(r.level = FromIni("edit_level")).empty())
            r.edit = true;
        else
            r.level = FromIni("start_level");
        return r;
    }();
    return request;
}

// Whether Level_<name>'s scene can load: a level without a scene pre (scnpre) needs levels\<level>\<level>.scn.xbx
// (data folder or overlay). The DEVKIT_ONLY level Default has none on PC (only its scripts and sky ship): the game's
// scene load returns no scene for it and writes through it (0x0048C5AA). Levels with a scene pre aren't checked.
bool SceneInstalled(const std::string& name)
{
    Script::CStruct* info = Script::GetStructure(("Level_" + name).c_str());
    const char* scene = nullptr;
    if (!info || info->ContainsComponentNamed("scnpre") || !info->GetString("level", &scene, 0) || !scene)
        return true;
    char path[160];
    std::snprintf(path, sizeof(path), "levels\\%s\\%s.scn.xbx", scene, scene);
    return File::Exist(path);
}

int s_frames;  // the front end's frames with the main menu's scripts loaded
bool s_done;

// -edit: the editor opened once the level is up (EditUpdate).
constexpr uint32_t kSkater = 0x5B8AB877;  // Skater (the local skater's alias)
constexpr int kLevelSettleFrames = 90;     // (in the level, unpaused, before Start is pressed)
constexpr int kPauseWaitFrames = 120;      // (for the pause menu, before Start is pressed again)
constexpr int kPauseMenuFrames = 20;       // (the pause menu up this long before LEVEL EDITOR is chosen)
constexpr int kStartPresses = 5;
enum class Edit { Off, WaitLevel, WaitPause, Done };
Edit s_edit = Edit::Off;
int s_editFrames;
int s_presses;

// -at / start_at: the skater put there standing once the level is up (AtUpdate).
constexpr int kAtSettleFrames = 60;  // (in the level, unpaused, before the teleport)
enum class At { Off, WaitLevel, Done };
At s_at = At::Off;
int s_atFrames;

// The spot asked for ("" if none): -at, then start_at=.
const std::string& AtText()
{
    static const std::string text = [] {
        std::string t = OptionValue(g_commandLine, "at");
        return t.empty() ? FromIni("start_at") : t;
    }();
    return text;
}

// True while the teleport is still to come (-edit's editor waits for it).
bool AtUpdate(Mdl::FrontEnd* frontEnd)
{
    if (s_at != At::WaitLevel)
        return false;
    bool inLevel = Nx::CurrentLevel() != 0 && Obj::FindObject(kSkater) != nullptr;
    bool paused = frontEnd && frontEnd->m_paused;
    s_atFrames = inLevel && !paused ? s_atFrames + 1 : 0;
    if (s_atFrames < kAtSettleFrames)
        return true;
    s_at = At::Done;
    Spot spot;
    ParseSpot(AtText(), &spot);  // (checked when it was asked for)
    TestView::Vec3 at{};
    std::string failed = TestView::Teleport({spot.x, spot.y, spot.z}, spot.hasHeading, spot.heading, &at);
    if (!failed.empty())
        Log("start at: %s - the skater stays where the level put it", failed.c_str());
    else
        Log("start at: the skater stands at (%.1f, %.1f, %.1f) in %s", at.x, at.y, at.z, Requested());
    return false;
}

void EditUpdate(Mdl::FrontEnd* frontEnd)
{
    bool inLevel = Nx::CurrentLevel() != 0 && Obj::FindObject(kSkater) != nullptr;
    bool paused = frontEnd && frontEnd->m_paused;
    if (s_edit == Edit::WaitLevel) {
        s_editFrames = inLevel && !paused ? s_editFrames + 1 : 0;
        if (s_editFrames < kLevelSettleFrames)
            return;
        if (!LevelEditor::Enabled()) {
            Log("edit level: the level editor is off ([features] level_editor=0) - free skate in %s, as -level",
                Requested());
            s_edit = Edit::Done;
            return;
        }
        if (!IsScript(Checksum("level_editor_enter"))) {
            Log("edit level: no level_editor_enter script (the data overlay isn't installed - "
                "tools/menu_level/make_menu_level.ts) - free skate in %s, as -level",
                Requested());
            s_edit = Edit::Done;
            return;
        }
        // (the level is loaded in free skate - Load - so this is only a safeguard: never in a network session or a
        // career-type mode - level_editor_layout.h)
        std::string why;
        if (LevelEditor::Layout::Refused(&why)) {
            Log("edit level: the level editor isn't offered here (%s) - %s as it is", why.c_str(), Requested());
            LevelEditor::Layout::PanelMessage(("The level editor isn't offered here - " + why).c_str());
            s_edit = Edit::Done;
            return;
        }
        // Start, as the player presses it: handle_start_pressed pauses and makes the pause menu.
        Log("edit level: %s is up - pausing (Start) to open the level editor", Requested());
        MouseMenus::SendMenuPadEvent(frontEnd, Mdl::FrontEnd::kPadStart);
        ++s_presses;
        s_edit = Edit::WaitPause;
        s_editFrames = 0;
        return;
    }
    if (s_edit != Edit::WaitPause)
        return;
    if (!inLevel) {
        s_editFrames = 0;
        return;
    }
    if (!paused) {
        // (not taken - a level's intro camera, menu input blocked a moment: pressed again a little later)
        if (++s_editFrames < kPauseWaitFrames)
            return;
        if (s_presses >= kStartPresses) {
            Log("edit level: the game didn't pause after %d presses of Start - free skate in %s (the pause menu's "
                "LEVEL EDITOR opens the editor)",
                s_presses, Requested());
            s_edit = Edit::Done;
            return;
        }
        MouseMenus::SendMenuPadEvent(frontEnd, Mdl::FrontEnd::kPadStart);
        ++s_presses;
        s_editFrames = 0;
        return;
    }
    if (++s_editFrames < kPauseMenuFrames)
        return;
    s_edit = Edit::Done;
    Log("edit level: running level_editor_enter (the pause menu's LEVEL EDITOR)");
    Script::RunScript(Checksum("level_editor_enter"), nullptr, nullptr, false, "level_editor_enter");
}

} // namespace

std::string OptionValue(const std::string& line, const char* option)
{
    std::string name = option;
    for (const std::string& form : {"--" + name + "=", "--" + name + " ", "-" + name + " "}) {
        for (size_t at = line.find(form); at != std::string::npos; at = line.find(form, at + 1)) {
            // (a word of its own: at the start, or after a space or a quote - not inside a path such as C:\my-level x)
            if (at > 0 && line[at - 1] != ' ' && line[at - 1] != '\t' && line[at - 1] != '"')
                continue;
            size_t start = line.find_first_not_of(' ', at + form.size());
            if (start == std::string::npos)
                return {};
            if (line[start] == '"') {
                size_t end = line.find('"', start + 1);
                return line.substr(start + 1, end == std::string::npos ? std::string::npos : end - start - 1);
            }
            size_t end = line.find_first_of(" \"", start);
            return line.substr(start, end == std::string::npos ? std::string::npos : end - start);
        }
    }
    return {};
}

bool ParseSpot(const std::string& text, Spot* out)
{
    std::string s = text;
    // (the sidecar's "(x, y, z)": one pair of parentheses around the whole)
    size_t first = s.find_first_not_of(" \t");
    size_t last = s.find_last_not_of(" \t");
    if (first == std::string::npos)
        return false;
    s = s.substr(first, last - first + 1);
    if (!s.empty() && s.front() == '(' && s.back() == ')')
        s = s.substr(1, s.size() - 2);
    float values[4];
    int count = 0;
    size_t at = 0;
    for (;;) {
        at = s.find_first_not_of(" \t", at);
        if (at == std::string::npos || count == 4)
            return false;
        const char* start = s.c_str() + at;
        char* end = nullptr;
        double v = std::strtod(start, &end);
        if (end == start || !std::isfinite(v))
            return false;
        values[count++] = static_cast<float>(v);
        at = static_cast<size_t>(end - s.c_str());
        at = s.find_first_not_of(" \t", at);
        if (at == std::string::npos)
            break;
        if (s[at] == ',')
            ++at;  // (a comma, or only spaces, between two numbers)
        else if (at > 0 && s[at - 1] != ' ' && s[at - 1] != '\t')
            return false;  // (something glued to the number: "1x")
    }
    if (count < 3)
        return false;
    if (out) {
        *out = {};
        out->x = values[0];
        out->y = values[1];
        out->z = values[2];
        out->hasHeading = count == 4;
        out->heading = count == 4 ? values[3] : 0.0f;
    }
    return true;
}

const char* Requested() { return TheRequest().level.c_str(); }

bool EditRequested() { return TheRequest().edit && !TheRequest().level.empty(); }

void Update(Mdl::FrontEnd* frontEnd)
{
    if (Mlp::FramesRun() == 0)
        return;
    if (!*Requested()) {
        if (!s_done && !AtText().empty()) {
            s_done = true;
            Log("start at: %s asked for without a level (-level / start_level) - ignored", AtText().c_str());
        }
        return;
    }
    if (s_done) {
        if (AtUpdate(frontEnd))
            return;  // (-edit's editor once the skater stands there)
        if (s_edit != Edit::Off && s_edit != Edit::Done)
            EditUpdate(frontEnd);
        return;
    }
    // In the front end, with the main menu up: its scripts loaded (not the early self-tests' stand-in symbol table,
    // which run this before the game is up), then a second more for the menu to settle.
    if (Nx::CurrentLevel() != 0 || (frontEnd && frontEnd->m_paused) || !IsScript(Checksum("main_menu_skate_now")))
        return;
    if (++s_frames < 60)
        return;
    s_done = true;
    bool loaded = Load(Requested(), nullptr);
    if (loaded && !AtText().empty()) {
        if (ParseSpot(AtText(), nullptr)) {
            s_at = At::WaitLevel;
            s_atFrames = 0;
            Log("start at: the skater is put at %s standing once %s is up", AtText().c_str(), Requested());
        } else {
            Log("start at: \"%s\" isn't x,y,z or x,y,z,heading - ignored", AtText().c_str());
        }
    }
    if (loaded && EditRequested()) {
        s_edit = Edit::WaitLevel;
        s_editFrames = 0;
        s_presses = 0;
        Log("edit level: the level editor opens once %s is up", Requested());
    }
}

bool Load(const char* name, std::string* why)
{
    std::string level = name;
    if (_strnicmp(level.c_str(), "load_", 5) != 0)
        level = "Load_" + level;
    uint32_t load = Checksum(level.c_str());
    if (!IsScript(load)) {
        Log("start level: %s isn't a level's load script - the main menu stays", level.c_str());
        if (why)
            *why = level + " isn't a level's load script";
        return false;
    }
    if (!SceneInstalled(level.substr(5))) {
        Log("start level: %s - the level's scene isn't installed (the game would crash loading it) - the main menu "
            "stays",
            level.c_str());
        if (why)
            *why = level + ": the level's scene isn't installed";
        return false;
    }
    Log("start level: %s (free skate)", level.c_str());
    Script::RunScript(Checksum("EnableSun"), nullptr, nullptr, false, "EnableSun");
    Script::CStruct* params = Script::NewStruct()->Init();
    params->AddChecksum(0u, Checksum("freeskate"));
    Script::RunScript(Checksum("SetGameType"), params, nullptr, false, "SetGameType");
    params->Clear();
    Script::RunScript(Checksum("SetCurrentGameType"), nullptr, nullptr, false, "SetCurrentGameType");
    params->AddChecksum(Checksum("level"), load);
    Script::SpawnScript(Checksum("level_select_change_level"), params, 0, nullptr, 0, 0, false, false, false, false);
    params->Clear();
    Script::DeleteStruct(params);
    return true;
}

} // namespace Front::StartLevel
