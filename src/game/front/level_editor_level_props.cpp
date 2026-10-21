// NOT ORIGINAL - the level editor's level properties (ours). See level_editor_level_props.h.

#include "game/front/level_editor_level_props.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>

#include "game/core/crc.h"
#include "game/gfx/tod_manager.h"
#include "game/script/cscript.h"
#include "game/script/node_manager.h"
#include "game/script/nodes.h"
#include "game/script/struct.h"
#include "hook/game.h"
#include "hook/log.h"

namespace Front::LevelEditor::LevelProps {

namespace {

const char* const kStateNames[kStates] = {"morning", "afternoon", "evening", "night"};

bool ReadInts(const std::string& text, int* out, int count)
{
    std::string error;
    Fields::Value v;
    if (!Fields::Parse(Fields::Kind::Integers, text, &v, &error) || static_cast<int>(v.list.size()) != count)
        return false;
    for (int i = 0; i < count; ++i)
        out[i] = static_cast<int32_t>(v.list[i]);
    return true;
}

Fields::Value Int(int value)
{
    Fields::Value v;
    v.kind = Fields::Kind::Integer;
    v.integer = value;
    return v;
}

} // namespace

const char* TodName(int tod) { return tod >= 0 && tod < kStates ? kStateNames[tod] : nullptr; }

bool TodFromName(const std::string& name, int* tod)
{
    for (int i = 0; i < kStates; ++i) {
        if (_stricmp(name.c_str(), kStateNames[i]) == 0) {
            *tod = i;
            return true;
        }
    }
    return false;
}

bool Same(const Props& a, const Props& b)
{
    return a.name == b.name && a.start == b.start && a.tod == b.tod && a.time == b.time && a.hasAmbient == b.hasAmbient &&
           (!a.hasAmbient || std::memcmp(a.ambient, b.ambient, sizeof(a.ambient)) == 0) && a.hasFog == b.hasFog &&
           (!a.hasFog || std::memcmp(a.fog, b.fog, sizeof(a.fog)) == 0) && _stricmp(a.sky.c_str(), b.sky.c_str()) == 0;
}

bool Empty(const Props& props) { return Same(props, Props{}); }

bool ValidSky(const std::string& sky)
{
    if (sky.empty() || sky.size() > 64)
        return false;
    std::string lower;
    for (char c : sky) {
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_')
            return false;
        lower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return lower.find("_sky") != std::string::npos;
}

int Channel(int value) { return value < 0 ? 0 : value > 255 ? 255 : value; }
int Distance(int value) { return value < 0 ? 0 : value > 100000 ? 100000 : value; }

std::string WriteRecord(const Props& props)
{
    if (Empty(props))
        return "";
    std::string line = "level";
    char text[96];
    if (!props.name.empty())
        line += " name=" + Fields::Escape(props.name);
    if (props.start) {
        std::snprintf(text, sizeof(text), " start=%08X", props.start);
        line += text;
    }
    if (TodName(props.tod))
        line += std::string(" tod=") + TodName(props.tod);
    if (props.time == 0 || props.time == 1)
        line += props.time ? " time=1" : " time=0";
    if (props.hasAmbient) {
        std::snprintf(text, sizeof(text), " ambient=%d,%d,%d", props.ambient[0], props.ambient[1], props.ambient[2]);
        line += text;
    }
    if (props.hasFog) {
        std::snprintf(text, sizeof(text), " fog=%d,%d,%d,%d,%d", props.fog[0], props.fog[1], props.fog[2], props.fog[3],
                      props.fog[4]);
        line += text;
    }
    if (!props.sky.empty())
        line += " sky=" + Fields::Escape(props.sky);
    return line + "\n";
}

bool ReadRecord(std::istringstream& in, Props* props)
{
    Props p;
    std::string field;
    while (in >> field) {
        size_t equals = field.find('=');
        if (equals == std::string::npos || equals == 0)
            return false;
        std::string key = field.substr(0, equals), value = field.substr(equals + 1);
        if (key == "name") {
            if (!Fields::Unescape(value, &p.name) || p.name.empty() || p.name.size() > 64)
                return false;
        } else if (key == "start") {
            if (!Fields::ParseName("0x" + value, &p.start) || value.size() != 8 || !p.start)
                return false;
        } else if (key == "tod") {
            if (!TodFromName(value, &p.tod))
                return false;
        } else if (key == "time") {
            if (value != "0" && value != "1")
                return false;
            p.time = value == "1" ? 1 : 0;
        } else if (key == "ambient") {
            if (!ReadInts(value, p.ambient, 3))
                return false;
            p.hasAmbient = true;
        } else if (key == "fog") {
            if (!ReadInts(value, p.fog, 5))
                return false;
            p.hasFog = true;
        } else if (key == "sky") {
            if (!Fields::Unescape(value, &p.sky) || !ValidSky(p.sky))
                return false;
        } // (another key: a later version's - skipped)
    }
    *props = p;
    return true;
}

std::vector<std::pair<const char*, Fields::Value>> StructureValues(const Props& props)
{
    std::vector<std::pair<const char*, Fields::Value>> out;
    if (TodName(props.tod)) {
        Fields::Value v;
        v.kind = Fields::Kind::Name;
        v.checksum = Fields::NameChecksum(TodName(props.tod));
        out.push_back({"tod_state", v});
    }
    if (props.time == 0 || props.time == 1)
        out.push_back({"tod_manager_active", Int(props.time)});
    if (props.hasAmbient) {
        out.push_back({"ambient_red", Int(Channel(props.ambient[0]))});
        out.push_back({"ambient_green", Int(Channel(props.ambient[1]))});
        out.push_back({"ambient_blue", Int(Channel(props.ambient[2]))});
    }
    if (props.hasFog) {
        out.push_back({"fog_red", Int(Channel(props.fog[0]))});
        out.push_back({"fog_green", Int(Channel(props.fog[1]))});
        out.push_back({"fog_blue", Int(Channel(props.fog[2]))});
        out.push_back({"fog_alpha", Int(Channel(props.fog[3]))});
        out.push_back({"fog_dist", Int(Distance(props.fog[4]))});
    }
    if (ValidSky(props.sky)) {
        Fields::Value v;
        v.kind = Fields::Kind::String;
        v.text = props.sky;
        out.push_back({"sky", v});
    }
    return out;
}

// --- the game side ----------------------------------------------------------------------------------------------

namespace {

Props g_props;
std::vector<std::string> g_skies;
bool g_skiesMade;

// The level structure's values put in by WriteIntoStructure, and what was there before.
struct Kept {
    uint32_t key;
    Fields::Own before;
};
Script::CStruct* g_written;
std::vector<Kept> g_kept;

// The level structure of the level loaded now (Levels.qb's Level_<x>: its load_script is the skate module's level).
Script::CStruct* CurrentLevelStructure()
{
    uint8_t* skate = GameVariable<uint8_t*>(0x007CE478);
    uint32_t load = skate ? *reinterpret_cast<uint32_t*>(skate + 0x378) : 0;
    if (!load)
        return nullptr;
    static const uint32_t kLoadScript = Crc::GenerateCRCFromString("load_script");
    for (Script::CSymbolTableEntry* e = Script::GetNextSymbol(nullptr); e; e = Script::GetNextSymbol(e)) {
        uint32_t value = 0;
        if (e->type == Script::ESYMBOLTYPE_STRUCTURE && e->structure &&
            e->structure->GetChecksum(kLoadScript, &value, 0) && value == load)
            return e->structure;
    }
    return nullptr;
}

void ShowTimeOfDay(int tod)
{
    Gfx::CTODManager* manager = Gfx::TODManager();
    if (manager && TodName(tod))
        manager->SetTODInstant(Crc::GenerateCRCFromString(TodName(tod)));
}

} // namespace

const Props& Current() { return g_props; }

void Set(const Props& props)
{
    int tod = props.tod;
    if (tod != g_props.tod && tod < 0) {
        // (back to the level's own state)
        Props defaults;
        if (LevelDefaults(&defaults))
            tod = defaults.tod;
    }
    bool todChanged = props.tod != g_props.tod;
    g_props = props;
    if (todChanged)
        ShowTimeOfDay(tod);
}

bool LevelDefaults(Props* props)
{
    Script::CStruct* level = CurrentLevelStructure();
    if (!level)
        return false;
    Props p;
    const char* name = nullptr;
    if (level->GetString(Crc::GenerateCRCFromString("name"), &name, 0) && name)
        p.name = name;
    const char* sky = nullptr;
    if (level->GetString(Crc::GenerateCRCFromString("sky"), &sky, 0) && sky)
        p.sky = sky;
    uint32_t state = 0;
    p.tod = 1; // (load_level: afternoon when the level names none)
    if (level->GetChecksum(Crc::GenerateCRCFromString("tod_state"), &state, 0)) {
        for (int i = 0; i < kStates; ++i)
            p.tod = state == Crc::GenerateCRCFromString(kStateNames[i]) ? i : p.tod;
    }
    int value = 1;
    p.time = level->GetInteger(Crc::GenerateCRCFromString("tod_manager_active"), &value, 0) && value == 0 ? 0 : 1;
    static const char* const kAmbient[3] = {"ambient_red", "ambient_green", "ambient_blue"};
    static const char* const kFog[5] = {"fog_red", "fog_green", "fog_blue", "fog_alpha", "fog_dist"};
    for (int i = 0; i < 3; ++i) {
        value = 0;
        p.hasAmbient = level->GetInteger(Crc::GenerateCRCFromString(kAmbient[i]), &value, 0) || p.hasAmbient;
        p.ambient[i] = value;
    }
    for (int i = 0; i < 5; ++i) {
        value = 0;
        p.hasFog = level->GetInteger(Crc::GenerateCRCFromString(kFog[i]), &value, 0) || p.hasFog;
        p.fog[i] = value;
    }
    *props = p;
    return true;
}

void ForgetLevel()
{
    g_props = Props{};
    g_skiesMade = false;
}

const std::vector<std::string>& Skies()
{
    if (g_skiesMade)
        return g_skies;
    g_skiesMade = true;
    g_skies.clear();
    static const uint32_t kSky = Crc::GenerateCRCFromString("sky"), kLoadScript = Crc::GenerateCRCFromString("load_script");
    for (Script::CSymbolTableEntry* e = Script::GetNextSymbol(nullptr); e; e = Script::GetNextSymbol(e)) {
        const char* sky = nullptr;
        uint32_t load = 0;
        // (a level structure: it names its load script - not some other structure with a sky=)
        if (e->type != Script::ESYMBOLTYPE_STRUCTURE || !e->structure || !e->structure->GetChecksum(kLoadScript, &load, 0) ||
            !e->structure->GetString(kSky, &sky, 0) || !sky || !ValidSky(sky))
            continue;
        bool there = false;
        for (const std::string& s : g_skies)
            there = there || _stricmp(s.c_str(), sky) == 0;
        if (!there)
            g_skies.push_back(sky);
    }
    std::sort(g_skies.begin(), g_skies.end(), [](const std::string& a, const std::string& b) {
        return _stricmp(a.c_str(), b.c_str()) < 0;
    });
    return g_skies;
}

void WriteIntoStructure(Script::CStruct* level, const Props& props)
{
    RestoreStructure();
    if (!level)
        return;
    std::vector<std::pair<const char*, Fields::Value>> values = StructureValues(props);
    if (values.empty())
        return;
    g_written = level;
    for (const auto& entry : values) {
        uint32_t key = Crc::GenerateCRCFromString(entry.first);
        g_kept.push_back({key, Fields::OwnIn(level, key, false)});
        Fields::Own own;
        own.present = true;
        own.value = entry.second;
        Fields::PutIn(level, key, false, own);
    }
    Log("level editor: the edited copy's level properties written into its level structure (%u values)",
        static_cast<unsigned>(values.size()));
}

void RestoreStructure()
{
    if (g_written) {
        for (auto it = g_kept.rbegin(); it != g_kept.rend(); ++it)
            Fields::PutIn(g_written, it->key, false, it->before);
    }
    g_written = nullptr;
    g_kept.clear();
}

void OnEditedCopyUp(const Props& props)
{
    static const uint32_t kClass = Crc::GenerateCRCFromString("Class"), kRestart = Crc::GenerateCRCFromString("Restart");
    Script::CStruct* node = props.start && SkateScript::NodeManagerInstance() ? SkateScript::GetNode(props.start, false)
                                                                              : nullptr;
    uint32_t cls = 0;
    if (!node || !node->GetChecksum(kClass, &cls, 0) || cls != kRestart) {
        if (props.start)
            Log("level editor: the edited copy's start %08X isn't a restart of this level - not used", props.start);
        return;
    }
    Script::CStruct* params = Script::NewStruct()->Init();
    params->AddChecksum(Crc::GenerateCRCFromString("node_name"), props.start);
    Script::RunScript(Crc::GenerateCRCFromString("ResetSkaters"), params, nullptr, false, "ResetSkaters");
    params->Clear();
    Script::DeleteStruct(params);
    Log("level editor: the edited copy's start - the skater put at %08X", props.start);
}

} // namespace Front::LevelEditor::LevelProps
