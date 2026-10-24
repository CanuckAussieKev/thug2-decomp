// Script commands (CFunctions) of the game: interpolating parameters, a texture's name, power-ups (see
// game_util_cfuncs.h).

#include "game/script/game_util_cfuncs.h"

#include <cstdint>
#include <cstring>

#include "game/obj/compositemgr.h"
#include "game/script/cscript.h"
#include "game/script/struct.h"
#include "game/skate/skate_module.h"
#include "game/skater/skater_state.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace CFuncs {

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

// 0x005A9E80 (no source yet): an angle brought into the range (-pi, pi] by whole turns (cdecl; the result in ST0,
// read at full precision as the original's callers use it).
double WrapAngle(float angle) { return GameFunction<double(__cdecl*)(float)>(0x005A9E80)(angle); }
// The constants it compares and turns with (0x0064B278 pi, 0x00645EA8 a whole turn).
float Pi() { return GameVariable<float>(0x0064B278); }
float WholeTurn() { return GameVariable<float>(0x00645EA8); }

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
// difference and the result wrapped). An integer stays an integer (truncated) only if both were integers. Always
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
            double difference = WrapAngle(static_cast<float>(static_cast<double>(to) - from));
            if (difference > Pi())
                difference = -(WholeTurn() - difference);
            value = static_cast<float>(WrapAngle(static_cast<float>(difference * t + from)));
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

} // namespace CFuncs

REPLACE(0x005B1B60, CFuncs::CFunc_InterpolateParameters);
REPLACE(0x005B1E00, CFuncs::CFunc_GetTextureFromPath);
REPLACE(0x005B1EA0, CFuncs::CFunc_ClearPowerups);
