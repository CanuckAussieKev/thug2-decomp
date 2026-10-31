// Script commands (CFunctions) of the skater: board logos, velocity, bail streams (see skater_misc_cfuncs.h).

#include "game/rider/skater_misc_cfuncs.h"
#include "engine/gfx/d3dx.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "engine/core/checksums.h"
#include "engine/core/slab_bank.h"
#include "engine/maths/mat4.h"
#include "engine/maths/random.h"
#include "engine/maths/vec4.h"
#include "engine/objects/entity.h"
#include "engine/audio/music_api.h"
#include "engine/script/cscript.h"
#include "engine/script/param_struct.h"
#include "game/skate/profile_manager.h"
#include "game/skate/skate_module.h"
#include "game/rider/rider.h"
#include "engine/gfx/cag_cfuncs.h"

namespace commands {

namespace {

using script::Array;
using script::Struct;

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
int ftol(double value) { return static_cast<int>(static_cast<int64_t>(value)); }

// D3DXMatrixMultiply (the statically linked library's, 0x005BFA8C): out = a * b.
void matrix_multiply(maths::Mat4* out, const maths::Mat4* a, const maths::Mat4* b)
{
    d3dx::matrix_multiply(out, a, b);
}

// The matrix turned about Y by `degrees` (the rotation: degrees in radians, rounded to float): m = m * rotation, or
// with `before` m = rotation * m.
void turn_about_y(maths::Mat4& m, float degrees, bool before)
{
    float radians = static_cast<float>(static_cast<double>(degrees) * g_degree_to_radian_ratio); // pi / 180
    maths::Mat4 rotation;
    maths::Mat4& turn = maths::make_yaw_matrix(rotation, radians);
    if (before)
        matrix_multiply(&m, &turn, &m);
    else
        matrix_multiply(&m, &m, &turn);
}

// strcpy (unbounded, as the original's).
void copy_string(char* to, const char* from) { std::memcpy(to, from, std::strlen(from) + 1); }

} // namespace

// 0x005B45F0: ApplyCustomLogoToPart part=<type> player=n: skater n's board element of that type shown (the skater
// isn't checked for null). Always true.
bool __cdecl cfunc_apply_custom_logo_to_part(script::Struct* params, script::Script*)
{
    uint32_t part;
    params->read_hash_value(kPart, &part, 1);
    int player;
    params->fetch_int_value(kPlayer, &player, 1);
    static_cast<objects::Rider*>(modules::Skate::sole()->get_rider_at(player))->show_board_element(part);
    return true;
}

// 0x005B4890: GetSkaterVelocity [Skew_Angle=degrees] [scale=s (1)]: vel_x / vel_y / vel_z, the script's object's
// velocity (truncated), and scaled_x / scaled_y / scaled_z, a direction times `scale`: moving faster than 100 across
// the ground, the horizontal velocity's direction, otherwise the object's facing (its matrix's third row) - turned
// about Y by the skew angle if there is one. False without an object.
bool __cdecl cfunc_get_skater_velocity(script::Struct* params, script::Script* script)
{
    auto* object = static_cast<objects::GameEntity*>(script->entity_.owner_object_);
    if (!object)
        return false;
    maths::Vec4 velocity = object->vel_;
    float scale = 1.0f;
    float skew = 0.0f;
    params->fetch_float(kTiltDegrees, &skew, 0);
    script->args_->put_int(kVelX, ftol(velocity.x));
    script->args_->put_int(kVelY, ftol(velocity.y));
    script->args_->put_int(kVelZ, ftol(velocity.z));
    params->fetch_float(kScale, &scale, 0);
    velocity.y = 0.0f;
    // x87: z * z + x * x and its root in double
    double speed = std::sqrt(static_cast<double>(velocity.z) * velocity.z + static_cast<double>(velocity.x) * velocity.x);
    float x;
    float y;
    float z;
    if (speed > 100.0) {
        velocity.make_unit();
        if (skew == 0.0f) {
            x = velocity.x;
            y = velocity.y;
            z = velocity.z;
        } else {
            maths::Mat4 m;
            m.reset_to_identity();
            turn_about_y(m, skew, false);
            maths::Vec4 turned;
            m.xform(turned, velocity);
            x = turned.x;
            y = turned.y;
            z = turned.z;
        }
    } else {
        const maths::Vec4& at = object->basis_frame_.row[2];
        x = at.x;
        y = at.y;
        z = at.z;
        if (skew != 0.0f) {
            maths::Mat4 m;
            m = object->basis_frame_;
            turn_about_y(m, skew, true);
            x = m.row[2].x;
            y = m.row[2].y;
            z = m.row[2].z;
        }
    }
    script->args_->put_int(kScaledX, ftol(static_cast<double>(x) * scale));
    script->args_->put_int(kScaledY, ftol(static_cast<double>(y) * scale));
    script->args_->put_int(kScaledZ, ftol(static_cast<double>(z) * scale));
    return true;
}

// 0x005B4AF0: PlaySkaterStream type="..." [0x58492707=count (10)]: one of the script's skater's voice's streams
// "<voice>_<type><nn>" (nn 01..count, tried in a random order: the first that exists) played through
// skater_play_bail_stream (stream_checksum). The voice is a pro's last name, else the profile's voice's
// play_skater_stream_name in player_voices, else customM / customF by `is_male` (the original leaves the voice
// unset when the profile's voice isn't in the list). Always true.
bool __cdecl cfunc_play_skater_stream(script::Struct* params, script::Script* script)
{
    char voice[128];
    voice[0] = 0; // (left as the stack had it in the original when the voice is not found)
    char stream[128];
    char first_name[128];
    auto* skater = static_cast<objects::Rider*>(script->entity_.owner_object_);
    const char* type;
    params->fetch_text(kType, &type, 1);
    int count = 10;
    params->fetch_int_value(kStreamCount, &count, 0);

    // the stream numbers 1..count, shuffled
    Array* memory = script::alloc_script_array();
    Array* order = memory ? memory->prepare_all() : nullptr;
    order->configure_size_kind(count, script::kQbvalInteger);
    for (int i = 0; i < count; ++i)
        order->set_element(i, static_cast<uint32_t>(i + 1));
    for (int i = 0; i < count; ++i) {
        int other = maths::rnd(count);
        uint32_t a = order->fetch_part(other);
        order->set_element(other, order->fetch_part(i));
        order->set_element(i, a);
    }

    copy_string(first_name, skater->given_name_); // (copied and not used)
    objects::RiderIdentity* profile = objects::g_profile_roster->get_template(skater->label_);
    if (skater->pro_) {
        const char* last_name;
        profile->details_params()->fetch_text(kLastName, &last_name, 1);
        copy_string(voice, last_name);
    } else {
        uint32_t voice_name;
        if (profile->details_params()->read_hash_value(kVoice, &voice_name, 0)) {
            Array* voices = script::fetch_list(kPlayerVoices);
            int size = static_cast<int>(voices->extent_);
            for (int i = 0; i < size; ++i) {
                auto* entry = reinterpret_cast<Struct*>(voices->fetch_part(i));
                uint32_t id;
                entry->read_hash_value(kId, &id, 1);
                if (id == voice_name) {
                    const char* name = nullptr;
                    entry->fetch_text(kStreamName, &name, 1);
                    copy_string(voice, name);
                    break;
                }
            }
        } else {
            int male;
            params->fetch_int_value(kIsMale, &male, 1);
            std::memcpy(voice, male == 1 ? "customM" : "customF", 8);
        }
    }

    for (int i = 0; i < count; ++i) {
        // "%s_%s%02i" (the original's sprintf is unbounded)
        std::snprintf(stream, sizeof(stream), "%s_%s%02i", voice, type,
                      static_cast<int>(order->fetch_part(i)));
        if (!audio::stream_exists(checksums::hash_from_text(stream)))
            continue;
        Struct* struct_memory = script::spawn_param_bag();
        Struct* stream_params = struct_memory ? struct_memory->prepare_all() : nullptr;
        stream_params->put_name_key(kStreamChecksum, checksums::hash_from_text(stream));
        script::launch_script(kSkaterPlayBailStream, stream_params, script->entity_.owner_object_, false, nullptr);
        if (stream_params) {
            // (delete inlined: the structure pool pair's Free)
            stream_params->get_clear();
            script::g_struct_pools.dealloc(stream_params);
        }
        break;
    }
    script::release_array_storage(order);
    if (order) {
        // (delete inlined: the array pool pair's Free)
        order->get_clear();
        script::g_array_pools.dealloc(order);
    }
    return true;
}

} // namespace commands
