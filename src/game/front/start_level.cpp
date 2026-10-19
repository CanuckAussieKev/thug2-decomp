// Start in a level (game/front/start_level.h).

#include "game/front/start_level.h"

#include <windows.h>

#include <cctype>
#include <cstdint>
#include <cstring>
#include <string>

#include "game/core/crc.h"
#include "game/front/front_end.h"
#include "game/gfx/screen.h"
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

// -level <name>, --level <name> or --level=<name> on the command line ("" if none).
std::string FromCommandLine()
{
    std::string line = GetCommandLineA();
    for (const char* option : {"--level=", "--level ", "-level "}) {
        size_t at = line.find(option);
        if (at == std::string::npos)
            continue;
        size_t start = line.find_first_not_of(' ', at + std::strlen(option));
        if (start == std::string::npos)
            return {};
        size_t end = line.find_first_of(" \"", start);
        return line.substr(start, end == std::string::npos ? std::string::npos : end - start);
    }
    return {};
}

std::string FromIni()
{
    char value[128] = {};
    GetPrivateProfileStringA("features", "start_level", "", value, sizeof(value),
                             GameFolderPath("thug2-decomp.ini").c_str());
    return value;
}

int s_frames;  // the front end's frames with the main menu's scripts loaded
bool s_done;

} // namespace

const char* Requested()
{
    static const std::string level = [] {
        std::string name = FromCommandLine();
        return name.empty() ? FromIni() : name;
    }();
    return level.c_str();
}

void Update(Mdl::FrontEnd* frontEnd)
{
    if (s_done || !*Requested() || Mlp::FramesRun() == 0)
        return;
    // In the front end, with the main menu up: its scripts loaded (not the early self-tests' stand-in symbol table,
    // which run this before the game is up), then a second more for the menu to settle.
    if (Nx::CurrentLevel() != 0 || (frontEnd && frontEnd->m_paused) || !IsScript(Checksum("main_menu_skate_now")))
        return;
    if (++s_frames < 60)
        return;
    s_done = true;
    std::string level = Requested();
    if (_strnicmp(level.c_str(), "load_", 5) != 0)
        level = "Load_" + level;
    uint32_t load = Checksum(level.c_str());
    if (!IsScript(load)) {
        Log("start level: %s isn't a level's load script - the main menu stays", level.c_str());
        return;
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
}

} // namespace Front::StartLevel
