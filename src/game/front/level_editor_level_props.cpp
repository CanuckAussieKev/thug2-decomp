// NOT ORIGINAL - the level editor's level properties (ours). See level_editor_level_props.h.

#include "game/front/level_editor_level_props.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "game/core/crc.h"
#include "game/gfx/tod_manager.h"
#include "game/script/cscript.h"
#include "game/script/node_manager.h"
#include "game/script/nodes.h"
#include "game/script/struct.h"
#include "game/sys/log.h"
#include "game/skate/skate_module.h"

namespace MenuSystem::LevelEditor::LevelProps {

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

bool Vacant(const Props& props) { return Same(props, Props{}); }

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
int SpanTo(int value) { return value < 0 ? 0 : value > 100000 ? 100000 : value; }

std::string WriteRecord(const Props& props)
{
    if (Vacant(props))
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
        v.checksum = Fields::HashOfName(TodName(props.tod));
        out.push_back({"tod_state", v});
    }
    if (props.time == 0 || props.time == 1)
        out.push_back({"tod_manager_active", Int(props.time)});
    // (map4: our own keys - the engine reads no level structure's ambient_* / fog_*; OnLevelUp reads these)
    if (props.hasAmbient) {
        Fields::Value v;
        v.kind = Fields::Kind::Integers;
        for (int i = 0; i < 3; ++i)
            v.list.push_back(static_cast<uint32_t>(Channel(props.ambient[i])));
        out.push_back({"ed_ambient", v});
    }
    if (props.hasFog) {
        Fields::Value v;
        v.kind = Fields::Kind::Integers;
        for (int i = 0; i < 4; ++i)
            v.list.push_back(static_cast<uint32_t>(Channel(props.fog[i])));
        v.list.push_back(static_cast<uint32_t>(SpanTo(props.fog[4])));
        out.push_back({"ed_fog", v});
    }
    if (ValidSky(props.sky)) {
        Fields::Value v;
        v.kind = Fields::Kind::Chars;
        v.text = props.sky;
        out.push_back({"sky", v});
    }
    return out;
}

bool Override(const Props& edited, const Props& level, Gfx2D::TODParams* params)
{
    const Props* ambient = edited.hasAmbient ? &edited : level.hasAmbient ? &level : nullptr;
    if (ambient) {
        params->ambientRed = Channel(ambient->ambient[0]);
        params->ambientGreen = Channel(ambient->ambient[1]);
        params->ambientBlue = Channel(ambient->ambient[2]);
    }
    const Props* fog = edited.hasFog ? &edited : level.hasFog ? &level : nullptr;
    if (!fog)
        return false;
    params->fogRed = Channel(fog->fog[0]);
    params->fogGreen = Channel(fog->fog[1]);
    params->fogBlue = Channel(fog->fog[2]);
    params->fogAlpha = Channel(fog->fog[3]);
    params->fogDist = SpanTo(fog->fog[4]);
    return true;
}

void ReadOverrideKeys(Scripts::CStruct* level, Props* out)
{
    static const uint32_t kAmbient = Checksums::HashFromText("ed_ambient"),
                          kFog = Checksums::HashFromText("ed_fog");
    auto read = [&](uint32_t key, int* values, uint32_t count) {
        Scripts::CArray* array = nullptr;
        if (!level || !level->FetchList(key, &array, 0) || !array || array->m_kindTag != Scripts::QBVAL_INTEGER ||
            array->m_extent != count)
            return false;
        for (uint32_t i = 0; i < count; ++i)
            values[i] = static_cast<int32_t>(array->FetchPart(i));
        return true;
    };
    out->hasAmbient = read(kAmbient, out->ambient, 3);
    out->hasFog = read(kFog, out->fog, 5);
}

// --- the game side ----------------------------------------------------------------------------------------------

namespace {

Props g_props;
// (map4) The level structure's ed_ambient / ed_fog (a bake's - or the edited copy's, written for its load).
Props g_level;
bool g_filterSet;

bool Filter(Gfx2D::TODParams* params) { return Override(g_props, g_level, params); }

void InstallFilter()
{
    if (!g_filterSet) {
        g_filterSet = true;
        Gfx2D::SetTODParamsFilter(&Filter);
    }
}

bool LightingSame(const Props& a, const Props& b)
{
    return a.hasAmbient == b.hasAmbient && (!a.hasAmbient || std::memcmp(a.ambient, b.ambient, sizeof(a.ambient)) == 0) &&
           a.hasFog == b.hasFog && (!a.hasFog || std::memcmp(a.fog, b.fog, sizeof(a.fog)) == 0);
}
std::vector<std::string> g_skies;
bool g_skiesMade;

// The level structure's values put in by ExportToStruct, and what was there before.
struct Kept {
    uint32_t key;
    Fields::Own before;
};
Scripts::CStruct* g_written;
std::vector<Kept> g_kept;

// The level structure of the level loaded now (Levels.qb's Level_<x>: its load_script is the skate module's level).
Scripts::CStruct* CurrentLevelStructure()
{
    uint8_t* skate = reinterpret_cast<uint8_t*>(GameModules::g_skateModule);
    uint32_t load = skate ? *reinterpret_cast<uint32_t*>(skate + 0x378) : 0;
    if (!load)
        return nullptr;
    static const uint32_t kLoadScript = Checksums::HashFromText("load_script");
    for (Scripts::CSymbolTableEntry* e = Scripts::GetNextSymbol(nullptr); e; e = Scripts::GetNextSymbol(e)) {
        uint32_t value = 0;
        if (e->type == Scripts::QBVAL_STRUCT && e->structure &&
            e->structure->ReadHashValue(kLoadScript, &value, 0) && value == load)
            return e->structure;
    }
    return nullptr;
}

void ShowTimeOfDay(int tod)
{
    Gfx2D::CTODManager* manager = Gfx2D::TODManager();
    if (manager && TodName(tod))
        manager->SetTODInstant(Checksums::HashFromText(TodName(tod)));
}

} // namespace

const Props& AtHand() { return g_props; }

void AssignValue(const Props& props)
{
    int tod = props.tod;
    if (tod != g_props.tod && tod < 0) {
        // (back to the level's own state)
        Props defaults;
        if (LevelDefaults(&defaults))
            tod = defaults.tod;
    }
    bool todChanged = props.tod != g_props.tod;
    bool fogWas = g_props.hasFog || g_level.hasFog;
    bool lighting = !LightingSame(props, g_props);
    g_props = props;
    InstallFilter();
    if (todChanged)
        ShowTimeOfDay(tod); // (its refresh sends the ambient / fog through the filter too)
    if (lighting && !todChanged)
        Reapply();
    // (a fog the editor had forced on, taken off where the manager's fog is off: off again - the manager's own switch)
    Gfx2D::CTODManager* manager = Gfx2D::TODManager();
    if (fogWas && !g_props.hasFog && !g_level.hasFog && manager && !manager->m_fogActive) {
        manager->m_fogActive = true;
        manager->SetFogActive(false);
    }
}

void OnLevelUp(bool network)
{
    Props level;
    if (!network)
        ReadOverrideKeys(CurrentLevelStructure(), &level);
    bool changed = !LightingSame(level, g_level);
    g_level = level;
    if (!changed)
        return;
    InstallFilter();
    Reapply();
    Log("level editor: the level structure's own lighting (ed_ambient / ed_fog) put in place:%s%s",
        level.hasAmbient ? " ambient" : "", level.hasFog ? " fog" : "");
}

void Reapply()
{
    Gfx2D::CTODManager* manager = Gfx2D::TODManager();
    // (no parameters yet: nothing to send; lights a script set from a named state - ResetLevelLights - kept: the
    // override goes with the next state change)
    if (!manager || !manager->m_paramsSet || manager->m_levelLightsReset)
        return;
    InstallFilter();
    manager->ApplyTODParams(manager->GetTODParams(-1));
}

bool LevelDefaults(Props* props)
{
    Scripts::CStruct* level = CurrentLevelStructure();
    if (!level)
        return false;
    Props p;
    const char* name = nullptr;
    if (level->FetchText(Checksums::HashFromText("name"), &name, 0) && name)
        p.name = name;
    const char* sky = nullptr;
    if (level->FetchText(Checksums::HashFromText("sky"), &sky, 0) && sky)
        p.sky = sky;
    uint32_t state = 0;
    p.tod = 1; // (load_level: afternoon when the level names none)
    if (level->ReadHashValue(Checksums::HashFromText("tod_state"), &state, 0)) {
        for (int i = 0; i < kStates; ++i)
            p.tod = state == Checksums::HashFromText(kStateNames[i]) ? i : p.tod;
    }
    int value = 1;
    p.time = level->FetchIntValue(Checksums::HashFromText("tod_manager_active"), &value, 0) && value == 0 ? 0 : 1;
    // (map4) The ambient light and fog the level shows: the time-of-day manager's current state's - where the engine's
    // come from - with the level structure's ed_ keys (a bake's) over them.
    Gfx2D::CTODManager* manager = Gfx2D::TODManager();
    if (manager && manager->m_paramsSet) {
        Gfx2D::TODParams now = manager->GetTODParams(-1);
        Override(Props{}, g_level, &now);
        p.hasAmbient = true;
        p.ambient[0] = now.ambientRed;
        p.ambient[1] = now.ambientGreen;
        p.ambient[2] = now.ambientBlue;
        p.hasFog = true;
        int fog[5] = {now.fogRed, now.fogGreen, now.fogBlue, now.fogAlpha, now.fogDist};
        std::memcpy(p.fog, fog, sizeof(fog));
    }
    *props = p;
    return true;
}

void ForgetLevel()
{
    g_props = Props{};
    g_level = Props{}; // (before the new level's load sends its lighting: OnLevelUp reads its keys once it's up)
    g_skiesMade = false;
}

const std::vector<std::string>& Skies()
{
    if (g_skiesMade)
        return g_skies;
    g_skiesMade = true;
    g_skies.clear();
    static const uint32_t kSky = Checksums::HashFromText("sky"), kLoadScript = Checksums::HashFromText("load_script");
    for (Scripts::CSymbolTableEntry* e = Scripts::GetNextSymbol(nullptr); e; e = Scripts::GetNextSymbol(e)) {
        const char* sky = nullptr;
        uint32_t load = 0;
        // (a level structure: it names its load script - not some other structure with a sky=)
        if (e->type != Scripts::QBVAL_STRUCT || !e->structure || !e->structure->ReadHashValue(kLoadScript, &load, 0) ||
            !e->structure->FetchText(kSky, &sky, 0) || !sky || !ValidSky(sky))
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

void ExportToStruct(Scripts::CStruct* level, const Props& props)
{
    RestoreStructure();
    if (!level)
        return;
    std::vector<std::pair<const char*, Fields::Value>> values = StructureValues(props);
    if (values.empty())
        return;
    g_written = level;
    for (const auto& entry : values) {
        uint32_t key = Checksums::HashFromText(entry.first);
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
    static const uint32_t kClass = Checksums::HashFromText("Class"), kRestart = Checksums::HashFromText("Restart");
    Scripts::CStruct* node = props.start && LevelNodes::NodeManagerInstance() ? LevelNodes::FetchVertex(props.start, false)
                                                                              : nullptr;
    uint32_t cls = 0;
    if (!node || !node->ReadHashValue(kClass, &cls, 0) || cls != kRestart) {
        if (props.start)
            Log("level editor: the edited copy's start %08X isn't a restart of this level - not used", props.start);
        return;
    }
    Scripts::CStruct* params = Scripts::NewStruct()->PrepareAll();
    params->PutNameKey(Checksums::HashFromText("node_name"), props.start);
    Scripts::LaunchScript(Checksums::HashFromText("ResetSkaters"), params, nullptr, false, "ResetSkaters");
    params->Clear();
    Scripts::DeleteStruct(params);
    Log("level editor: the edited copy's start - the skater put at %08X", props.start);
}

} // namespace MenuSystem::LevelEditor::LevelProps
