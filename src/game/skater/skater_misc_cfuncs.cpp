// Script commands (CFunctions) of the skater: board logos, velocity, bail streams (see skater_misc_cfuncs.h).

#include "game/skater/skater_misc_cfuncs.h"
#include "game/gfx/d3dx.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "game/core/crc.h"
#include "game/core/pool.h"
#include "game/mth/matrix.h"
#include "game/mth/random.h"
#include "game/mth/vector.h"
#include "game/obj/object.h"
#include "game/pcm/music_api.h"
#include "game/script/cscript.h"
#include "game/script/struct.h"
#include "game/skate/profile_manager.h"
#include "game/skate/skate_module.h"
#include "game/skater/skater.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "game/gfx/cag_cfuncs.h"
#include "hook/game_code.h"

namespace CFuncs {

namespace {

using Script::CArray;
using Script::CStruct;

constexpr uint32_t kPart = 0xB6F08F39;            // "part"
constexpr uint32_t kPlayer = 0x67E6859A;          // "player"
constexpr uint32_t kSkewAngle = 0x5582FE27;       // "Skew_Angle"
constexpr uint32_t kVelX = 0x6187906D;            // "vel_x"
constexpr uint32_t kVelY = 0x1680A0FB;            // "vel_y"
constexpr uint32_t kVelZ = 0x8F89F141;            // "vel_z"
constexpr uint32_t kScale = 0x13B9DA7B;           // "scale"
constexpr uint32_t kScaledX = 0xBDED7A76;         // "scaled_x"
constexpr uint32_t kScaledY = 0xCAEA4AE0;         // "scaled_y"
constexpr uint32_t kScaledZ = 0x53E31B5A;         // "scaled_z"
constexpr uint32_t kType = 0x7321A8D6;            // "type"
constexpr uint32_t kStreamCount = 0x58492707;     // (name unknown) PlaySkaterStream: how many streams there are (10)
constexpr uint32_t kLastName = 0x37F745A5;        // "last_name"
constexpr uint32_t kVoice = 0x1804A7C4;           // "voice"
constexpr uint32_t kPlayerVoices = 0xCA1DB456;    // "player_voices"
constexpr uint32_t kId = 0x40C698AF;              // "id"
constexpr uint32_t kStreamName = 0x1A6BE294;      // "play_skater_stream_name"
constexpr uint32_t kIsMale = 0x3F813177;          // "is_male"
constexpr uint32_t kStreamChecksum = 0x0572EA28;  // "stream_checksum"
constexpr uint32_t kSkaterPlayBailStream = 0x38A0F1EC; // "skater_play_bail_stream"

// The C runtime's _ftol (0x006236FC): truncated to a 64-bit integer, the low half kept.
int Ftol(double value) { return static_cast<int>(static_cast<int64_t>(value)); }

// D3DXMatrixMultiply (the statically linked library's, 0x005BFA8C): out = a * b.
void MatrixMultiply(Mth::Matrix* out, const Mth::Matrix* a, const Mth::Matrix* b)
{
    D3dx::MatrixMultiply(out, a, b);
}

// The matrix turned about Y by `degrees` (the rotation: degrees in radians, rounded to float): m = m * rotation, or
// with `before` m = rotation * m.
void TurnAboutY(Mth::Matrix& m, float degrees, bool before)
{
    float radians = static_cast<float>(static_cast<double>(degrees) * g_degreesToRadians); // pi / 180
    Mth::Matrix rotation;
    Mth::Matrix& turn = Mth::CreateRotateYMatrix(rotation, radians);
    if (before)
        MatrixMultiply(&m, &turn, &m);
    else
        MatrixMultiply(&m, &m, &turn);
}

// strcpy (unbounded, as the original's).
void CopyString(char* to, const char* from) { std::memcpy(to, from, std::strlen(from) + 1); }

} // namespace

// 0x005B45F0: ApplyCustomLogoToPart part=<type> player=n: skater n's board element of that type shown (the skater
// isn't checked for null). Always true.
bool __cdecl CFunc_ApplyCustomLogoToPart(Script::CStruct* params, Script::CScript*)
{
    uint32_t part;
    params->GetChecksum(kPart, &part, 1);
    int player;
    params->GetInteger(kPlayer, &player, 1);
    static_cast<Obj::CSkater*>(Mdl::Skate::Instance()->GetSkater(player))->ShowBoardElement(part);
    return true;
}

// 0x005B4890: GetSkaterVelocity [Skew_Angle=degrees] [scale=s (1)]: vel_x / vel_y / vel_z, the script's object's
// velocity (truncated), and scaled_x / scaled_y / scaled_z, a direction times `scale`: moving faster than 100 across
// the ground, the horizontal velocity's direction, otherwise the object's facing (its matrix's third row) - turned
// about Y by the skew angle if there is one. False without an object.
bool __cdecl CFunc_GetSkaterVelocity(Script::CStruct* params, Script::CScript* script)
{
    auto* object = static_cast<Obj::CCompositeObject*>(script->m_object.mp_object);
    if (!object)
        return false;
    Mth::Vector velocity = object->m_vel;
    float scale = 1.0f;
    float skew = 0.0f;
    params->GetFloat(kSkewAngle, &skew, 0);
    script->m_params->AddInteger(kVelX, Ftol(velocity.x));
    script->m_params->AddInteger(kVelY, Ftol(velocity.y));
    script->m_params->AddInteger(kVelZ, Ftol(velocity.z));
    params->GetFloat(kScale, &scale, 0);
    velocity.y = 0.0f;
    // x87: z * z + x * x and its root in double
    double speed = std::sqrt(static_cast<double>(velocity.z) * velocity.z + static_cast<double>(velocity.x) * velocity.x);
    float x;
    float y;
    float z;
    if (speed > 100.0) {
        velocity.Normalize();
        if (skew == 0.0f) {
            x = velocity.x;
            y = velocity.y;
            z = velocity.z;
        } else {
            Mth::Matrix m;
            m.Ident();
            TurnAboutY(m, skew, false);
            Mth::Vector turned;
            m.Transform(turned, velocity);
            x = turned.x;
            y = turned.y;
            z = turned.z;
        }
    } else {
        const Mth::Vector& at = object->m_matrix.row[2];
        x = at.x;
        y = at.y;
        z = at.z;
        if (skew != 0.0f) {
            Mth::Matrix m;
            m = object->m_matrix;
            TurnAboutY(m, skew, true);
            x = m.row[2].x;
            y = m.row[2].y;
            z = m.row[2].z;
        }
    }
    script->m_params->AddInteger(kScaledX, Ftol(static_cast<double>(x) * scale));
    script->m_params->AddInteger(kScaledY, Ftol(static_cast<double>(y) * scale));
    script->m_params->AddInteger(kScaledZ, Ftol(static_cast<double>(z) * scale));
    return true;
}

// 0x005B4AF0: PlaySkaterStream type="..." [0x58492707=count (10)]: one of the script's skater's voice's streams
// "<voice>_<type><nn>" (nn 01..count, tried in a random order: the first that exists) played through
// skater_play_bail_stream (stream_checksum). The voice is a pro's last name, else the profile's voice's
// play_skater_stream_name in player_voices, else customM / customF by `is_male` (the original leaves the voice
// unset when the profile's voice isn't in the list). Always true.
bool __cdecl CFunc_PlaySkaterStream(Script::CStruct* params, Script::CScript* script)
{
    char voice[128];
    voice[0] = 0; // (left as the stack had it in the original when the voice is not found)
    char stream[128];
    char firstName[128];
    auto* skater = static_cast<Obj::CSkater*>(script->m_object.mp_object);
    const char* type;
    params->GetString(kType, &type, 1);
    int count = 10;
    params->GetInteger(kStreamCount, &count, 0);

    // the stream numbers 1..count, shuffled
    CArray* memory = Script::NewArray();
    CArray* order = memory ? memory->Init() : nullptr;
    order->SetSizeAndType(count, Script::ESYMBOLTYPE_INTEGER);
    for (int i = 0; i < count; ++i)
        order->SetElement(i, static_cast<uint32_t>(i + 1));
    for (int i = 0; i < count; ++i) {
        int other = Mth::Rnd(count);
        uint32_t a = order->GetElement(other);
        order->SetElement(other, order->GetElement(i));
        order->SetElement(i, a);
    }

    CopyString(firstName, skater->m_firstName); // (copied and not used)
    Obj::CSkaterProfile* profile = Obj::g_profileManager->GetTemplate(skater->m_name);
    if (skater->m_pro) {
        const char* lastName;
        profile->InfoStructure()->GetString(kLastName, &lastName, 1);
        CopyString(voice, lastName);
    } else {
        uint32_t voiceName;
        if (profile->InfoStructure()->GetChecksum(kVoice, &voiceName, 0)) {
            CArray* voices = Script::GetArray(kPlayerVoices);
            int size = static_cast<int>(voices->m_size);
            for (int i = 0; i < size; ++i) {
                auto* entry = reinterpret_cast<CStruct*>(voices->GetElement(i));
                uint32_t id;
                entry->GetChecksum(kId, &id, 1);
                if (id == voiceName) {
                    const char* name = nullptr;
                    entry->GetString(kStreamName, &name, 1);
                    CopyString(voice, name);
                    break;
                }
            }
        } else {
            int male;
            params->GetInteger(kIsMale, &male, 1);
            std::memcpy(voice, male == 1 ? "customM" : "customF", 8);
        }
    }

    for (int i = 0; i < count; ++i) {
        // "%s_%s%02i" (the original's sprintf is unbounded)
        std::snprintf(stream, sizeof(stream), GAME_TEXT(0x0064E048, "%s_%s%02i"), voice, type,
                      static_cast<int>(order->GetElement(i)));
        if (!Pcm::StreamExists(Crc::GenerateCRCFromString(stream)))
            continue;
        CStruct* structMemory = Script::NewStruct();
        CStruct* streamParams = structMemory ? structMemory->Init() : nullptr;
        streamParams->AddChecksum(kStreamChecksum, Crc::GenerateCRCFromString(stream));
        Script::RunScript(kSkaterPlayBailStream, streamParams, script->m_object.mp_object, false, nullptr);
        if (streamParams) {
            // (delete inlined: the structure pool pair's Free)
            streamParams->Clear();
            Script::g_structPools.Free(streamParams);
        }
        break;
    }
    Script::CleanUpArray(order);
    if (order) {
        // (delete inlined: the array pool pair's Free)
        order->Clear();
        Script::g_arrayPools.Free(order);
    }
    return true;
}

} // namespace CFuncs

REPLACE(0x005B45F0, CFuncs::CFunc_ApplyCustomLogoToPart);
REPLACE(0x005B4890, CFuncs::CFunc_GetSkaterVelocity);
REPLACE(0x005B4AF0, CFuncs::CFunc_PlaySkaterStream);
