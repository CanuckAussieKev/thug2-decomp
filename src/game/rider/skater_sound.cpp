// The skater's sound component (GameObjects::RiderSfxPlayer): see skater_sound.h.

#include "game/rider/skater_sound.h"

#include <cmath>
#include <cstdint>

#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/maths/vec4.h"
#include "engine/script/param_struct.h"
#include "engine/sound/surface_kind.h"
#include "game/rider/thaw_bmx.h"
#include "game/rider/skitch.h"
#include "engine/objects/car_physics.h"
#include "engine/objects/node_components.h"
#include "engine/objects/entity.h"
#include "engine/objects/camera_lookaround.h"

namespace objects {

namespace {

constexpr uint32_t kSkaterSound = 0x187574FA;
constexpr uint32_t kVolumeMult = 0xF1A99B27;
constexpr uint32_t kPlayJumpSound = 0x0E338369, kPlayBonkSound = 0x0069E457, kPlayLandSound = 0x2C779F22,
                   kPlayCessSound = 0x6F5E9124;
// The terrain sound tables
constexpr uint32_t kTableLand = 0x1D296AEC, kTableGrindLand = 0x80E4427E, kTableSlideLand = 0x840278CC,
                   kTableJump = 0x129AB922, kTableGrindJump = 0x8F5791B0, kTableSlideJump = 0x8BB1AB02,
                   kTableBonk = 0x67D72D44, kTableCess = 0x3EFC3BBD;

constexpr int kStateGround = 0, kStateAir = 1, kStateRail = 4;
constexpr float kPercent = 100.0f; // 0x006463D4

// The terrain sounds: a table by name; `terrain`'s sound in it played at `pos` (and sent to the other players).
uint8_t sound_table(uint32_t name) { return surface_kind::get_table_index(name); }
void play_terrain_sound(uint32_t table, int terrain, const maths::Vec4* pos, float volume, float percent)
{
    surface_kind::play_terrain_sound(sound_table(table), static_cast<uint8_t>(terrain), pos, volume, percent, percent,
                                     true);
}

bool on_ground_or_in_air(int state) { return state == kStateGround || state == kStateAir; }

// Not original: THAW's BMX (game/rider/thaw_bmx.h) - on the bike its own sound for `kind` instead of the terrain's
// (THAW 0x005F197B / 0x005F1A10 / 0x005F1AB0: BIKE_LAND, BIKE_GRINDLAND, BIKE_OLLIE, BIKE_GRINDJUMP, BIKE_BONK).
// Whether it was the bike's (played, or none).
bool play_bike_sound(GameEntity* skater, skater::thaw_bmx::BikeSound kind, float volume, float percent)
{
    if (!skater::thaw_bmx::is_switched_on())
        return false;
    surface_kind::SoundInfo info;
    int bike = skater::thaw_bmx::bike_sound_of(skater, kind, &info);
    if (bike < 0)
        return false;
    if (bike > 0)
        surface_kind::play_sound_info(info, &skater->at_point_, volume, percent);
    return true;
}

// The speed (x, y, z summed in that order) over full speed.
float full_length(const maths::Vec4& v, float full_speed)
{
    double x = v.x, y = v.y, z = v.z;
    return static_cast<float>(std::sqrt((x * x + y * y) + z * z) / full_speed);
}

} // namespace

// 0x00518E20
memory::owner<ObjectModule*> RiderSfxPlayer::fabricate() { return new RiderSfxPlayer; }

// (the constructor: inlined in fabricate in the original)
RiderSfxPlayer::RiderSfxPlayer()
{
    full_speed_ = 1.0f;
    volume_mult_ = 1.0f;
    kind_tag_ = kSkaterSound;
    sliding_ = 0;
}

// 0x00518C80 (0x00518C90 the deleting one)
RiderSfxPlayer::~RiderSfxPlayer() = default;

// 0x00518CC0
void RiderSfxPlayer::configure_from_data(script::Struct* params)
{
    float mult;
    if (params->fetch_float(kVolumeMult, &mult, 0))
        volume_mult_ = mult;
}

// 0x00518CF0
void RiderSfxPlayer::play_land(float speed, int terrain)
{
    uint32_t table = cur_phase_ == kStateRail ? (sliding_ ? kTableSlideLand : kTableGrindLand) : kTableLand;
    auto percent = static_cast<float>(static_cast<double>(speed) * kPercent);
    auto volume = static_cast<float>(static_cast<double>(percent) * volume_mult_);
    using skater::thaw_bmx::BikeSound;
    if (play_bike_sound(entity_, cur_phase_ == kStateRail ? BikeSound::kGrindLand : BikeSound::kLand, volume, percent))
        return;
    play_terrain_sound(table, terrain, &entity_->at_point_, volume, percent);
}

// 0x00518D60
void RiderSfxPlayer::play_jump(float speed, int terrain, int state)
{
    uint32_t table = state == kStateRail ? (sliding_ ? kTableSlideJump : kTableGrindJump) : kTableJump;
    auto percent = static_cast<float>(static_cast<double>(speed) * kPercent);
    auto volume = static_cast<float>(static_cast<double>(percent) * volume_mult_);
    using skater::thaw_bmx::BikeSound;
    if (play_bike_sound(entity_, state == kStateRail ? BikeSound::kGrindJump : BikeSound::kOllie, volume, percent))
        return;
    play_terrain_sound(table, terrain, &entity_->at_point_, volume, percent);
}

// 0x00518DD0
void RiderSfxPlayer::play_bonk(float speed, int terrain)
{
    auto percent = static_cast<float>(static_cast<double>(speed) * kPercent);
    auto volume = static_cast<float>(static_cast<double>(percent) * volume_mult_);
    if (play_bike_sound(entity_, skater::thaw_bmx::BikeSound::kBonk, volume, percent))
        return;
    play_terrain_sound(kTableBonk, terrain, &entity_->at_point_, volume, percent);
}

// 0x00518EA0
void RiderSfxPlayer::play_land_here(float speed)
{
    play_land(speed, on_ground_or_in_air(cur_phase_) ? last_ground_ : surface_type_);
}

// 0x00518FA0
void RiderSfxPlayer::play_jump_here(float speed)
{
    play_jump(speed, on_ground_or_in_air(cur_phase_) ? last_ground_ : surface_type_, cur_phase_);
}

// 0x00518EE0
void RiderSfxPlayer::play_jump_in_state(float speed, int terrain) { play_jump(speed, terrain, cur_phase_); }

// 0x00518F00
void RiderSfxPlayer::play_bonk_here(float speed)
{
    auto percent = static_cast<float>(static_cast<double>(speed) * kPercent);
    auto volume = static_cast<float>(static_cast<double>(percent) * volume_mult_);
    if (play_bike_sound(entity_, skater::thaw_bmx::BikeSound::kBonk, volume, percent))
        return;
    play_terrain_sound(kTableBonk, surface_type_, &entity_->at_point_, volume, percent);
}

// 0x00518F50
void RiderSfxPlayer::play_cess_here(float speed)
{
    auto percent = static_cast<float>(static_cast<double>(speed) * kPercent);
    auto volume = static_cast<float>(static_cast<double>(percent) * volume_mult_);
    play_terrain_sound(kTableCess, surface_type_, &entity_->at_point_, volume, percent);
}

// 0x00518FD0
int RiderSfxPlayer::invoke_method_by_name(uint32_t name, script::Struct* /*params*/, script::Script* /*script*/)
{
    const maths::Vec4& vel = entity_->vel_;
    switch (name) {
    case kPlayJumpSound:
        if (cur_phase_ >= 0 && (cur_phase_ <= kStateAir || cur_phase_ == kStateRail))
            play_jump_here(static_cast<float>(vel.len_of() / full_speed_));
        return 1;
    case kPlayBonkSound:
        play_bonk_here(full_length(vel, full_speed_));
        return 1;
    case kPlayLandSound:
        if (cur_phase_ >= 0 && cur_phase_ <= kStateAir)
            play_land_here(static_cast<float>(std::fabs(vel.y) / full_speed_));
        else if (cur_phase_ == kStateRail)
            play_land_here(static_cast<float>(vel.len_of() / full_speed_));
        return 1;
    case kPlayCessSound:
        play_cess_here(full_length(vel, full_speed_));
        return 1;
    default:
        return 2;
    }
}

} // namespace objects
