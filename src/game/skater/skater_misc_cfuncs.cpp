// Script commands (CFunctions) of the skater: board logos, velocity, bail streams (see skater_misc_cfuncs.h).

#include "game/skater/skater_misc_cfuncs.h"
#include "game/gfx/d3dx.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "game/core/checksums.h"
#include "game/core/slab_bank.h"
#include "game/maths/mat4.h"
#include "game/maths/random.h"
#include "game/maths/vec4.h"
#include "game/obj/entity.h"
#include "game/audio/music_api.h"
#include "game/script/cscript.h"
#include "game/script/param_struct.h"
#include "game/skate/profile_manager.h"
#include "game/skate/skate_module.h"
#include "game/skater/rider.h"
#include "game/gfx/cag_cfuncs.h"

namespace ScriptCommands {

namespace {

using Scripts::CArray;
using Scripts::CStruct;

constexpr uint32_t kPart = 0xB6F08F39;            // "part"
constexpr uint32_t kPlayer = 0x67E6859A;          // "player"
constexpr uint32_t kTiltDegrees = 0x5582FE27;       // "Skew_Angle"
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
void MatrixMultiply(Maths::Mat4* out, const Maths::Mat4* a, const Maths::Mat4* b)
{
    D3dx::MatrixMultiply(out, a, b);
}

// The matrix turned about Y by `degrees` (the rotation: degrees in radians, rounded to float): m = m * rotation, or
// with `before` m = rotation * m.
void TurnAboutY(Maths::Mat4& m, float degrees, bool before)
{
    float radians = static_cast<float>(static_cast<double>(degrees) * g_degreeToRadianRatio); // pi / 180
    Maths::Mat4 rotation;
    Maths::Mat4& turn = Maths::MakeYawMatrix(rotation, radians);
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
bool __cdecl CFunc_ApplyCustomLogoToPart(Scripts::CStruct* params, Scripts::CScript*)
{
    uint32_t part;
    params->ReadHashValue(kPart, &part, 1);
    int player;
    params->FetchIntValue(kPlayer, &player, 1);
    static_cast<GameObjects::CRider*>(GameModules::Skate::Sole()->GetRiderAt(player))->ShowBoardElement(part);
    return true;
}

// 0x005B4890: GetSkaterVelocity [Skew_Angle=degrees] [scale=s (1)]: vel_x / vel_y / vel_z, the script's object's
// velocity (truncated), and scaled_x / scaled_y / scaled_z, a direction times `scale`: moving faster than 100 across
// the ground, the horizontal velocity's direction, otherwise the object's facing (its matrix's third row) - turned
// about Y by the skew angle if there is one. False without an object.
bool __cdecl CFunc_GetSkaterVelocity(Scripts::CStruct* params, Scripts::CScript* script)
{
    auto* object = static_cast<GameObjects::CGameEntity*>(script->m_entity.m_ownerObject);
    if (!object)
        return false;
    Maths::Vec4 velocity = object->m_vel;
    float scale = 1.0f;
    float skew = 0.0f;
    params->FetchFloat(kTiltDegrees, &skew, 0);
    script->m_args->PutInt(kVelX, Ftol(velocity.x));
    script->m_args->PutInt(kVelY, Ftol(velocity.y));
    script->m_args->PutInt(kVelZ, Ftol(velocity.z));
    params->FetchFloat(kScale, &scale, 0);
    velocity.y = 0.0f;
    // x87: z * z + x * x and its root in double
    double speed = std::sqrt(static_cast<double>(velocity.z) * velocity.z + static_cast<double>(velocity.x) * velocity.x);
    float x;
    float y;
    float z;
    if (speed > 100.0) {
        velocity.MakeUnit();
        if (skew == 0.0f) {
            x = velocity.x;
            y = velocity.y;
            z = velocity.z;
        } else {
            Maths::Mat4 m;
            m.ResetToIdentity();
            TurnAboutY(m, skew, false);
            Maths::Vec4 turned;
            m.Xform(turned, velocity);
            x = turned.x;
            y = turned.y;
            z = turned.z;
        }
    } else {
        const Maths::Vec4& at = object->m_basisFrame.row[2];
        x = at.x;
        y = at.y;
        z = at.z;
        if (skew != 0.0f) {
            Maths::Mat4 m;
            m = object->m_basisFrame;
            TurnAboutY(m, skew, true);
            x = m.row[2].x;
            y = m.row[2].y;
            z = m.row[2].z;
        }
    }
    script->m_args->PutInt(kScaledX, Ftol(static_cast<double>(x) * scale));
    script->m_args->PutInt(kScaledY, Ftol(static_cast<double>(y) * scale));
    script->m_args->PutInt(kScaledZ, Ftol(static_cast<double>(z) * scale));
    return true;
}

// 0x005B4AF0: PlaySkaterStream type="..." [0x58492707=count (10)]: one of the script's skater's voice's streams
// "<voice>_<type><nn>" (nn 01..count, tried in a random order: the first that exists) played through
// skater_play_bail_stream (stream_checksum). The voice is a pro's last name, else the profile's voice's
// play_skater_stream_name in player_voices, else customM / customF by `is_male` (the original leaves the voice
// unset when the profile's voice isn't in the list). Always true.
bool __cdecl CFunc_PlaySkaterStream(Scripts::CStruct* params, Scripts::CScript* script)
{
    char voice[128];
    voice[0] = 0; // (left as the stack had it in the original when the voice is not found)
    char stream[128];
    char firstName[128];
    auto* skater = static_cast<GameObjects::CRider*>(script->m_entity.m_ownerObject);
    const char* type;
    params->FetchText(kType, &type, 1);
    int count = 10;
    params->FetchIntValue(kStreamCount, &count, 0);

    // the stream numbers 1..count, shuffled
    CArray* memory = Scripts::AllocScriptArray();
    CArray* order = memory ? memory->PrepareAll() : nullptr;
    order->ConfigureSizeKind(count, Scripts::QBVAL_INTEGER);
    for (int i = 0; i < count; ++i)
        order->SetElement(i, static_cast<uint32_t>(i + 1));
    for (int i = 0; i < count; ++i) {
        int other = Maths::Rnd(count);
        uint32_t a = order->FetchPart(other);
        order->SetElement(other, order->FetchPart(i));
        order->SetElement(i, a);
    }

    CopyString(firstName, skater->m_givenName); // (copied and not used)
    GameObjects::CRiderIdentity* profile = GameObjects::g_profileRoster->GetTemplate(skater->m_label);
    if (skater->m_pro) {
        const char* lastName;
        profile->DetailsParams()->FetchText(kLastName, &lastName, 1);
        CopyString(voice, lastName);
    } else {
        uint32_t voiceName;
        if (profile->DetailsParams()->ReadHashValue(kVoice, &voiceName, 0)) {
            CArray* voices = Scripts::FetchList(kPlayerVoices);
            int size = static_cast<int>(voices->m_extent);
            for (int i = 0; i < size; ++i) {
                auto* entry = reinterpret_cast<CStruct*>(voices->FetchPart(i));
                uint32_t id;
                entry->ReadHashValue(kId, &id, 1);
                if (id == voiceName) {
                    const char* name = nullptr;
                    entry->FetchText(kStreamName, &name, 1);
                    CopyString(voice, name);
                    break;
                }
            }
        } else {
            int male;
            params->FetchIntValue(kIsMale, &male, 1);
            std::memcpy(voice, male == 1 ? "customM" : "customF", 8);
        }
    }

    for (int i = 0; i < count; ++i) {
        // "%s_%s%02i" (the original's sprintf is unbounded)
        std::snprintf(stream, sizeof(stream), "%s_%s%02i", voice, type,
                      static_cast<int>(order->FetchPart(i)));
        if (!AudioStream::StreamExists(Checksums::HashFromText(stream)))
            continue;
        CStruct* structMemory = Scripts::SpawnParamBag();
        CStruct* streamParams = structMemory ? structMemory->PrepareAll() : nullptr;
        streamParams->PutNameKey(kStreamChecksum, Checksums::HashFromText(stream));
        Scripts::LaunchScript(kSkaterPlayBailStream, streamParams, script->m_entity.m_ownerObject, false, nullptr);
        if (streamParams) {
            // (delete inlined: the structure pool pair's Free)
            streamParams->Clear();
            Scripts::g_structPools.Dealloc(streamParams);
        }
        break;
    }
    Scripts::ReleaseArrayStorage(order);
    if (order) {
        // (delete inlined: the array pool pair's Free)
        order->Clear();
        Scripts::g_arrayPools.Dealloc(order);
    }
    return true;
}

} // namespace ScriptCommands
