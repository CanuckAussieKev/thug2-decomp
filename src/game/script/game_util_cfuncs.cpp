// Script commands (CFunctions) of the game: interpolating parameters, a texture's name, power-ups (see
// game_util_cfuncs.h).

#include "game/script/game_util_cfuncs.h"

#include <cstdint>
#include <cmath>
#include <cstring>

#include "game/ed/park_editor.h"
#include "game/gfx/debug_lines.h"
#include "game/gfx/texture_cfuncs.h"
#include "game/mth/vector.h"
#include "game/obj/compositemgr.h"
#include "game/core/crc.h"
#include "game/script/cscript.h"
#include "game/script/preferences.h"
#include "game/script/struct.h"
#include "game/skate/records.h"
#include "game/skate/skate_module.h"
#include "game/skater/skater.h"
#include "game/skater/skater_state.h"
#include "game/ed/rail_editor.h"
#include "game/skate/game_mode.h"
#include "game/skate/score.h"
#include "game/gfx/cag_cfuncs.h"

namespace CFuncs {

float g_gameUtilWholeTurn{360.0f};
float g_gameUtilHalfTurn{180.0f};
float g_gameUtilMinusDegreesToRadians{-0.017453292f};

namespace {

using Script::CArray;
using Script::CComponent;
using Script::CStruct;

constexpr uint32_t kA = 0x174841BC;           // "A"
constexpr uint32_t kB = 0x8E411006;           // "B"
constexpr uint32_t kFraction = 0x404E690D;    // (name unknown) InterpolateParameters: how far from A to B
constexpr uint32_t kIgnore = 0xF277291D;      // "ignore"
constexpr uint32_t kAngles = 0x9D2D0915;      // "Angles"
constexpr uint32_t kInterpolated = 0xFF6F3872; // (name unknown) InterpolateParameters: the result
constexpr uint32_t kPath = 0xF4AB74F0;        // "path"
constexpr uint32_t kTexture = 0x7D99F28D;     // "texture"
constexpr uint32_t kSkaterState = 0x43686585; // "SkaterState"
constexpr uint32_t kPos = 0x7F261953;         // "pos"
constexpr uint32_t kRailDistance = 0x213395F1; // (name unknown) FindNearestRailPoint: how far to look (150)
constexpr uint32_t kIgnoreRailId = 0x288A6EFE; // "IgnoreRailId"
constexpr uint32_t kIgnoreRailPointIndex = 0xE4D305A7; // "IgnoreRailPointIndex"
constexpr uint32_t kCheckLevelGeometry = 0x9C247A94; // "CheckLevelGeometry"
constexpr uint32_t kRailId = 0xA61E7CD9;      // "rail_id"
constexpr uint32_t kRailPointIndex = 0x00AB3C14; // "rail_point_index"
constexpr uint32_t kNearestPos = 0xDF970A05;  // "NearestPos"
constexpr uint32_t kParkName = 0xCC23BDDB;    // (name unknown) GetParkName: the park's name
constexpr uint32_t kSkater = 0x5B8AB877;      // "Skater"
constexpr uint32_t kString = 0x61414D56;      // "string"
constexpr uint32_t kLevel = 0x651533EC;       // "level"
constexpr uint32_t kMasterSkaterList = 0xF31E8FF8; // "master_skater_list"
constexpr uint32_t kName = 0xA1DC81F9;        // "name"
constexpr uint32_t kLastName = 0x37F745A5;    // "last_name"
constexpr uint32_t kHeading = 0xFD4BC03E;     // "heading"
constexpr uint32_t kPitch = 0xD8604126;       // "pitch"
constexpr uint32_t kR = 0x93F60062;           // "r"
constexpr uint32_t kG = 0xFE2BE489;           // "g"
constexpr uint32_t kBlue = 0x8E411006;        // "b" (the same checksum as "B")
constexpr uint32_t kNetSlap = 0xF9D5D933;     // "netslap"
constexpr uint32_t kSlap = 0xCA1F360F;        // "slap"
constexpr uint32_t kUiString = 0x96875C0F;    // "ui_string"
constexpr uint32_t kChecksum = 0x21902065;    // "checksum"
constexpr uint32_t kTime = 0x906B67BA;        // "time"
constexpr uint32_t kScore = 0xCD66C8AE;       // "score"

Mdl::Skate* Skate() { return Mdl::Skate::Instance(); }
// The game mode (0x007CE470): its name (0x005532A0) and fireball level (0x005531F0) - neither has source yet.
void* GameMode() { return static_cast<void*>(Game::g_gameMode); }
uint32_t GameModeName() { return static_cast<Game::CGameMode*>(GameMode())->GetNameChecksum(); }
// The rail editor (0x004F4970 finds it).
Ed::CRailEditorComponent* RailEditor() { return Ed::CRailEditorComponent::Instance(); }

// 0x005A9E80 (CFuncs::WrapAngle, texture_cfuncs.cpp): an angle in degrees brought into 0..360. The original reads
// its x87 result unrounded; ours is the float it returns (the last bits can differ when it adds or takes a turn).
double WrappedDegrees(float angle) { return CFuncs::WrapAngle(angle); }
// The half and whole turns compared and turned with (0x0064B278 180, 0x00645EA8 360).
float HalfTurn() { return g_gameUtilHalfTurn; }
float WholeTurn() { return g_gameUtilWholeTurn; }

// Whether `array` (if any) has the element `name`.
// The C runtime's _ftol (0x006236FC): truncated to a 64-bit integer, the low half kept.
int Ftol(double value) { return static_cast<int>(static_cast<int64_t>(value)); }

bool ArrayHas(CArray* array, uint32_t name)
{
    if (!array)
        return false;
    for (uint32_t i = 0; i < array->m_size; ++i)
        if (array->GetElement(i) == name)
            return true;
    return false;
}

} // namespace

// 0x005B1B60: InterpolateParameters A={...} B={...} 0x404E690D=t [ignore=[names]] [Angles=[names]]: 0xFF6F3872, a new
// structure with each named integer or float of A (unless ignored) interpolated towards B's value of that name (an
// integer or a float; skipped if B has neither): A + (B - A) * t - for the Angles the shorter way round (the
// difference and the result wrapped into 0-360 degrees). An integer stays an integer (truncated) only if both were integers. Always
// true.
bool __cdecl CFunc_InterpolateParameters(Script::CStruct* params, Script::CScript* script)
{
    CStruct* memory = Script::NewStruct();
    CStruct* result = memory ? memory->Init() : nullptr;
    CStruct* a = nullptr;
    params->GetStructure(kA, &a, 1);
    CStruct* b = nullptr;
    params->GetStructure(kB, &b, 1);
    float t = 0.0f;
    params->GetFloat(kFraction, &t, 0);
    CArray* ignore = nullptr;
    params->GetArray(kIgnore, &ignore, 0);
    CArray* angles = nullptr;
    params->GetArray(kAngles, &angles, 0);

    for (CComponent* component = a->GetNextComponent(nullptr); component;
         // (the original asks the command's params for the next one: the same, the component isn't null)
         component = params->GetNextComponent(component)) {
        if (ArrayHas(ignore, component->name))
            continue;
        bool integer = true;
        float from;
        uint8_t type = component->typeAndFlag >> 1;
        if (type == Script::ESYMBOLTYPE_INTEGER) {
            from = static_cast<float>(component->integer);
        } else if (type == Script::ESYMBOLTYPE_FLOAT) {
            from = component->floatValue;
            integer = false;
        } else {
            continue;
        }
        uint32_t name = component->name;
        if (!name)
            continue;
        float to = 0.0f;
        int toInteger = 0;
        if (b->GetInteger(name, &toInteger, 0)) {
            to = static_cast<float>(toInteger);
        } else if (b->GetFloat(name, &to, 0)) {
            integer = false;
        } else {
            continue;
        }
        float value;
        if (ArrayHas(angles, name)) {
            // x87: the wrapped difference, less a whole turn above pi, scaled and added in double
            double difference = WrappedDegrees(static_cast<float>(static_cast<double>(to) - from));
            if (difference > HalfTurn())
                difference = -(WholeTurn() - difference);
            value = static_cast<float>(WrappedDegrees(static_cast<float>(difference * t + from)));
        } else {
            value = static_cast<float>((static_cast<double>(to) - from) * t + from);
        }
        if (integer)
            result->AddInteger(name, Ftol(value));
        else
            result->AddFloat(name, value);
    }
    script->m_params->AddStructurePointer(kInterpolated, result);
    return true;
}

// 0x005B1E00: GetTextureFromPath path="dir/name": texture, the path's last part (after the last / or \). The name is
// copied into a 32-byte buffer, as in the original (a longer one overruns it). Always true.
bool __cdecl CFunc_GetTextureFromPath(Script::CStruct* params, Script::CScript* script)
{
    const char* path;
    params->GetString(kPath, &path, 1);
    char name[32];
    int length = static_cast<int>(std::strlen(path));
    int i = length;
    for (; i >= 0; --i)
        if (path[i] == '/' || path[i] == '\\')
            break;
    for (int from = i + 1; from <= length; ++from)
        name[from - (i + 1)] = path[from];
    script->m_params->AddString(kTexture, name);
    return true;
}

// 0x005B1EA0: ClearPowerups: for each of the skate module's skaters 0-7 that exists, the power-ups of the first
// SkaterState component the object manager has cleared - the same component each time (as in the original). Always
// true.
bool __cdecl CFunc_ClearPowerups(Script::CStruct*, Script::CScript*)
{
    Mdl::Skate* skate = Mdl::Skate::Instance();
    for (int i = 0; i < 8; ++i) {
        if (skate->GetSkater(i)) {
            auto* state = static_cast<Obj::CSkaterStateComponent*>(
                Obj::CompositeObjectManager()->GetFirstComponentByType(kSkaterState));
            state->ClearPowerups();
        }
    }
    return true;
}

// 0x005B2FC0: FindNearestRailPoint pos=(x,y,z) [0x213395F1=distance (150)] [IgnoreRailId=<rail>]
// [IgnoreRailPointIndex=n] [CheckLevelGeometry]: NearestPos, the nearest rail point (also rail_id and
// rail_point_index) or - with CheckLevelGeometry and when nearer - the park generator's nearest point, within the
// distance. False if neither is within it.
bool __cdecl CFunc_FindNearestRailPoint(Script::CStruct* params, Script::CScript* script)
{
    Mth::Vector pos;
    params->GetVector(kPos, reinterpret_cast<Script::VectorValue*>(&pos), 1);
    float distance = 150.0f;
    params->GetFloat(kRailDistance, &distance, 0);
    float maxDistanceSquared = static_cast<float>(static_cast<double>(distance) * distance);
    uint32_t ignoreRail = 0;
    params->GetChecksum(kIgnoreRailId, &ignoreRail, 0);
    int ignoreIndex = -1;
    params->GetInteger(kIgnoreRailPointIndex, &ignoreIndex, 0);

    float railDistanceSquared = 1.0e8f;
    uint32_t rail = 0;
    int railIndex = 0;
    Mth::Vector railPoint;
    RailEditor()->FindNearestPoint(&pos, &railPoint, &railDistanceSquared, &rail, &railIndex, ignoreRail, ignoreIndex);
    float geometryDistanceSquared = 1.0e8f;
    Mth::Vector geometryPoint;
    if (params->ContainsFlag(kCheckLevelGeometry))
        Ed::CParkEditor::FindRestart(&pos, &geometryPoint, &geometryDistanceSquared);
    if (!(railDistanceSquared < maxDistanceSquared) && !(geometryDistanceSquared < maxDistanceSquared))
        return false;
    Mth::Vector nearest;
    if (railDistanceSquared < geometryDistanceSquared) {
        nearest = railPoint;
        script->m_params->AddChecksum(kRailId, rail);
        script->m_params->AddInteger(kRailPointIndex, railIndex);
    } else {
        nearest = geometryPoint;
    }
    script->m_params->AddVector(kNearestPos, nearest.x, nearest.y, nearest.z);
    return true;
}

// 0x005B3160: GetParkName: 0xCC23BDDB, the park's name (the buffer at 0x007CED58). Returns false.
bool __cdecl CFunc_GetParkName(Script::CStruct*, Script::CScript* script)
{
    script->m_params->AddString(kParkName, CFuncs::g_parkName);
    return false;
}

// 0x005B3180: SetSpecialBarColors Skater=n: skater n's score takes the special bar's colours from the global array
// 0x2E8B9F1C (0x005612D0; the skater isn't checked for null). Always true.
bool __cdecl CFunc_SetSpecialBarColors(Script::CStruct* params, Script::CScript*)
{
    int index;
    params->GetInteger(kSkater, &index, 1);
    auto* skater = static_cast<Obj::CSkater*>(Skate()->GetSkater(index));
    static_cast<Mdl::Score*>(skater->GetScore())->SetSpecialBarColors();
    return true;
}

// 0x005B31C0: GetInitialsString: string, the records' default initials. Always true.
bool __cdecl CFunc_GetInitialsString(Script::CStruct*, Script::CScript* script)
{
    auto* records = Records::g_gameRecords;
    // (the initials through two getters folded with others: 0x005480C0, then 0x005902E0 - returns its argument)
    script->m_params->AddString(kString, Script::ESYMBOLTYPE_STRING, records->m_defaultInitials);
    return true;
}

// 0x005B31F0: SetInitialsString [string="ABC" ("ERR")]: the records' default initials. Always true.
bool __cdecl CFunc_SetInitialsString(Script::CStruct* params, Script::CScript*)
{
    auto* records = Records::g_gameRecords;
    const char* initials = "ERR"; // "ERR"
    params->GetString(kString, &initials, 0);
    records->SetDefaultInitials(initials);
    return true;
}

// 0x005B3230: GetFireballLevel: level, the game mode's fireball level. Always true.
bool __cdecl CFunc_GetFireballLevel(Script::CStruct*, Script::CScript* script)
{
    script->m_params->AddInteger(kLevel, static_cast<Game::CGameMode*>(GameMode())->GetFireballLevel());
    return true;
}

// 0x005B3260: GetSkaterLastNameString <name>: last_name, the master_skater_list entry's of that name. False if there
// is no such entry or it has no last_name.
bool __cdecl CFunc_GetSkaterLastNameString(Script::CStruct* params, Script::CScript* script)
{
    uint32_t name;
    params->GetChecksum(0u, &name, 1);
    CArray* skaters = Script::GetArray(kMasterSkaterList);
    int count = static_cast<int>(skaters->m_size);
    for (int i = 0; i < count; ++i) {
        auto* skater = reinterpret_cast<CStruct*>(skaters->GetElement(i));
        uint32_t entry;
        skater->GetChecksum(kName, &entry, 1);
        if (entry != name)
            continue;
        const char* lastName;
        if (!skater->GetString(kLastName, &lastName, 0))
            return false;
        script->m_params->AddString(kLastName, lastName);
        return true;
    }
    return false;
}

// 0x005B3300: DrawDirectionalLightLines heading=degrees pitch=degrees r= g= b=: a debug line from 75 above the local
// skater 50 long towards the light - compiled out of this build (the drawing call is empty); the local skater isn't
// checked for null. Always true.
bool __cdecl CFunc_DrawDirectionalLightLines(Script::CStruct* params, Script::CScript*)
{
    float heading;
    params->GetFloat(kHeading, &heading, 1);
    float pitch;
    params->GetFloat(kPitch, &pitch, 1);
    pitch = static_cast<float>(pitch * static_cast<double>(g_gameUtilMinusDegreesToRadians)); // -pi / 180
    heading = static_cast<float>((90.0 - heading) * g_degreesToRadians);          // pi / 180
    int r;
    params->GetInteger(kR, &r, 1);
    int g;
    params->GetInteger(kG, &g, 1);
    int b;
    params->GetInteger(kBlue, &b, 1);
    auto* skater = reinterpret_cast<uint8_t*>(Skate()->GetLocalSkater());
    const Mth::Vector& position = *reinterpret_cast<Mth::Vector*>(skater + 0x4C);
    double above = static_cast<double>(position.y) + 75.0;
    float horizontal = static_cast<float>(std::cos(static_cast<double>(pitch)) * 50.0);
    Mth::Vector from;
    from.x = position.x;
    from.y = static_cast<float>(above);
    from.z = position.z;
    from.w = 0.0f;
    Mth::Vector to;
    to.x = static_cast<float>(std::cos(static_cast<double>(heading)) * horizontal + position.x);
    to.y = static_cast<float>(std::sin(static_cast<double>(pitch)) * 50.0 + above);
    to.z = static_cast<float>(std::sin(static_cast<double>(heading)) * horizontal + position.z);
    to.w = 0.0f;
    uint32_t colour = 0xFF000000u | static_cast<uint32_t>(static_cast<uint8_t>(b)) << 16 |
                      static_cast<uint32_t>(static_cast<uint8_t>(g)) << 8 | static_cast<uint8_t>(r);
    Gfx::AddDebugLine(&from, &to, colour, colour, 1);
    return true;
}

// 0x005B3470: InSlapGame: whether the game mode is netslap or slap.
bool __cdecl CFunc_InSlapGame(Script::CStruct*, Script::CScript*)
{
    return GameModeName() == kNetSlap || GameModeName() == kSlap;
}

// 0x005B46A0: SetPreferencesFromUI <which preferences> string="..." field="name" [checksum=<value>] [time=n]
// [score=n]: the preference `field` set to {ui_string, checksum, time (time_limit / horse_time_limit), score (target
// score: score, else time * 1000)} in the preferences the params name (0x00565D40). Always true.
bool __cdecl CFunc_SetPreferencesFromUI(Script::CStruct* params, Script::CScript*)
{
    auto* preferences = Mdl::Skate::GetPreferencesFromParams(params);
    const char* string;
    params->GetString(kString, &string, 1);
    const char* field;
    params->GetString("field", &field, 1);
    CStruct* memory = Script::NewStruct();
    CStruct* value = memory ? memory->Init() : nullptr;
    value->AddString(kUiString, Script::ESYMBOLTYPE_STRING, string);
    uint32_t checksum = 0;
    params->GetChecksum("checksum", &checksum, 0);
    value->AddComponentOfType(kChecksum, Script::ESYMBOLTYPE_NAME, checksum);
    if (std::strcmp(field, "time_limit") == 0 || std::strcmp(field, "horse_time_limit") == 0) {
        int time = 0; // (uninitialised in the original without time)
        params->GetInteger("time", &time, 0);
        value->AddComponentOfType(kTime, Script::ESYMBOLTYPE_INTEGER, static_cast<uint32_t>(time));
    }
    if (std::strcmp(field, "target_score") == 0) {
        int score;
        if (!params->GetInteger(kScore, &score, 0)) {
            params->GetInteger(kTime, &score, 1);
            score *= 1000;
        }
        value->AddComponentOfType(kScore, Script::ESYMBOLTYPE_INTEGER, static_cast<uint32_t>(score));
    }
    preferences->SetPreference(Crc::GenerateCRCFromString(field), value);
    if (value) {
        value->Clear();
        Script::DeleteStruct(value);
    }
    return true;
}

} // namespace CFuncs
