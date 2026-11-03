// Pieces of the skater object, 0x0057FE30-0x00580BD8. See rider.h.

#include "game/rider/rider.h"

#include <bit>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <memory>
#include <new>
#include <array>
#include <string_view>

#include "engine/core/arena_alloc.h"
#include "engine/core/checksums.h"
#include "engine/net/buddy_status.h"
#include "engine/gfx/engine_model_lights.h"
#include "engine/core/dbg.h"
#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/gfx/view_cam.h"
#include "engine/maths/mat4.h"
#include "engine/net/net_app.h"
#include "engine/net/net_client.h"
#include "engine/net/net_dispatcher.h"
#include "engine/script/cell.h"
#include "engine/script/nodes.h"
#include "engine/pad_input/device.h"
#include "game/skate/profile_manager.h"
#include "engine/maths/random.h"
#include "engine/maths/vec4.h"
#include "engine/objects/compositemgr.h"
#include "engine/objects/game_objects.h"
#include "engine/objects/tracker.h"
#include "engine/script/cscript.h"
#include "engine/script/param_struct.h"
#include "engine/assets/resource_registry.h"
#include "frontend/front_end.h"
#include "engine/net/gamenet_manager.h"
#include "engine/net/player_info.h"
#include "engine/objects/anim_blender.h"
#include "engine/objects/control_pad.h"
#include "engine/objects/model_component.h"
#include "engine/objects/proximity.h"
#include "engine/objects/rigidbody_component.h"
#include "engine/objects/shadow_component.h"
#include "engine/objects/skeleton_component.h"
#include "engine/objects/vibration_component.h"
#include "engine/objects/walk_hang.h"
#include "engine/sound/sfx.h"
#include "game/skate/career.h"
#include "game/rider/core_physics.h"
#include "game/rider/end_run.h"
#include "game/rider/flip_and_rotate.h"
#include "game/rider/gap_entry.h"
#include "game/rider/looping_sound.h"
#include "game/rider/nonlocal_net_logic.h"
#include "game/rider/physics_control.h"
#include "game/rider/skater_score.h"
#include "game/rider/skater_state.h"
#include "game/rider/special_item.h"
#include "game/rider/stats_manager.h"
#include "game/rider/tagging.h"
#include "game/rider/thaw_bmx.h"
#include "game/rider/trick_component.h"
#include "game/rider/walk_camera.h"
#include "game/rider/walk_physics.h"
#include "platform/game_clock.h"
#include "game/goals/create_a_trick.h"
#include "game/goals/goal_manager.h"
#include "engine/gfx/texture_dict.h"
#include "engine/gfx/viewport.h"
#include "engine/objects/ped_logic.h"
#include "engine/script/game_cfuncs.h"
#include "game/skate/game_mode.h"
#include "game/skate/point_tally.h"
#include "game/skate/skate_module.h"
#include "game/rider/balance_trick.h"
#include "game/rider/skater_camera_component.h"
#include "game/park_editing/park_editor.h"
#include "engine/objects/entity.h"
#include "engine/objects/car_physics.h"
#include "game/rider/state_history.h"
#include "game/rider/local_net_logic.h"
#include "engine/gfx/engine_geom.h"
#include "engine/gfx/engine_texture.h"
#include "game/goals/objective.h"
#include "game/rider/skater_sound.h"
#include "engine/objects/input_component.h"
#include "engine/objects/suspend_component.h"
#include "game/rider/stance_panel.h"
#include "engine/objects/collision_component.h"
#include "engine/render/coll_obj.h"
#include "game/rider/vehicle_sound.h"
#include "engine/gfx/model.h"
#include "engine/core/down_cast.h"
#include "engine/core/checked_index.h"
#include <gsl/pointers>
#include <gsl/assert>
#include "engine/core/format.h"
#include "engine/core/no_destroy.h"
#include "engine/gfx/engine_parts.h"
#include "engine/net/net_game_state.h"
#include "wire/message_ids.h"

namespace objects {

namespace {
constexpr float kStatCeiling = 10.0f;              // a stat goes no higher than this
constexpr int kGoalStatusCount = 0x46;
constexpr float kOutOfGameStat = 10.0f;            // the stat of a player not in the game (net games)
constexpr float kCarryingFlagStat = 6.0f;
constexpr float kCheatStat = 15.0f;
constexpr double kSpecialStatBonus = 3.0;          // added to a stat while the special is on
constexpr float kStatToFraction = 0.1f;            // stats count in tenths
constexpr int kMaxRestartTries = 20;
constexpr int kRestartCandidates = 8;
constexpr float kLightHorizontal = 0.8f;           // the skater's light colour
constexpr float kLightDepth = 0.3f;
constexpr float kLightMinDrop = 0.65f;
constexpr int kShadowFadeFrames = 16;
constexpr float kShadowFadeStep = 0.0625f;
constexpr int kStatCount = 10;
constexpr int kLoopPoseFlags = 24;
constexpr int kSlideFlag = static_cast<int>(PoseFlag::kSlide);
constexpr float kQuarterTurnRadians = 1.5707964f;
constexpr float kDefaultAnimBlend = 0.3f;
constexpr std::size_t kProjectileNameBytes = 32;
constexpr int kListenerPriority = 0x80;
constexpr float kNearLodDistance = 3000.0f;        // the lod switch distances, in feet
constexpr float kMidLodDistance = 5000.0f;
constexpr float kFarLodDistance = 10000000.0f;
constexpr float kNoHitDistance = 1000.0f;
constexpr int kListPriority = 500;                 // the skater's list link runs at the negative of this
constexpr int kSourceLineSkaterNumber = 0x43C;     // the source lines the original reports its asserts from
constexpr int kSourceLineId = 0x43D;
constexpr unsigned kByteMask = 0xFF;
constexpr float kCameraBackOffset = 144.0f;
constexpr float kRecoverSpeed = 10.0f;
constexpr uint32_t kSegway = 0x3029FB1F; // Segway
constexpr uint32_t kSegwayPhysics = 0x8B3B782B; // segway_physics
constexpr uint32_t kMechbull = 0x61C62B2D; // mechbull
constexpr uint32_t kMechbullPhysics = 0x05716042; // mechbull_physics
constexpr uint32_t kMiniKart = 0x86929898; // MiniKart
constexpr uint32_t kMinikartPhysics = 0x8711BC92; // minikart_physics
constexpr uint32_t kWheelchair = 0xB2C99BF0; // Wheelchair
constexpr uint32_t kWheelchairPhysics = 0x871FFB73; // wheelchair_physics
constexpr uint32_t kTricycle = 0x906F06E2; // Tricycle
constexpr uint32_t kTricyclePhysics = 0x43DA544E; // tricycle_physics
constexpr uint32_t kGurney = 0xD8D2D646; // Gurney
constexpr uint32_t kGurneyPhysics = 0x08C9AD5E; // gurney_physics
constexpr uint32_t kChainsaw = 0x14A3A4CF; // Chainsaw
constexpr uint32_t kUnsetStatBits = std::bit_cast<uint32_t>(-1.0f); // -1.0f's bits: no override (compared as bits)
constexpr uint32_t kSkaterBalanceTrick = 0x589E5FB3; // SkaterBalanceTrick
constexpr uint32_t kInitSkaterParticles = 0xAE5438AF; // InitSkaterParticles
constexpr uint32_t kSkaterState = 0x43686585; // SkaterState
constexpr uint32_t kSkaterStancePanel = 0x21FC3301; // SkaterStancePanel
constexpr uint32_t kBoard = 0xA7A9D4B8; // Board
constexpr uint32_t kFrontLogo = 0x23A1DF80; // front_logo
constexpr uint32_t kCustomFrontlogoParams = 0x53409C6B; // custom_frontlogo_params
constexpr uint32_t kDeckGraphic = 0x07CB4FA9; // deck_graphic
constexpr uint32_t kCustomDecklogoParams = 0xC4BFAEC3; // custom_decklogo_params
constexpr uint32_t kHatLogo = 0x5B288E0B; // hat_logo
constexpr uint32_t kCustomHatlogoParams = 0xEF76A8DB; // custom_hatlogo_params
constexpr uint32_t kBackLogo = 0xD0C656A2; // back_logo
constexpr uint32_t kCustomBacklogoParams = 0xFFE9F52A; // custom_backlogo_params
constexpr uint32_t kIn = 0xA01371B1; // in
constexpr uint32_t kXScale = 0x6AA8D676; // x_scale
constexpr uint32_t kYScale = 0xCCDFDDC2; // y_scale
constexpr uint32_t kWith = 0x676F1DF1; // With
constexpr uint32_t kTempTexture = 0xC553C612; // temp_texture
constexpr uint32_t kTagging = 0x5B512EDC; // Tagging
constexpr uint32_t kRotate = 0xC876ADF3; // Rotate
constexpr uint32_t kIsDeck = 0x25A0E7E9; // is_deck
constexpr uint32_t kBaseTexture = 0x499F4BE9; // base_texture
constexpr uint32_t kLogoTexture = 0x49B318FD; // logo_texture
constexpr uint32_t kSkaterNonLocalNetLogic = 0x850EEF66; // SkaterNonLocalNetLogic
constexpr uint32_t kSkaterLoopingSound = 0xB5F1E9D2; // SkaterLoopingSound
constexpr uint32_t kPauseRunTimer = 0x1277E3FD; // pause_run_timer
constexpr uint32_t kSkaterCamera = 0x5E43A604; // SkaterCamera
constexpr uint32_t kWalkCamera = 0xC7D37EC2; // WalkCamera
constexpr uint32_t kCameraTarget = 0x4C48DA58; // CameraTarget
constexpr uint32_t kWalk = 0x726E85AA; // Walk
constexpr uint32_t kTHPS6Human = 0x9FE476E2; // THPS6_Human
constexpr uint32_t kSkaterAssetInfo = 0x962E5CF7; // skater_asset_info
constexpr uint32_t kSkaterSkeleton = 0xCEEFD662; // skater_skeleton
constexpr uint32_t kAnimBlender = 0x7149B753; // AnimBlender
constexpr uint32_t kSkeletonName = 0x09794932; // SkeletonName
constexpr uint32_t kAnimEventTableName = 0x5B8C6DC2; // AnimEventTableName
constexpr uint32_t kSkaterAnimEventTable = 0x3FEC31A6; // SkaterAnimEventTable
constexpr uint32_t kSkeleton2 = 0x222756D5; // Skeleton
constexpr uint32_t kMaxBoneSkipLod = 0xD3982061; // max_bone_skip_lod
constexpr uint32_t kUseModelLights = 0x10079F2D; // UseModelLights
constexpr uint32_t kMaxSceneLights = 0xD223B8BD; // Max_Scene_Lights
constexpr uint32_t kShadowVolume = 0x2A1F92C0; // ShadowVolume
constexpr uint32_t kShadow = 0x8A897DD2; // Shadow
constexpr uint32_t kShadowType = 0x9AC24B18; // ShadowType
constexpr uint32_t kDetailed = 0x76A54CD1; // Detailed
constexpr uint32_t kVibration = 0x404EB936; // Vibration
constexpr uint32_t kSkaterSound = 0x187574FA; // SkaterSound
constexpr uint32_t kSkaterVehicleSound = 0x1859F772; // SkaterVehicleSound
constexpr uint32_t kCollision = 0xA432DC41; // Collision
constexpr uint32_t kDefaultName = 0x1CA1FF20; // Default
constexpr uint32_t kSpawnAddPremadeCatsToSkater = 0x0BDC1C89; // spawn_add_premade_cats_to_skater
constexpr uint32_t kSkaterPhysicsControlSwitchWalkingToSkating = 0x5C038F9B; // SkaterPhysicsControl_SwitchWalkingToSkating
constexpr uint32_t kTagTexture = 0x4FA8DF27; // tag_texture
constexpr uint32_t kHasCustomTag = 0xB78D4AAE; // has_custom_tag
constexpr uint32_t kStickerTexture = 0x130618A0; // sticker_texture
constexpr uint32_t kSuspend = 0xCE0CA665; // Suspend
constexpr uint32_t kInCleanup = 0x7B738580; // incleanup (letter case unknown)
constexpr uint32_t kSkaterInit = 0xE740F458; // SkaterInit
constexpr uint32_t kProjectileproximity = 0x3618B878; // projectileproximity
constexpr uint32_t kPedLogic = 0x6EFDDFB8; // PedLogic
constexpr uint32_t kShot = 0x54F87744; // Shot
constexpr uint32_t kPhysicsProjectileTargetingDist = 0x39186247; // Physics_Projectile_Targeting_Dist
constexpr uint32_t kChooseProjectileTargetBone = 0x5DB74F79; // choose_projectile_target_bone
constexpr uint32_t kLaunchProjectile = 0x86919CB6; // LaunchProjectile
constexpr uint32_t kOBJECTTYPESKATER = 0xBAF9B341; // OBJECT_TYPE_SKATER
constexpr uint32_t kSkater = 0x5B8AB877; // Skater
constexpr uint32_t kSkater2 = 0x06ED3FA7; // Skater2
constexpr uint32_t kSpecialItem = 0xF27A294D; // SpecialItem
constexpr uint32_t kInput = 0x27D7CD28; // Input
constexpr uint32_t kSkaterScore = 0xD262802F; // SkaterScore
constexpr uint32_t kSkaterMatrixQueries = 0xBF518E27; // SkaterMatrixQueries
constexpr uint32_t kTrickName = 0x270F56E1; // Trick
constexpr uint32_t kSkaterRotate = 0x52D61A0D; // SkaterRotate
constexpr uint32_t kSkaterGap = 0x11B4C67F; // SkaterGap
constexpr uint32_t kSkaterAdjustPhysics = 0x9F9CC949; // SkaterAdjustPhysics
constexpr uint32_t kTrigger = 0xE594F0A2; // Trigger
constexpr uint32_t kSkaterFinalizePhysics = 0x9B373E58; // SkaterFinalizePhysics
constexpr uint32_t kSkaterCleanupState = 0xD8CC3428; // SkaterCleanupState
constexpr uint32_t kSkaterLocalNetLogic = 0x7CD3E6D5; // SkaterLocalNetLogic
constexpr uint32_t kSkaterEndRun = 0xB1D5A3E1; // SkaterEndRun
constexpr uint32_t kSTATSManager = 0x4FF8DAC6; // STATSManager
constexpr uint32_t kMovableContact = 0x408D2AA9; // MovableContact
constexpr uint32_t kSkaterRunTimer = 0x8BCE7E66; // SkaterRunTimer
constexpr uint32_t kTaggingControl = 0x380D909A; // TaggingControl
constexpr uint32_t kSkaterFloatingName = 0x125044E2; // SkaterFloatingName
constexpr uint32_t kSkaterStateHistory = 0x18223FD6; // SkaterStateHistory
constexpr uint32_t kUniqueID = 0xA2B033FD; // UniqueID
constexpr uint32_t kPagerMessage = 0xD1515392; // PagerMessage
constexpr uint32_t kNewScript = 0x91D0D784; // NewScript
constexpr uint32_t kOnGroundAI = 0x1DDE4804; // OnGroundAI
constexpr uint32_t kSwitchOnGroundAI = 0x93A4E402; // Switch_OnGroundAI
constexpr uint32_t kKillSkater = 0x1B572ABF; // KillSkater
constexpr uint32_t kPrefix = 0x6C4E7971; // Prefix
constexpr uint32_t kNode = 0x7A8017BA; // Node
constexpr uint32_t kOff = 0xD443A2BC; // Off
constexpr uint32_t kAddSkaterToWorld = 0x0C09C1B7; // AddSkaterToWorld
constexpr uint32_t kRemoveSkaterFromWorld = 0xBCE85494; // RemoveSkaterFromWorld
constexpr uint32_t kSkaterIsNamed = 0x1C5612EE; // SkaterIsNamed
constexpr uint32_t kShouldMongo = 0x3E8EB713; // ShouldMongo
constexpr uint32_t kMongoWhenSwitch = 0x0CDBA4F9; // mongo_when_switch
constexpr uint32_t kAlwaysMongo = 0x16C6DF0B; // always_mongo
constexpr uint32_t kLaunchDirtyProjectile = 0x3F06191C; // LaunchDirtyProjectile
constexpr uint32_t kSparksOffName = 0x5436A335; // SparksOff
constexpr uint32_t kSparksOnName = 0xE62F94A2; // SparksOn
constexpr uint32_t kSetCustomRestart = 0x42832375; // SetCustomRestart
constexpr uint32_t kSet = 0x19EBDA23; // Set
constexpr uint32_t kClear = 0x1A4E0EF9; // Clear
constexpr uint32_t kSkipToCustomRestart = 0x5A3C19E9; // SkipToCustomRestart
constexpr uint32_t kIsLocalSkater = 0x7015BC33; // IsLocalSkater
constexpr uint32_t kResetRigidBodyCollisionRadiusBoost = 0x72AECFC0; // ResetRigidBodyCollisionRadiusBoost
constexpr uint32_t kGetCameraId = 0x97C8285A; // GetCameraId
constexpr uint32_t kCameraId = 0x7A039889; // CameraId
constexpr uint32_t kGetStat = 0x7915AA31; // GetStat
constexpr uint32_t kStat = 0xDF4700DE; // Stat
constexpr uint32_t kStatValue = 0x8EAF7ADD; // stat_value
constexpr uint32_t kGetScriptedStat = 0x9ABE8A21; // GetScriptedStat
constexpr uint32_t kMoveToRandomRestart = 0xBF450BAB; // MoveToRandomRestart
constexpr uint32_t kPlaceBeforeCamera = 0xD38E8B6C; // PlaceBeforeCamera
constexpr uint32_t kDie = 0xC6870028; // Die
constexpr uint32_t kGetSkaterNumber = 0xF4C52F72; // GetSkaterNumber
constexpr uint32_t kSkaternumber = 0xF3CF5755; // skaternumber
constexpr uint32_t kTriggerScript = 0x2CA8A299; // TriggerScript
constexpr uint32_t kNetctf = 0x6C5FF266; // netctf
constexpr uint32_t kNetking = 0x6EF8FDA0; // netking
constexpr uint32_t kKing = 0x5D32129C; // King
constexpr uint32_t kDestroySkaterParticles = 0xF429E0AC; // DestroySkaterParticles
// NOT ORIGINAL grouping - rider.cpp's state (the original's globals). Made on first use, never destroyed
// (rider_state()).
struct RiderState {
    // (4 floats each: the light's direction while in the air - straight down - and on the ground. They were declared
    // as single floats and written as four: the writes ran on over whatever followed - the Direct3D device pointer, once.)
    maths::Vec4 skater_down_light_colour_{};
    maths::Vec4 skater_light_colour_{};
    uint32_t skater_lights_made_{};
};

RiderState& rider_state()
{
    static core::NoDestroy<RiderState> state;
    return state.get();
}

// Names.
constexpr uint32_t kIsPro = 0xD82F8AC8;               // is_pro
constexpr uint32_t kIsCustom = 0x5A3264BB;            // (a flag: not a pro)
constexpr uint32_t kIsMale = 0x3F813177;              // is_male
constexpr uint32_t kIsFemale = 0xE5C2F84C;            // is_female
constexpr uint32_t kIsNamed = 0xC2E3D008;             // is_named
constexpr uint32_t kStance = 0x7D02BCC3;              // stance
constexpr uint32_t kRegular = 0xB58EFC2B;             // regular
constexpr uint32_t kGoofy = 0x287FCD4E;               // goofy
constexpr uint32_t kStyle = 0xCC424795;               // style
constexpr uint32_t kSparksOn = 0x0DC85C7B;            // SkaterParticles_SparksOn (a script)
constexpr uint32_t kSparksOff = 0xABD22E8D;           // SkaterParticles_SparksOff (a script)
constexpr uint32_t kTricks = 0x1E26FD3E;              // tricks
constexpr uint32_t kOther = 0x26A7CADF;               // other
constexpr uint32_t kAnimations = 0x6B19FC8F;          // Animations
constexpr uint32_t kRotations = 0x2E628EE6;           // rotations
constexpr uint32_t kKillLoadPremadeCats = 0x33FE5817; // kill_load_premade_cats (a script)
constexpr uint32_t kList44b66a9f = 0x44B66A9F;
constexpr uint32_t kChapterStatus = 0xDFA17D68;     // CHAPTER_STATUS
constexpr uint32_t kChapterIntros = 0x2C6437CD;     // CHAPTER_INTROS_VIEWED
constexpr uint32_t kChapterOuttros = 0x4B1ED398;    // CHAPTER_OUTTROS_VIEWED
constexpr uint32_t kSaveChapterStatus = 0x8E8EE0C0; // (the save's names for them)
constexpr uint32_t kSaveChapterIntros = 0xD5F46FDA;
constexpr uint32_t kSaveChapterOuttros = 0x72FC1AE3;
constexpr uint32_t kSkaterCorePhysics = 0x5BD63B29;    // SkaterCorePhysics (a component)
constexpr uint32_t kSkaterPhysicsControl = 0xD9151D4D; // SkaterPhysicsControl (a component)
constexpr uint32_t kSkaterPhysics = 0x8CBC89DD;        // skater_physics
constexpr uint32_t kChainsawPhysics = 0xD3D12913;      // chainsaw_physics
constexpr uint32_t kSkaterDefaultStats = 0x6357D57C;   // Skater_Default_Stats
constexpr uint32_t kCheatStats13 = 0x6B0F24BC;         // CHEAT_STATS_13
constexpr uint32_t kSwitch = 0x9016B4E7;               // switch
constexpr uint32_t kDiff = 0xBA8FB854;                 // diff
constexpr uint32_t kLimit = 0x8069179F;                // limit
constexpr uint32_t kIsClassic = 0x5A2C589A;            // is_classic
constexpr uint32_t kSkaterCam0 = 0x967C138C;           // skatercam0
constexpr uint32_t kSkaterCam1 = 0xE17B231A;           // skatercam1
constexpr uint32_t kModelModuleName = 0x286A8D26;      // model (a component)
constexpr uint32_t kPauseValue = 0x9816E1E3;           // (a physics integer: pausing freezes the whole game)

bool rule_5a_1180() { return static_cast<uint8_t>(commands::cfunc_in_split_screen_game(nullptr, nullptr)) != 0; }

} // namespace

// 0x0057FE30
void Rider::set_stat_override(int index, float value)
{
    if (rule_5a_1180()) {
        if (value > kStatCeiling)
            value = kStatCeiling;
        else if (value < 0.0f)
            value = 0.0f;
    }
    core::at(stat_overrides_, index) = value;
}

// 0x0057FEB0 / 0x0057FEC0
void* Rider::obtain_active_lens() const { return render::fetch_live_camera(view_slot_); }
int Rider::get_skater_number() const { return skater_number_; }

// 0x0057FED0
bool Rider::matches_params(script::Struct* params) const
{
    if (params->has_bare_flag(kIsPro) && !pro_)
        return false;
    if (params->has_bare_flag(kIsCustom) && pro_)
        return false;
    if (params->has_bare_flag(kIsMale) && !male_)
        return false;
    if (params->has_bare_flag(kIsFemale) && male_)
        return false;
    uint32_t value = 0;
    if (params->read_hash_value(kIsNamed, &value, 0) && value != label_)
        return false;
    if (params->read_hash_value(kStance, &value, 0) && value != (goofy_ ? kGoofy : kRegular))
        return false;
    if (params->read_hash_value(kStyle, &value, 0) && value != style_)
        return false;
    return true;
}

// 0x0057FFE0 / 0x00580000
void Rider::sparks_on()
{
    script::launch_script(kSparksOn, nullptr, this, false, nullptr);
    sparks_ = true;
}
void Rider::sparks_off()
{
    if (!sparks_)
        return;
    script::launch_script(kSparksOff, nullptr, this, false, nullptr);
    sparks_ = false;
}

// 0x00580030
maths::Mat4* Rider::get_draw_matrix() { return &core_physics_->display_matrix(); }

// 0x00580040 / 0x00580050
memory::ListNode* Rider::get_skater_link() { return &skater_link_; }
bool Rider::get_flag_2c_4() const { return flag_2c_4_; }

// 0x00580060
void Rider::load_cat_info(script::Struct* params)
{
    debug::printf("LoadCATInfo was called\n");
    script::Array* tricks = nullptr;
    params->fetch_list(kTricks, &tricks, 0);
    for (int i = 0; i < kCatTrickCount; ++i) {
        auto* trick = tricks->struct_at(i);
        const goals::CreateATrick* info = core::at(trick_slots_, i).get();
        script::Struct* other = nullptr;
        trick->read_sub_struct(kOther, &other, 1);
        info->other_params()->append_structure_if_any(other);
        script::Array* animations = nullptr;
        trick->fetch_list(kAnimations, &animations, 1);
        script::duplicate_array(info->animations(), animations);
        script::Array* rotations = nullptr;
        trick->fetch_list(kRotations, &rotations, 1);
        script::duplicate_array(info->spin_list(), rotations);
    }
    script::launch_script(kKillLoadPremadeCats, nullptr, nullptr, false, nullptr);
}

// 0x00580150
void Rider::load_list_44b_66a_9f(script::Struct* params)
{
    static_cast<AttributeTracker*>(stats_)->reapply_from_data(nullptr);
    script::Array* entries = nullptr;
    params->fetch_list(kList44b66a9f, &entries, 0);
    for (int i = 0; i < kGoalStatusCount; ++i)
        static_cast<AttributeTracker*>(stats_)->set_goal_status(i, static_cast<int>(entries->fetch_part(i)));
}

// 0x005801B0
void Rider::write_chapter_info(script::Struct* save)
{
    save->append_array_value(kSaveChapterStatus, script::fetch_list(kChapterStatus));
    save->append_array_value(kSaveChapterIntros, script::fetch_list(kChapterIntros));
    save->append_array_value(kSaveChapterOuttros, script::fetch_list(kChapterOuttros));
}

// 0x00580210
void Rider::read_chapter_info(script::Struct* save)
{
    script::Array* array = nullptr;
    if (save->fetch_list(kSaveChapterStatus, &array, 0))
        script::duplicate_array(script::fetch_list(kChapterStatus), array);
    if (save->fetch_list(kSaveChapterIntros, &array, 0))
        script::duplicate_array(script::fetch_list(kChapterIntros), array);
    if (save->fetch_list(kSaveChapterOuttros, &array, 0))
        script::duplicate_array(script::fetch_list(kChapterOuttros), array);
}

// 0x005802C0
void Rider::set_42b_d60(float value) { set_rigid_body_pusher_reach(this, value); }

// 0x005802E0
script::Struct* get_physics_structure(GameEntity* skater)
{
    if (!skater || !skater->is_local_skater())
        return script::read_sub_struct(kSkaterPhysics);
    auto* core = core::down_cast<skater::SkaterPhysicsModule>(skater->fetch_module(kSkaterCorePhysics));
    // Not original - on THAW's bike, THAW's bicycle_physics (thaw_bicycle_physics: game/rider/thaw_bmx.h), as
    // THAW's own get_physics_structure (0x0058ED90) picks it.
    // Original behaviour: the vehicle's, the chainsaw's or the skater's below.
    if (skater::thaw_bmx::is_switched_on()) {
        if (script::Struct* bike = skater::thaw_bmx::physics_structure(core))
            return bike;
    }
    if (core->on_vehicle()) {
        switch (core->vehicle()) { // what it rides
        case kSegway:
            return script::read_sub_struct(kSegwayPhysics); // Segway: segway_physics
        case kMechbull:
            return script::read_sub_struct(kMechbullPhysics); // mechbull: mechbull_physics
        case kMiniKart:
            return script::read_sub_struct(kMinikartPhysics); // minikart: minikart_physics
        case kWheelchair:
            return script::read_sub_struct(kWheelchairPhysics); // Wheelchair: wheelchair_physics
        case kTricycle:
            return script::read_sub_struct(kTricyclePhysics); // tricycle: tricycle_physics
        case kGurney:
            return script::read_sub_struct(kGurneyPhysics); // gurney: gurney_physics
        case kChainsaw:
            return script::read_sub_struct(kChainsawPhysics); // chainsaw
        default:
            return script::read_sub_struct(kSkaterPhysics); // (slalom, shoppingcart, anything else)
        }
    }
    if (core->motoskateboard())
        return script::read_sub_struct(kChainsawPhysics);
    return script::read_sub_struct(kSkaterPhysics);
}

// 0x00580400
float read_tuning_float(checksums::Name tuning_name, int assert, GameEntity* skater)
{
    const uint32_t name = tuning_name.value();
    script::Struct* physics = get_physics_structure(skater);
    float value = 0.0f;
    if (physics->fetch_float(name, &value, 0))
        return value;
    (void)assert;
    return script::fetch_float(name);
}

// 0x00580440
int read_tuning_int(checksums::Name tuning_name, int assert, GameEntity* skater)
{
    const uint32_t name = tuning_name.value();
    script::Struct* physics = get_physics_structure(skater);
    int value = 0;
    if (physics->fetch_int_value(name, &value, 0))
        return value;
    (void)assert;
    return script::fetch_int_value(name);
}

// 0x00580480
modules::PointTally* Rider::get_points_total() { return score_holder_->points_held(); }

// 0x00580490
float Rider::fetch_tally(int index)
{
    float stat = modules::Skate::sole()->forced_stat(); // (forced by the skate module unless 0)
    if (stat == 0.0f) {
        uint32_t bits = 0;
        std::memcpy(&bits, &core::at(stat_overrides_, index), 4);
        stat = bits == kUnsetStatBits ? script::fetch_float(kSkaterDefaultStats) : core::at(stat_overrides_, index);
    }
    // Online: the server's rules for this player.
    if (online::online_hub()->in_net_game()) {
        auto* player =
            online::online_hub()->find_player_slot_by_obj(static_cast<uint16_t>(ident()));
        if (player && player->holds_crown())
            stat = 1.0f;
        else if (player && player->is_carrying_flag())
            stat = kCarryingFlagStat;
        else
            stat = kOutOfGameStat; // (no player, or neither)
    } else if (rule_5a_1180()) {
        auto* player =
            online::online_hub()->find_player_slot_by_obj(static_cast<uint16_t>(ident()));
        if (player && player->holds_crown())
            stat = 1.0f;
    }
    RiderStoryProgress* cheats = modules::Skate::sole()->career().get();
    float result = cheats->is_code_on(static_cast<uint32_t>(script::fetch_int_value(kCheatStats13))) ? kCheatStat : stat;
    if (score_holder_->points_held()->special_active())
        result = static_cast<float>(result + kSpecialStatBonus);
    return result;
}

// 0x005805B0
float Rider::get_scaled_value(script::Struct* params)
{
    script::Pair range, switch_range;
    params->fetch_pair(0u, &range, false);
    int stat_index = 0;
    const double stat = params->fetch_int_value(0u, &stat_index, 0) ? fetch_tally(stat_index) : 10.0f;
    const float low = range.x(), high = range.y();
    auto result = static_cast<float>((static_cast<double>(high) - low) * stat * kStatToFraction + low);
    // Riding switch: times the switch pair, by the switch stat (0-1).
    const bool goofy = core::down_cast<Rider>(core_physics_->entity())->goofy_;
    const uint8_t flipped = core_physics_->state()->flag(PoseFlag::kFlipped).on;
    const bool riding = goofy ? flipped != 0 : flipped == 0;
    if (riding && params->fetch_pair(kSwitch, &switch_range, false)) {
        const double s = fetch_tally(6);
        double scale = (static_cast<double>(switch_range.y()) - switch_range.x()) * s * kStatToFraction + switch_range.x();
        if (scale < 0.0)
            scale = 0.0;
        else if (scale > 1.0)
            scale = 1.0;
        result = static_cast<float>(scale * result);
    }
    // The game's difficulty (one more in classic mode): times the diff pair's first (0) or second (2).
    goals::GoalManager* mode = modules::Skate::sole()->get_objective_board();
    int difficulty = mode->get_skill_tier(false);
    if (goals::game_mode()->is_true(kIsClassic))
        ++difficulty;
    if (difficulty != 1) {
        script::Pair diff;
        if (params->fetch_pair(kDiff, &diff, false)) {
            if (difficulty == 0)
                result = static_cast<float>(static_cast<double>(diff.x()) * result);
            else if (difficulty == 2)
                result = static_cast<float>(static_cast<double>(diff.y()) * result);
        }
    }
    // The limit: a maximum for a rising range, a minimum otherwise.
    float limit = 0.0f;
    if (!params->fetch_float(kLimit, &limit, 0))
        return result;
    if (low < high)
        return result > limit ? limit : result;
    return result < limit ? limit : result;
}

// 0x00580790
float Rider::get_scaled_physics(uint32_t name)
{
    script::Struct* params = nullptr;
    Entity* object = object_hub_slot()->look_up_entity(ident());
    script::Struct* physics = get_physics_structure(core::down_cast<GameEntity>(object));
    if (!physics->read_sub_struct(name, &params, 0))
        params = script::read_sub_struct(name);
    return get_scaled_value(params);
}

// 0x005807F0
float Rider::get_scaled_param(uint32_t name, script::Struct* params)
{
    script::Struct* structure = nullptr;
    params->read_sub_struct(name, &structure, 0);
    return get_scaled_value(structure);
}

// 0x00580830
void Rider::move_to_random_restart()
{
    modules::Skate* skate = modules::Skate::sole();
    uint32_t restart = 0;
    int tries = 0;
    for (bool again = true; again; again = restart == last_restart_ && tries < kMaxRestartTries) {
        restart = skate->warp_to_restart(static_cast<uint32_t>(maths::rnd(kRestartCandidates)));
        ++tries;
    }
    last_restart_ = restart;
    if (restart)
        jump_to_spawn({.node = restart, .walking = 0});
}

// 0x00580880
Entity* Rider::get_skater_camera() const
{
    return object_hub_slot()->look_up_entity(view_slot_ == 1 ? kSkaterCam1 : kSkaterCam0);
}

// 0x005808A0
void Rider::set_pause_state(int state)
{
    void* front_end = modules::front_end_instance().get();
    if (pause_state_ == state || !fetch_module(kSkaterPhysicsControl))
        return;
    pause_state_ = state;
    auto pause_game = [front_end](bool pause) { static_cast<modules::FrontEnd*>(front_end)->get_pause_game(pause); };
    auto control = [](void* physics_control, int paused) {
        static_cast<RiderBoardPhysics*>(physics_control)->set_frozen(paused != 0);
    };
    switch (state) {
    case 0:
        if (read_tuning_int(checksums::Name{kPauseValue}, 0, nullptr)) {
            debug::printf("UnPausing game\n");
            pause_game(false);
        }
        break;
    case 1:
        if (read_tuning_int(checksums::Name{kPauseValue}, 0, nullptr)) {
            debug::printf("Freezing game\n");
            pause_game(true);
        } else if (ObjectModule* physics_control = fetch_module(kSkaterPhysicsControl);
                   core::down_cast<RiderBoardPhysics>(physics_control)->is_halted() != 1) {
            control(physics_control, 1);
        }
        break;
    case 2:
        if (!read_tuning_int(checksums::Name{kPauseValue}, 0, nullptr)) {
            ObjectModule* physics_control = fetch_module(kSkaterPhysicsControl);
            if (core::down_cast<RiderBoardPhysics>(physics_control)->is_halted())
                control(physics_control, 0);
        }
        break;
    default:
        break;
    }
}

// 0x005809C0
void Rider::update_shadow_light()
{
    uint32_t& made = rider_state().skater_lights_made_;
    maths::Vec4& light = rider_state().skater_light_colour_;
    maths::Vec4& down = rider_state().skater_down_light_colour_;
    if (!(made & 1)) {
        made |= 1;
        light.x = kLightHorizontal, light.y = -kLightHorizontal, light.z = kLightDepth, light.w = 1.0f;
    }
    if (!(made & 2)) {
        made |= 2;
        down.x = 0.0f, down.y = -1.0f, down.z = 0.0f, down.w = 1.0f;
    }
    // The model's light, reversed; kept at least 0.65 below the horizon.
    if (fetch_module(kModelModuleName) &&
        core::down_cast<MeshInstanceModule>(fetch_module(kModelModuleName))->mesh_instance().get()) {
        // (the model's +0x2C - read through the folded getter 0x00532EB0 - and its virtual slot 5 - through the jump
        // 0x00456B30)
        void* lights = core::down_cast<MeshInstanceModule>(fetch_module(kModelModuleName))->mesh_instance()->lights_ptr().get();
        if (lights) {
            const maths::Vec4& aim = *static_cast<render::ModelLights*>(lights)->read_light_aim(0);
            const double x = aim.x * -1.0;
            const double y = aim.y * -1.0;
            const double z = aim.z * -1.0;
            const double w = aim.w * -1.0;
            light.x = static_cast<float>(x);
            light.y = static_cast<float>(y);
            const auto z_stored = static_cast<float>(z); // (the original reads z back as stored)
            light.z = z_stored;
            light.w = static_cast<float>(w);
            if (y > -kLightMinDrop) {
                const double flat = static_cast<double>(z_stored) * z_stored + x * x;
                const double scale = flat > 0.0 ? std::sqrt(0.5775f / flat) : 0.0;
                light.y = -kLightMinDrop;
                light.x = static_cast<float>(x * scale);
                light.z = static_cast<float>(z_stored * scale);
            }
        }
    }
    // In the air (or not on the ground): fade towards straight down; else back.
    bool fading = false;
    const RiderBoardPhysics* control = physics_control_;
    if (control && !control->pose()->in_vehicle() && control->pose()->physics_mode() == objects::RiderPoseState::PhysicsMode::kSkating)
        fading = core_physics_->state()->cur_phase() == 0;
    else if (!walk_)
        fading = true;
    else
        fading = static_cast<uint32_t>(walk_->get_state()) != 1;
    if (fading)
        shadow_fade_ = shadow_fade_ > 0 ? shadow_fade_ - 1 : 0;
    else
        shadow_fade_ = shadow_fade_ < kShadowFadeFrames ? shadow_fade_ + 1 : kShadowFadeFrames;
    maths::Vec4 direction;
    walk::get_lerp(&direction, &light, &down,
                   static_cast<float>(shadow_fade_) * kShadowFadeStep);
    static_cast<GroundShadowModule*>(shadow_)->set_light_direction(&direction);
}

namespace {

constexpr std::array<uint32_t, 10> kStatNames = {{0x439F4704, 0xAF895B3F, 0x9B65D7B8, 0xF0D90109, 0xEDF5DB70,
                                     0x6DCB497C, 0x9016B4E7, 0xF73A13E3, 0xAE798769, 0xB1FC0722}};
constexpr uint32_t kPushStyle = 0xC15DBF86;           // pushstyle
constexpr uint32_t kSkateStyle = 0x76FD33E8;          // skatestyle
constexpr uint32_t kFirstName = 0x562E3ECD;           // first_name
constexpr uint32_t kSkaterFlipAndRotate = 0x488AF07A; // SkaterFlipAndRotate (a component)

int get_clock() { return frame_clock::game_clock_elapsed() + clock_offset(); }

// The score's two balance displays off (0x00563370 / 0x00563390).
void balance_displays_off(void* score)
{
    static_cast<modules::PointTally*>(score)->set_teeter_gauge(false, 0.0f);
    static_cast<modules::PointTally*>(score)->set_wheelie_gauge(false, 0.0f);
}

// An input flag (a byte and the clock when it last changed) set to `on` if it isn't already.

} // namespace

// 0x00580BE0
void Rider::set_stats_from_profile(RiderIdentity* profile)
{
    RiderStoryProgress* classic_stats = modules::Skate::sole()->career().get();
    if (goals::game_mode()->is_true(kIsClassic)) {
        for (int i = 0; i < kStatCount; ++i)
            set_stat_override(i, static_cast<float>(classic_stats->get_info_integer(core::at(kStatNames, i))));
        return;
    }
    for (int i = 0; i < kStatCount; ++i)
        set_stat_override(i, static_cast<float>(profile->fetch_tally(core::at(kStatNames, i))));
}

// 0x00580EB0
void Rider::update_from_profile(RiderIdentity* profile)
{
    memory::Hub* manager = memory::Hub::get_pull();
    manager->enter_heap_scope(core::at(manager->skater_heaps(), skater_number_));
    auto checksum = [profile](uint32_t name) { return profile->get_name_key_field(name); };
    stroke_kind_ = checksum(kPushStyle);
    goofy_ = checksum(kStance) == kGoofy;
    pro_ = profile->is_pro();
    shown_name_ = profile->fetch_shown_name();
    script::Struct* info = profile->details_params();
    style_ = 0;
    info->fetch_int_value(kIsMale, &male_, 1);
    info->read_hash_value(kSkateStyle, &style_, 0);
    const char* first_name = nullptr;
    info->fetch_text(kFirstName, &first_name, 1);
    {
        // (copied inline, unchecked in the original: a name too long for the array ends the game with a report here)
        const std::string_view first(first_name);
        for (size_t i = 0; i <= first.size(); ++i)
            core::at(given_name_, i) = i < first.size() ? core::at(first, i) : '\0';
    }
    label_ = profile->fetch_title();
    if (is_local_skater()) {
        core_physics_->state()->set_flag(PoseFlag::kFlipped, goofy_ ? 0 : 1);
        core::down_cast<RiderSpinAndFlip>(fetch_module(kSkaterFlipAndRotate))->apply_flip_to_anims();
    }
    memory::Hub::get_pull()->leave_heap_scope();
}

// 0x00580FF0
void Rider::reset_input()
{
    score_holder_->reset_score();
    core::down_cast<RiderTeeterMoves>(fetch_module(kSkaterBalanceTrick))->clear_best_times();
    set_up_network();
    for (int i = 0; i < kLoopPoseFlags; ++i) {
        core_physics_->state()->set_flag(static_cast<PoseFlag>(i), 1);
        if (i == kSlideFlag)
            core_physics_->sound()->set_sliding(1);
        core_physics_->state()->set_flag(static_cast<PoseFlag>(i), 0);
        if (i == kSlideFlag)
            core_physics_->sound()->set_sliding(0);
    }
    pad_input_->pad_primary().get_clear();
    script::launch_script(kInitSkaterParticles, nullptr, this, false, nullptr);
}

// 0x005810E0
void Rider::reset_state()
{
    auto* physics_control = core::down_cast<RiderBoardPhysics>(fetch_module(kSkaterPhysicsControl));
    if (physics_control->is_halted())
        physics_control->set_frozen(false);
    core::down_cast<RiderSpinAndFlip>(fetch_module(kSkaterFlipAndRotate))->clear_after_flags();
    MoveExecutor* tricks = trick_;
    tricks->clear_extra_tricks();
    tricks->clear_block1238();
    tricks->clear_block1280();
    tricks->clear_event_buffer();
    tricks->clear_queue();
    tricks->reset_trick_objects();
    core::down_cast<RiderPoseState>(fetch_module(kSkaterState))->clear_doing_trick();
    model_component_->set_flip(false);
    balance_displays_off(score_holder_->points_held());
    if (vibration_)
        vibration_->stop_all();
    shadow_->change_place(&at_point());
    shadow_->set_normal(&core::at(basis_frame().row, 1));
    core::down_cast<RiderStanceDisplay>(fetch_module(kSkaterStancePanel))->set_shown(-1);
}

// 0x005811E0
void Rider::set_up_network()
{
    const net::AppHost* client = online::online_hub()->fetch_patron(skater_number_);
    auto* flip_and_rotate = core::down_cast<RiderSpinAndFlip>(fetch_module(kSkaterFlipAndRotate));
    flip_and_rotate->apply_board_turned(ident(), false, client->clock(), is_local_skater() != 0);
    flip_and_rotate->apply_flip_to_anims();
    model_component_->conceal_mesh(kBoard, false, is_local_skater() != 0);
    anim_blender_->reset_channels();
}

// 0x00581260
void Rider::show_board_element(uint32_t type)
{
    uint32_t name = 0;
    switch (type) {
    case kFrontLogo:
        name = kCustomFrontlogoParams;
        break;
    case kDeckGraphic:
        name = kCustomDecklogoParams;
        break;
    case kHatLogo:
        name = kCustomHatlogoParams;
        break;
    case kBackLogo:
        name = kCustomBacklogoParams;
        break;
    default:
        return;
    }
    script::Struct* params = script::read_sub_struct(name);
    if (!params)
        return;
    memory::Hub::get_pull()->enter_heap_scope(memory::Hub::get_pull()->top_down_heap());
    uint32_t model_swap = 0, model_swap2 = 0;
    float width = 0.0f, height = 0.0f;
    const char* texture = nullptr;
    params->read_hash_value(kIn, &model_swap, 1);
    params->fetch_float(kXScale, &width, 1);
    params->fetch_float(kYScale, &height, 1);
    params->read_hash_value(kWith, &model_swap2, 1);
    params->fetch_text(kTempTexture, &texture, 1);
    auto new_element = [](const char* text) {
        return static_cast<render::TextureSet*>(render::engine_parts().game_texture_dict())->ingest_skin(text, 0, 0, true);
    };
    // (the textures' calls go through the folded jumps 0x0048D060, 0x0048DE80, 0x0048DED0, 0x0048DEB0, 0x0049F360,
    // 0x0049F420, 0x0048DE30 and 0x0048E4C0)
    auto reset = [](render::BitmapTexture* element) { element->begin_edit(false, false); };
    render::BitmapTexture* element = new_element(texture);
    reset(element);
    render::BitmapTexture* parent =
        core::down_cast<objects::TaggingComponent>(fetch_module(kTagging))->graffiti();
    reset(parent);
    element->resize_from(parent, 1);
    element->get_border(0); // (black)
    element->get_zoom(width, height);
    int flag = 0;
    if (params->fetch_int_value(kRotate, &flag, 0))
        element->spin_by(kQuarterTurnRadians, {.x = -1.0f, .y = -1.0f});
    bool second = false;
    render::BitmapTexture* overlay = nullptr;
    int unused = 0;
    if (params->fetch_int_value(kIsDeck, &unused, 0)) {
        second = true;
        const char* overlay_texture = nullptr;
        params->fetch_text(kBaseTexture, &overlay_texture, 1);
        overlay = new_element(overlay_texture);
        reset(overlay);
        overlay->get_overlay(element, 1);
        overlay->write_back(1, nullptr);
    }
    element->write_back(1, nullptr);
    const char* model = nullptr;
    params->fetch_text(kLogoTexture, &model, 1);
    core::down_cast<MeshInstanceModule>(fetch_module(kModelModuleName))->swap_texture(model_swap, model, model_swap2);
    if (second)
        static_cast<render::TextureSet*>(render::engine_parts().game_texture_dict())->unload_texture(overlay);
    static_cast<render::TextureSet*>(render::engine_parts().game_texture_dict())->unload_texture(element);
    parent->end_edit();
    memory::Hub::get_pull()->leave_heap_scope();
}

// 0x005814D0
void Rider::reset_27c()
{
    if (!is_local_skater())
        core::down_cast<RiderRemoteSync>(fetch_module(kSkaterNonLocalNetLogic))->reset_playback();
    state_history_->state_count() = 0;
}

// 0x00581510
void Rider::conceal_skater()
{
    if (is_local_skater())
        core::down_cast<RiderRollingAudio>(fetch_module(kSkaterLoopingSound))->stop_sound();
    static_cast<GroundShadowModule*>(shadow_)->destroy_shadow();
    conceal(true);
    flag_2c_4_ = false;
}

// 0x00581550
void Rider::show_skater()
{
    modules::Skate::sole()->set_skater_shadow(this);
    conceal(false);
    flag_2c_4_ = true;
}

// 0x00581580
const char* Rider::fetch_shown_name()
{
    if (online::online_hub()->in_net_game())
        return online::online_hub()->find_player_slot_by_obj(static_cast<uint16_t>(ident()))->label().data();
    return shown_name_.c_str();
}

// 0x005815C0
void Rider::glide_to_node(uint32_t name)
{
    core::at(basis_frame().row, 0) = {.x = 1.0f, .y = 0.0f, .z = 0.0f, .w = 0.0f};
    core::at(basis_frame().row, 1) = {.x = 0.0f, .y = 1.0f, .z = 0.0f, .w = 0.0f};
    core::at(basis_frame().row, 2) = {.x = 0.0f, .y = 0.0f, .z = 1.0f, .w = 0.0f};
    core::at(basis_frame().row, 3) = {.x = 0.0f, .y = 0.0f, .z = 0.0f, .w = 1.0f};
    if (!name)
        return;
    script::Struct* node = level_nodes::fetch_vertex(name, !online::online_hub()->in_net_game());
    if (!node)
        return;
    level_nodes::fetch_place(name, &at_point());
    old_pos() = at_point();
    core_physics_->safe_pos() = at_point();
    mark_jumped_ahead(true);
    maths::Vec4 angles;
    level_nodes::read_turn_angles(node, &angles);
    basis_frame().init_from_angles(angles);
    auto* object = core_physics_->entity();
    core_physics_->display_matrix() = object->basis_frame();
    core_physics_->set_floor_normal(core::at(basis_frame().row, 1));
    core_physics_->old_normal() = core_physics_->floor_normal();
    core_physics_->drawn_normal() = core_physics_->old_normal();
    static_cast<GroundShadowModule*>(shadow_)->change_place(&at_point());
    static_cast<GroundShadowModule*>(shadow_)->set_normal(&core::at(basis_frame().row, 1));
    display_matrix() = core_physics_->display_matrix();
}

// 0x00581760
void Rider::get_freeze()
{
    if (is_halted_ || !is_local_skater())
        return;
    is_halted_ = true;
    composite_flags() |= 1;
    balance_displays_off(score_holder_->points_held());
    script::launch_script(kPauseRunTimer, nullptr, nullptr, false, nullptr);
    if (const PadInputModule* input = pad_input_)
        input->handler()->dev()->pause_actuators();
    sound::get_audio_mixer()->update_voice_volumes();
}

// 0x005817F0
void Rider::unpause()
{
    if (!is_halted_ || !is_local_skater())
        return;
    is_halted_ = false;
    composite_flags() &= ~1u;
    if (const PadInputModule* input = pad_input_)
        input->handler()->dev()->unpause_actuators();
    if (auto* component = core::down_cast<RiderRollingAudio>(fetch_module(kSkaterLoopingSound)))
        component->set_force_update(1);
}

namespace {

constexpr uint32_t kPartHash = 0xB6015EA8;     // component
constexpr uint32_t kNameChecksum = 0xA1DC81F9; // name
constexpr uint32_t kCustomSkater = 0x0A7BE964; // custom

// A component made from a structure the caller fills (CreateComponentFromStructure, vtable +0x24), the structure
// freed after.
template <typename Fill>
void add_component_from_structure(Rider* skater, Fill fill)
{
    gsl::owner<script::Struct*> memory = script::spawn_param_bag();
    script::Struct* params = memory ? memory::construct_in<script::Struct>(memory) : nullptr;
    fill(params);
    skater->create_component_from_structure(params, nullptr);
    if (params) {
        params->get_clear();
        script::delete_struct(params);
    }
}

} // namespace

// 0x00581850
void Rider::prepare_all(RiderIdentity* profile)
{
    const modules::Skate* skate = modules::Skate::sole();
    memory::Hub* manager = memory::Hub::get_pull();
    if (is_local_skater()) {
        // Its camera: found, or made from skatercam_composite_structure.
        manager->enter_heap_scope(manager->skater_info_heap());
        const uint32_t camera_name = view_slot_ == 1 ? kSkaterCam1 : kSkaterCam0;
        auto* camera = core::down_cast<GameEntity>(object_hub_slot()->look_up_entity(camera_name));
        if (camera) {
            core::down_cast<RiderChaseView>(camera->fetch_module(kSkaterCamera))->bind_rider(this); // skatercamera
            auto* walk_camera = core::down_cast<FootView>(camera->fetch_module(kWalkCamera));     // walkcamera
            walk_camera->bind_rider(this);
            walk_camera->set_instant_frames(3);
        } else {
            gsl::owner<script::Struct*> memory = script::spawn_param_bag();
            script::Struct* params = memory ? memory::construct_in<script::Struct>(memory) : nullptr;
            Expects(params != nullptr);
            params->put_name_key(checksums::Name{kCameraTarget}, ident());
            params->put_name_key(checksums::Name{kNameChecksum}, camera_name);
            camera = object_hub_slot()->spawn_entity_from_node(script::fetch_list("skatercam_composite_structure"),
                                                               params, true);
            if (params) {
                params->get_clear();
                script::delete_struct(params);
            }
        }
        core::down_cast<FootMovement>(fetch_module(kWalk))->choose_camera(camera); // Walk
        commands::make_camera_active(checksums::Name{camera_name}, view_slot_, false);
        camera->object_flags() |= kObjectFlagLocked;
        core::down_cast<RiderChaseView>(camera->fetch_module(kSkaterCamera))->change_regime({.mode = 2, .time = 0.0f});
        memory::Hub::get_pull()->leave_heap_scope();
    }
    // Its script; its anim set (skater_asset_info's skater_skeleton, else thps6_human).
    set_own_script(script::Script::create());
    uint32_t skeleton = kTHPS6Human;
    if (script::Struct* asset_info = script::read_sub_struct(kSkaterAssetInfo))
        asset_info->read_hash_value(kSkaterSkeleton, &skeleton, 0);
    add_motion_components(this);
    add_component_from_structure(this, [skeleton](script::Struct* params) {
        Expects(params != nullptr);
        params->put_name_key(checksums::Name{kPartHash}, kAnimBlender);  // animblender
        params->put_name_key(checksums::Name{kSkeletonName}, skeleton);   // skeletonName
        params->put_name_key(checksums::Name{kAnimEventTableName}, kSkaterAnimEventTable); // AnimEventTableName SkaterAnimEventTable
    });
    anim_blender_ = core::down_cast<AnimBlenderComponent>(fetch_module(kAnimBlender));
    add_component_from_structure(this, [skeleton](script::Struct* params) {
        Expects(params != nullptr);
        params->put_name_key(checksums::Name{kPartHash}, kSkeleton2); // skeleton
        params->put_name_key(checksums::Name{kSkeleton2}, skeleton);
        params->put_int(checksums::Name{kMaxBoneSkipLod}, 0); // max_bone_skip_lod
    });
    static_cast<AnimBlenderComponent*>(anim_blender_)->set_flag_4c(true);
    const bool local = is_local_skater() != 0;
    add_component_from_structure(this, [local](script::Struct* params) {
        Expects(params != nullptr);
        params->put_name_key(checksums::Name{0u}, kUseModelLights); // UseModelLights
        params->put_int(checksums::Name{kMaxSceneLights}, 1);       // max_scene_lights
        params->put_int(checksums::Name{kShadowVolume}, local ? 1 : 0);
        params->put_name_key(checksums::Name{kPartHash}, kModelModuleName);
    });
    model_component_ = core::down_cast<MeshInstanceModule>(fetch_module(kModelModuleName));
    // Its geometry, in its geometry heap, with the asset manager's flag off meanwhile.
    memory::Hub::get_pull()->enter_heap_scope(core::at(memory::Hub::get_pull()->skater_geom_heaps(), skater_number_));
    auto* assets = assets::asset_manager();
    const bool flag = assets->get_permanent_by_default();
    assets->set_permanent_by_default(false);
    model_component_->set_up_mesh_from_profile(profile, false, skater_number_, checksums::Name{0});
    assets->set_permanent_by_default(flag);
    memory::Hub::get_pull()->leave_heap_scope();
    add_component_from_structure(this, [](script::Struct* params) {
        params->put_name_key(checksums::Name{kPartHash}, kShadow);  // shadow
        params->put_name_key(checksums::Name{kShadowType}, kDetailed); // ShadowType
    });
    shadow_ = core::down_cast<GroundShadowModule>(fetch_module(kShadow));
    shadow_->set_follow_object(false);
    if (is_local_skater()) {
        add_component_from_structure(
            this, [](script::Struct* params) { params->put_name_key(checksums::Name{kPartHash}, kVibration); }); // vibration
        vibration_ = core::down_cast<PadRumbleModule>(fetch_module(kVibration));
        add_component_from_structure(
            this, [](script::Struct* params) { params->put_name_key(checksums::Name{kPartHash}, kSkaterStancePanel); }); // SkaterStancePanel
    } else {
        vibration_ = nullptr;
    }
    if (is_local_skater()) {
        add_component_from_structure(
            this, [](script::Struct* params) { params->put_name_key(checksums::Name{kPartHash}, kSkaterSound); }); // skatersound
        sound_ = core::down_cast<RiderSfxPlayer>(fetch_module(kSkaterSound));
        vehicle_sound_ = core::down_cast<SkaterVehicleSoundComponent>(fetch_module(kSkaterVehicleSound)); // SkaterVehicleSound
    }
    seal(); // (Finalize)
    if (is_local_skater()) {
        // The pad's settings for its viewport (the skate module's, 8 bytes each from +0x394).
        core_physics_->set_auto_kick(core::at(skate->participants(), view_slot_).auto_kick_);
        core_physics_->set_tap_spin(core::at(skate->participants(), view_slot_).spin_taps_);
        vibration_->change_live(core::at(skate->participants(), view_slot_).vibration_ != 0);
        if (is_local_skater())
            own_script()->bind_script("SkaterInit", nullptr, this);
    }
    update_from_profile(profile);
    set_stats_from_profile(profile);
    if (is_local_skater()) {
        const float max_speed = get_scaled_physics(0xCC5F87AA); // Skater_Max_Max_Speed_Stat
        core::down_cast<RiderSfxPlayer>(fetch_module(kSkaterSound))->set_full_speed(max_speed);
    }
    if (MoveExecutor* state = trick_) {
        state->apply_trick_bindings(profile);
        MoveExecutor* tricks = trick_;
        tricks->clear_event_buffer();
        tricks->clear_queue();
        tricks->clear_block1238();
        tricks->clear_block1280();
        trick_->set_score(score_holder_->points_held());
    }
    if (RiderGapTracker* other = gap_tracker_)
        other->set_points_held(score_holder_->points_held());
    if (ObjectModule* collision = fetch_module(kCollision)) {
        // (its +0x18, read through the folded getter 0x005480F0)
        if (render::HitMesh* shape = core::down_cast<ContactShapeModule>(collision)->hit_shape())
            shape->flag_bits() |= 2;
    }
    model_component_->jump_to(); // the model's update
    script::launch_script(kInitSkaterParticles, nullptr, this, false, nullptr);
    anim_blender_->play_animation(kDefaultName, std::bit_cast<uint32_t>(kDefaultAnimBlend)); // default
    if (is_local_skater()) {
        for (std::unique_ptr<goals::CreateATrick>& trick : trick_slots_)
            trick = std::make_unique<goals::CreateATrick>();
        script::launch_script(kSpawnAddPremadeCatsToSkater, nullptr, this, false, nullptr); // spawn_add_premade_cats_to_skater
        GameEntity::invoke_method_by_name(kSkaterPhysicsControlSwitchWalkingToSkating, nullptr, nullptr);  // SkaterPhysicsControl_SwitchWalkingToSkating
        for (int i = 0; i < kProjectileCount; ++i) {
            std::array<char, kProjectileNameBytes> name{};
            text::format_to(name, "projectile_%i", i);
            core::at(projectiles_, i).name = checksums::hash_from_text(name.data());
            core::at(projectiles_, i).active = false;
        }
    }
    add_rigid_body_pusher(this);
    // Its controls' tag (a custom skater's own, if it has one) and sticker.
    script::Struct* info = profile->details_params();
    const char* tag = nullptr;
    info->fetch_text(kTagTexture, &tag, 1); // tag_texture
    ObjectModule* controls = fetch_module(kTagging);
    if (label_ == kCustomSkater) {
        int has_custom_tag = 0;
        info->fetch_int_value(kHasCustomTag, &has_custom_tag, 1);
        if (has_custom_tag)
            core::down_cast<TaggingComponent>(controls)->apply_custom_tag_texture();
        else
            core::down_cast<TaggingComponent>(controls)->set_graffiti_texture(tag);
    } else {
        core::down_cast<TaggingComponent>(controls)->set_graffiti_texture(tag);
    }
    const char* sticker = nullptr;
    if (info->fetch_text(kStickerTexture, &sticker, 0)) // sticker_texture
        core::down_cast<TaggingComponent>(controls)->set_sticker_texture(sticker);
}

// 0x005822A0
void Rider::init_from_profile(RiderIdentity* profile)
{
    prepare_all(profile);
    online::Hub* net = online::online_hub();
    if (is_local_skater() && static_cast<uint8_t>(commands::cfunc_in_multiplayer_game(nullptr, nullptr)) != 0) {
        // Its network messages.
        net::AppHost* client = net->fetch_patron(view_slot_);
        net::Router* dispatcher = &client->dispatcher();
        struct HandlerEntry {
            online::MessageId id;
            net::HandlerSlotValue code;
        };
        const auto handlers = std::to_array<HandlerEntry>({
            {.id = online::MessageId::kTrickObjects, .code = &modules::PointTally::on_score_msg},        {.id = online::MessageId::kRanIntoAck, .code = &RiderFrameLog::handle_collision_messages},
            {.id = online::MessageId::kRanInto, .code = &RiderFrameLog::handle_collision_messages}, {.id = online::MessageId::kHitOwner, .code = &RiderFrameLog::handle_hit_messages},
            {.id = online::MessageId::kHit, .code = &RiderFrameLog::handle_hit_messages},       {.id = online::MessageId::kTagStolen, .code = &RiderHostSync::handle_graffiti_steal}});
        for (const auto& handler : handlers)
            dispatcher->append_listener(handler.id, handler.code, 0, this, kListenerPriority);
    }
    // Never suspended (its suspend component's distances).
    if (auto* suspend = core::down_cast<DormancyModule>(fetch_module(kSuspend))) {
        core::at(suspend->lod_distances(), 0) = kNearLodDistance;
        core::at(suspend->lod_distances(), 1) = kMidLodDistance;
        core::at(suspend->lod_distances(), 2) = kFarLodDistance;
        core::at(suspend->lod_distances(), 3) = kFarLodDistance;
    }
}

// 0x005823A0
void Rider::run_state_script(bool walk, bool other)
{
    if (!own_script()) {
        set_own_script(script::Script::create());
    }
    script::Struct* params = nullptr;
    if (walk || other) {
        gsl::owner<script::Struct*> memory = script::spawn_param_bag();
        params = memory ? memory::construct_in<script::Struct>(memory) : nullptr;
        Expects(params != nullptr);
        if (walk)
            params->put_name_key(checksums::Name{0u}, kWalk); // Walk
        if (other)
            params->put_name_key(checksums::Name{0u}, kInCleanup);
    }
    own_script()->bind_script(kSkaterInit, params, this);
    if (is_local_skater())
        own_script()->tick_frame();
    if (params) {
        params->get_clear();
        script::delete_struct(params);
    }
}

// 0x00582510
void Rider::switch_to_script(uint32_t name, script::Struct* params)
{
    if (!own_script()) {
        set_own_script(script::Script::create());
    }
    if (ObjectModule* flip_and_rotate = fetch_module(kSkaterFlipAndRotate))
        core::down_cast<RiderSpinAndFlip>(flip_and_rotate)->do_after_anim();
    own_script()->bind_script(name, params, this);
    own_script()->tick_frame();
}

// 0x005825E0
void Rider::write_cat_info(script::Struct* save)
{
    gsl::owner<script::Array*> memory = script::alloc_script_array();
    gsl::owner<script::Array*> tricks = memory ? memory::construct_in<script::Array>(memory) : nullptr;
    Expects(tricks != nullptr);
    tricks->configure_size_kind(kCatTrickCount, script::QbValueKind::kStruct);
    for (int i = 0; i < kCatTrickCount; ++i) {
        gsl::owner<script::Struct*> struct_memory = script::spawn_param_bag();
        script::Struct* trick = struct_memory ? memory::construct_in<script::Struct>(struct_memory) : nullptr;
        const goals::CreateATrick* info = core::at(trick_slots_, i).get();
        trick->put_sub_struct(kOther, info->other_params());
        trick->append_array_value(kAnimations, info->animations());
        trick->append_array_value(kRotations, info->spin_list());
        tricks->set_cell(i, script::Cell::of(trick));
    }
    save->append_array_ref(kTricks, tricks);
}

// 0x00582740
void Rider::write_list_44b_66a_9f(script::Struct* save)
{
    gsl::owner<script::Array*> memory = script::alloc_script_array();
    gsl::owner<script::Array*> entries = memory ? memory::construct_in<script::Array>(memory) : nullptr;
    Expects(entries != nullptr);
    entries->configure_size_kind(kGoalStatusCount, script::QbValueKind::kInteger);
    for (int i = 0; i < kGoalStatusCount; ++i)
        entries->set_element(i, static_cast<uint32_t>(static_cast<AttributeTracker*>(stats_)->get_goal_status(i)));
    save->append_array_ref(kList44b66a9f, entries);
}

// 0x00582800
void Rider::launch_projectile()
{
    // Its flat heading.
    float heading_x = core::at(basis_frame().row, 2).x, heading_y = 0.0f, heading_z = core::at(basis_frame().row, 2).z;
    float best = kNoHitDistance;
    const GameEntity* target = nullptr;
    const double length =
        std::sqrt(static_cast<double>(heading_z) * heading_z + static_cast<double>(heading_x) * heading_x);
    if (length > 0.0) {
        const double inverse = 1.0 / length;
        heading_x = static_cast<float>(heading_x * inverse);
        heading_y = static_cast<float>(0.0 * inverse);
        heading_z = static_cast<float>(heading_z * inverse);
    }
    // From a wrist (the right one, or the left by its stance and whether it's walking).
    const RiderPoseState* walking = physics_control_->pose();
    const bool flipped = core_physics_->state()->flag(PoseFlag::kFlipped).on != 0;
    const bool walk = !walking->in_vehicle() && walking->physics_mode() == objects::RiderPoseState::PhysicsMode::kWalking;
    const uint32_t wrist = (walk ? flipped : !flipped) ? 0x7EE14CFE : 0x84EE719D; // Bone_Wrist_L / Bone_Wrist_R
    maths::Vec4 bone, rotated;
    core::down_cast<BoneRigModule>(fetch_module(kSkeleton2))->get_local_bone_position(wrist, &bone);
    const maths::Vec4* turned = &basis_frame().rotate_vector(rotated, bone);
    const maths::Vec4 origin = {.x = static_cast<float>(static_cast<double>(turned->x) + at_point().x),
                                .y = static_cast<float>(static_cast<double>(turned->y) + at_point().y),
                                .z = static_cast<float>(static_cast<double>(turned->z) + at_point().z),
                                .w = static_cast<float>(static_cast<double>(turned->w) + at_point().w)};
    // The nearest projectile target (projectileproximity) ahead within the targeting distance and angle; a ped
    // that's been shot is left out, and a ped is aimed at a bone it chooses.
    float to_target_x = 0.0f, to_target_z = 0.0f;
    maths::Vec4 aim = {};
    for (const ObjectModule* component = object_hub_slot()->find_first_module_of_kind(kProjectileproximity); component;
         component = component->next_of_type()) {
        GameEntity* object = component->entity();
        if (!object)
            continue;
        bool ped = false;
        if (object->fetch_module(kPedLogic)) { // PedLogic
            const uint32_t state =
                core::down_cast<PedestrianBrainModule>(object->fetch_module(0x6EFDDFB8))->get_logic_state();
            ped = true;
            if (state == kShot) // shot
                continue;
        }
        const double dz = static_cast<double>(object->at_point().z) - origin.z;
        const double dy = static_cast<double>(object->at_point().y) - origin.y;
        const double dx = static_cast<double>(object->at_point().x) - origin.x;
        auto distance = static_cast<float>(std::sqrt(dx * dx + dy * dy + dz * dz));
        auto to_z = static_cast<float>(dz);
        auto to_x = static_cast<float>(dx);
        double to_y = 0.0;
        auto flat = static_cast<float>(std::sqrt(static_cast<double>(to_x) * to_x + static_cast<double>(to_z) * to_z));
        if (flat > 0.0f) {
            auto inverse = static_cast<float>(1.0 / flat);
            to_x = static_cast<float>(static_cast<double>(to_x) * inverse);
            to_y = static_cast<double>(inverse) * 0.0;
            to_z = static_cast<float>(static_cast<double>(to_z) * inverse);
        }
        auto dot = static_cast<float>(static_cast<double>(to_z) * heading_z + to_y * heading_y +
                                      static_cast<double>(to_x) * heading_x);
        if (!(script::fetch_float(kPhysicsProjectileTargetingDist) > distance)) // Physics_Projectile_Targeting_Dist
            continue;
        const double cone = std::cos(script::fetch_float(0x195A7DBA) * static_cast<double>(0.017453292f)); // ..._Angle
        if (!(cone < dot))
            continue;
        if (!(distance < best))
            continue;
        target = object;
        best = distance;
        to_target_x = to_x;
        to_target_z = to_z;
        if (ped) {
            script::launch_script(kChooseProjectileTargetBone, nullptr, nullptr, false, nullptr); // choose_projectile_target_bone
            const uint32_t target_bone = script::get_symbol_value(0x07A15639);   // target_bone
            maths::Vec4 at;
            core::down_cast<BoneRigModule>(object->fetch_module(kSkeleton2))->get_local_bone_position(target_bone, &at);
            // (turned by the object's rotation in place: 0x00411840, Nx's VectorTurn::turn_by)
            maths::Vec4 turned_at;
            object->basis_frame().rotate_vector(turned_at, at);
            at = turned_at;
            aim = {.x = static_cast<float>(static_cast<double>(at.x) + object->at_point().x),
                   .y = static_cast<float>(static_cast<double>(at.y) + object->at_point().y),
                   .z = static_cast<float>(static_cast<double>(at.z) + object->at_point().z),
                   .w = static_cast<float>(static_cast<double>(at.w) + object->at_point().w)};
        } else {
            aim = object->at_point();
        }
    }
    // LaunchProjectile from there (at the target, when there is one beyond 100).
    gsl::owner<script::Struct*> memory = script::spawn_param_bag();
    script::Struct* params = memory ? memory::construct_in<script::Struct>(memory) : nullptr;
    params->put_vec3("pos", {.x = origin.x, .y = origin.y, .z = origin.z, .w = origin.w});
    if (target && best > 100.0f) {
        params->push_float("ped_dist", best);
        params->push_float("heading_x", to_target_x);
        params->push_float("heading_z", to_target_z);
        params->put_vec3("ped_pos", {.x = aim.x, .y = aim.y, .z = aim.z, .w = aim.w});
    }
    run_sub_script(kLaunchProjectile, params); // LaunchProjectile
    if (params) {
        params->get_clear();
        script::delete_struct(params);
    }
}

// 0x00582D90
Rider::Rider(int viewport, EntityHub* manager, bool local, RiderSlot rider_slot, int /*unused*/)
{
    const auto id = rider_slot.id;
    const auto skater_number = rider_slot.skater_number_;
    // Its node in the skater list (lists::Elem<Rider>).
    skater_link_.make_node(this);
    set_is_local_skater(local);
    add_base_components(this);
    set_kind_tag(kOBJECTTYPESKATER);
    skater_number_ = skater_number;
    set_ident(id);
    if (skater_number == 0)
        get_tracker()->register_nickname(kSkater, this);
    else if (skater_number == 1)
        get_tracker()->register_nickname(kSkater2, this);
    list_link().set_priority(-kListPriority);
    manager->enrol_subject(this);
    view_slot_ = viewport;
    unknown31c_ = 0;
    debug::printf("D:%s:%i: m_skater_number = %i\n", "jects\\skater.cpp", kSourceLineSkaterNumber, viewport);
    debug::printf("D:%s:%i: m_id = %i\n", "jects\\skater.cpp", kSourceLineId, ident());
    anim_blender_ = nullptr;
    model_component_ = nullptr;
    shadow_ = nullptr;
    trick_ = nullptr;
    vibration_ = nullptr;
    pad_input_ = nullptr;
    sound_ = nullptr;
    gap_tracker_ = nullptr;
    score_holder_ = nullptr;
    core_physics_ = nullptr;
    stats_ = nullptr;
    walk_ = nullptr;
    physics_control_ = nullptr;
    // Its components, one structure (component = ...) changed for each.
    gsl::owner<script::Struct*> memory = script::spawn_param_bag();
    script::Struct* params = memory ? memory::construct_in<script::Struct>(memory) : nullptr;
    auto add = [this, params](uint32_t type) {
        Expects(params != nullptr);
        params->put_name_key(checksums::Name{kPartHash}, type);
        create_component_from_structure(params, nullptr);
    };
    add(kSkaterState);
    add(kSpecialItem);
    if (is_local_skater()) {
        add(kInput);
        pad_input_ = core::down_cast<PadInputModule>(fetch_module(kInput));
        add(kSkaterScore);
        score_holder_ = core::down_cast<RiderPointKeeper>(fetch_module(kSkaterScore));
        add(kSkaterMatrixQueries);
        add(kTrickName);
        trick_ = core::down_cast<MoveExecutor>(fetch_module(kTrickName));
        add(kSkaterPhysicsControl); // SkaterPhysicsControl
        physics_control_ = core::down_cast<RiderBoardPhysics>(fetch_module(kSkaterPhysicsControl));
        add(kSkaterCorePhysics); // SkaterCorePhysics
        core_physics_ = core::down_cast<skater::SkaterPhysicsModule>(fetch_module(kSkaterCorePhysics));
        add(kSkaterRotate);
        add(kSkaterGap);
        gap_tracker_ = core::down_cast<RiderGapTracker>(fetch_module(kSkaterGap));
        add(kSkaterAdjustPhysics);
        add(kTrigger);
        add(kSkaterFinalizePhysics);
        add(kSkaterCleanupState);
        add(kWalk); // Walk
        walk_ = core::down_cast<FootMovement>(fetch_module(kWalk));
        add(kSkaterLocalNetLogic);
        add(kSkaterEndRun);
        add(kSkaterBalanceTrick);
        add(kSkaterLoopingSound);
        add(kSkaterVehicleSound); // SkaterVehicleSound
        add(kSTATSManager);
        stats_ = core::down_cast<AttributeTracker>(fetch_module(kSTATSManager));
        add(kMovableContact);
        add(kSkaterRunTimer);
        add(kTaggingControl);
    } else {
        add(kSkaterScore);
        score_holder_ = core::down_cast<RiderPointKeeper>(fetch_module(kSkaterScore));
        add(kSkaterNonLocalNetLogic);
        add(kSkaterCleanupState);
        add(kSkaterFloatingName);
        add(kSkaterEndRun);
        add(kSkaterLoopingSound);
    }
    add(kSkaterStateHistory);
    state_history_ = core::down_cast<RiderFrameLog>(fetch_module(kSkaterStateHistory));
    add(kSkaterFlipAndRotate);
    // The controls, told its skater number.
    gsl::owner<script::Struct*> extra_memory = script::spawn_param_bag();
    script::Struct* extra = extra_memory ? memory::construct_in<script::Struct>(extra_memory) : nullptr;
    Expects(extra != nullptr);
    extra->put_int(checksums::Name{kUniqueID}, skater_number_);
    params->put_name_key(checksums::Name{kPartHash}, kTagging);
    create_component_from_structure(params, extra);
    add(kPagerMessage);
    if (extra) {
        extra->get_clear();
        script::delete_struct(extra);
    }
    if (params) {
        params->get_clear();
        script::delete_struct(params);
    }
}

// 0x00583400
void Rider::reset_components()
{
    if (is_local_skater()) {
        gap_tracker_->clear_gaps();
        core::down_cast<RiderTeeterMoves>(fetch_module(kSkaterBalanceTrick))->clear_params();
    }
    core::down_cast<PickupItemHandler>(fetch_module(kSpecialItem))->destroy_items();
    anim_blender_->reset_channels();
    if (script::Script* script = own_script()) {
        script::Script::destroy(script);
        set_own_script(nullptr);
    }
    // Not original - the special items gone, THAW's bike with them: off the bike (game/rider/thaw_bmx.h).
    if (skater::thaw_bmx::is_switched_on())
        skater::thaw_bmx::on_skater_reset(this);
}

// 0x00583470
void Rider::relaunch(uint32_t a, uint32_t b)
{
    const RiderPoseState* walking = physics_control_->pose();
    if (!walking->in_vehicle() && walking->physics_mode() == objects::RiderPoseState::PhysicsMode::kWalking) {
        // Walking: back to skating first.
        gsl::owner<script::Struct*> memory = script::spawn_param_bag();
        script::Struct* params = memory ? memory::construct_in<script::Struct>(memory) : nullptr;
        Expects(params != nullptr);
        params->put_name_key(checksums::Name{kNewScript}, kOnGroundAI);
        switch_to_script(kSwitchOnGroundAI, params);
        if (params) {
            params->get_clear();
            script::delete_struct(params);
        }
    }
    if (own_script())
        own_script()->run_on_exit_scripts();
    core_physics_->get_reset();
    auto* camera =
        core::down_cast<GameEntity>(object_hub_slot()->look_up_entity(view_slot_ == 1 ? kSkaterCam1 : kSkaterCam0));
    core::down_cast<RiderChaseView>(camera->fetch_module(kSkaterCamera))->restore_default_state();
    reset_state();
    reset_components();
    set_up_network();
    // (the flags as the original's callee reads its bool arguments: their low bytes)
    run_state_script(static_cast<uint8_t>(a) != 0, static_cast<uint8_t>(b) != 0);
    core::down_cast<RiderSessionEnd>(fetch_module(kSkaterEndRun))->clear_forced();
}

// The custom restart's position and orientation put back (SkipToCustomRestart, and SetCustomRestart Set).
void Rider::skip_to_custom_restart()
{
    jump_to_spawn({.node = 0, .walking = custom_restart_walking_});
    at_point() = custom_restart_pos_;
    basis_frame() = custom_restart_matrix_;
    core_physics_->safe_pos() = at_point();
    old_pos() = at_point();
    mark_jumped_ahead(true);
    core_physics_->display_matrix() = core_physics_->entity()->basis_frame();
    core_physics_->set_floor_normal(core::at(basis_frame().row, 1));
    core_physics_->old_normal() = core_physics_->floor_normal();
    core_physics_->drawn_normal() = core_physics_->old_normal();
    static_cast<GroundShadowModule*>(shadow_)->change_place(&at_point());
    static_cast<GroundShadowModule*>(shadow_)->set_normal(&core::at(basis_frame().row, 1));
}

// 0x005835D0
bool Rider::invoke_method_by_name(uint32_t name, script::Struct* params, script::Script* script)
{
    switch (name) {
    case kKillSkater: { // KillSkater
        // (the original keeps the flag in the low byte of its params slot: the restart is told that word, the
        // address's other bytes with it - only the flag here)
        const uint32_t walking = params->has_entry_named(0xD6F06BF6) ? 1u : 0u;
        uint32_t prefix = 0;
        const char* text = nullptr;
        if (!params->read_hash_value(kPrefix, &prefix, 0) && params->fetch_text(kPrefix, &text, 0)) // prefix
            prefix = checksums::hash_from_text(text);
        if (prefix) {
            script::Struct* node = level_nodes::find_closest_node_with_prefix(prefix, &at_point());
            jump_to_spawn({.node = (level_nodes::get_node_name(node)), .walking = walking});
            return true;
        }
        uint32_t node = 0;
        if (params->read_hash_value(kNode, &node, 0)) { // node
            jump_to_spawn({.node = node, .walking = walking});
            return true;
        }
        // The first restart linked from the script's node; none: the skate module's (unless in the park editor).
        auto from = static_cast<uint32_t>(script->list_link());
        const int count = static_cast<int>(level_nodes::get_link_count(from));
        int i = 0;
        for (; i < count; ++i) {
            const uint32_t link = level_nodes::fetch_link(from, i);
            if (is_restart_node(link)) {
                jump_to_spawn({.node = link, .walking = walking});
                break;
            }
        }
        if (i == count && park_editing::ParkBuilderMode::sole()->editor_state() != kOff)
            modules::Skate::sole()->teleport_to_spawn(this, 0, walking);
        return true;
    }
    case kAddSkaterToWorld: // AddSkaterToWorld
        modules::Skate::sole()->conceal_skater(this, false);
        return true;
    case kRemoveSkaterFromWorld: // RemoveSkaterFromWorld
        modules::Skate::sole()->conceal_skater(this, true);
        return true;
    case kSkaterIsNamed: { // SkaterIsNamed
        uint32_t named = 0;
        params->read_hash_value(0u, &named, 1);
        return named == label_;
    }
    case kShouldMongo: // ShouldMongo
        if (!is_local_skater())
            return false;
        if (stroke_kind_ == kMongoWhenSwitch) // mongo_when_switch
            return (core_physics_->is_sides_swapped() & kByteMask) != 0;
        return stroke_kind_ == kAlwaysMongo; // always_mongo
    case kLaunchDirtyProjectile:                       // LaunchDirtyProjectile
        debug::printf("LaunchDirtyProjectile\n");
        launch_projectile();
        return true;
    case kSparksOffName: // SparksOff
        script::launch_script(kSparksOff, params, this, false, nullptr);
        sparks_ = false;
        return true;
    case kSparksOnName: // SparksOn
        script::launch_script(kSparksOn, params, this, false, nullptr);
        sparks_ = true;
        params->has_bare_flag("RailNotRequired"); // (read, unused)
        return true;
    case kSetCustomRestart:                             // SetCustomRestart
        if (params->has_bare_flag(kSet)) { // Set: here and now - and skipped to
            custom_restart_set_ = true;
            custom_restart_pos_ = at_point();
            custom_restart_matrix_ = basis_frame();
            custom_restart_walking_ = physics_control_->is_on_foot();
            skip_to_custom_restart();
            return true;
        }
        if (params->has_bare_flag(kClear)) { // clear
            custom_restart_set_ = false;
            return true;
        }
        return custom_restart_set_;
    case kSkipToCustomRestart: // SkipToCustomRestart
        skip_to_custom_restart();
        return true;
    case kIsLocalSkater: // IsLocalSkater
        return is_local_skater() != 0;
    case kResetRigidBodyCollisionRadiusBoost: // ResetRigidBodyCollisionRadiusBoost
        set_rigid_body_pusher_reach(this, 0.0f);
        return true;
    case kGetCameraId: // GetCameraId
        script->args().get()->put_name_key(checksums::Name{kCameraId}, view_slot_ == 1 ? kSkaterCam1 : kSkaterCam0);
        return true;
    case kGetStat: { // fetch_tally
        int stat = 0;
        params->fetch_int_value(kStat, &stat, 1);
        script::Struct* results = script->args().get();
        results->push_float(checksums::Name{kStatValue}, fetch_tally(stat)); // stat_value
        return true;
    }
    case kGetScriptedStat: { // GetScriptedStat
        script::Struct* stat = nullptr;
        params->read_sub_struct(0u, &stat, 1);
        script::Struct* results = script->args().get();
        results->push_float(checksums::Name{kStatValue}, get_scaled_value(stat));
        return true;
    }
    case kMoveToRandomRestart: // MoveToRandomRestart
        move_to_random_restart();
        return true;
    case kPlaceBeforeCamera: { // PlaceBeforeCamera: 144 in front of the camera, facing it, moving away at 10
        if (!render::fetch_live_camera(view_slot_))
            return true;
        auto* camera = render::fetch_live_camera(view_slot_);
        const maths::Vec4* camera_pos = camera->fetch_location();
        camera = render::fetch_live_camera(view_slot_);
        const maths::Mat4* camera_matrix = camera->fetch_mat();
        basis_frame().reset_to_identity();
        at_point() = *camera_pos;
        at_point() -= core::at(camera_matrix->row, 2) * kCameraBackOffset;
        old_pos() = at_point();
        core_physics_->safe_pos() = at_point();
        basis_frame() = *camera_matrix;
        core::at(basis_frame().row, 2) = -core::at(basis_frame().row, 2);
        core::at(basis_frame().row, 0) = -core::at(basis_frame().row, 0);
        core_physics_->display_matrix() = core_physics_->entity()->basis_frame();
        core_physics_->set_floor_normal(core::at(basis_frame().row, 1));
        core_physics_->old_normal() = core_physics_->floor_normal();
        core_physics_->drawn_normal() = core_physics_->old_normal();
        static_cast<GroundShadowModule*>(shadow_)->change_place(&at_point());
        static_cast<GroundShadowModule*>(shadow_)->set_normal(&core::at(basis_frame().row, 1));
        vel() = -*camera_pos;
        vel().y = 0.0f;
        vel().set_length(kRecoverSpeed);
        mark_jumped_ahead(false);
        return true;
    }
    case kDie: // die
        return true;
    case kGetSkaterNumber:                                             // GetSkaterNumber
        script->args().get()->put_int(checksums::Name{kSkaternumber}, view_slot_); // skaternumber
        return true;
    default:
        return GameEntity::invoke_method_by_name(name, params, script);
    }
}

// 0x00583DA0 (vtable +0x3C)
void Rider::jump_to_spawn(SpawnChoice spawn_choice)
{
    const auto node = spawn_choice.node;
    const auto walking = spawn_choice.walking;
    online::Hub* net = online::online_hub();
    core::down_cast<RiderBoardPhysics>(fetch_module(kSkaterPhysicsControl))->set_flag26(1);
    if (is_local_skater()) {
        pad_input_->set_disabled(false);
        unpause();
        if (AttributeTracker* list = stats_)
            list->set_first_update(true);
        if (net->in_net_game()) {
            commands::cfunc_notify_bail_done(nullptr, nullptr);
            core_physics_->state()->set_flag(PoseFlag::kBailing, 0);
        }
    }
    if (vibration_)
        vibration_->reset_device();
    if (node)
        glide_to_node(node);
    relaunch(walking, 0);
    if (is_local_skater()) {
        core::down_cast<RiderSpinAndFlip>(fetch_module(kSkaterFlipAndRotate))->apply_flip_to_anims();
        if (is_local_skater()) {
            if (pause_state_ == 0) {
                mark_jumped_ahead(false);
                auto* camera = core::down_cast<GameEntity>(
                    object_hub_slot()->look_up_entity(view_slot_ == 1 ? kSkaterCam1 : kSkaterCam0));
                if (camera)
                    camera->tick_frame();
            }
            // The restart node's TriggerScript, run on it.
            if (node && level_nodes::fetch_vertex(node, !online::online_hub()->in_net_game())) {
                uint32_t script = 0;
                if (level_nodes::fetch_vertex(node, true)->read_hash_value(kTriggerScript, &script, 0))
                    start_script_task(script, static_cast<int>(node), false, false, nullptr);
            }
        }
    }
    mark_jumped_ahead(true);
    // In these game modes the server is told (message 0x57).
    auto mode = [] { return goals::game_mode()->get_name_key(); };
    if (mode() == kNetctf || mode() == kNetking || mode() == kKing) {
        net::NetMessageInfo message;
        auto* client = core::down_cast<net::Guest>(net->fetch_patron(view_slot_));
        if (online::PlayerRecord* player = net->find_player_slot_by_obj(static_cast<uint16_t>(ident())))
            player->mark_capture_blocked();
        message.set_ident(online::MessageId::kRiderReady);
        message.set_queue(net::NetMessageInfo::kLaneOrdered);
        message.set_group_id(online::MessageGroup::kGame);
        client->send_to_host(&message);
    }
}

// 0x00583FB0
Rider::~Rider()
{
    if (is_halted_ && is_local_skater() && pad_input_) {
        pad_input_->handler()->dev()->unpause_actuators();
        pad_input_->handler()->dev()->stop_actuators();
    }
    reset_components();
    script::launch_script(kDestroySkaterParticles, nullptr, this, false, nullptr);
    for (std::unique_ptr<goals::CreateATrick>& trick : trick_slots_)
        trick.reset();
    if (is_local_skater() && view_slot_ == 1) {
        if (Entity* camera = object_hub_slot()->look_up_entity(kSkaterCam1))
            camera->flag_as_dead();
    }
    // Out of the skater list.
    skater_link_.uncouple();
    // (then shown_name_'s destructor and GameEntity's)
}

} // namespace objects
