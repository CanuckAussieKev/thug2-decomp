// Pieces of the skater object, 0x0057FE30-0x00580BD8. See rider.h.

#include "game/skater/rider.h"

#include <bit>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <new>

#include "game/core/checksums.h"
#include "game/core/dbg.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/gfx/view_cam.h"
#include "game/mth/mat4.h"
#include "game/net/net_app.h"
#include "game/net/net_client.h"
#include "game/net/net_dispatcher.h"
#include "game/script/nodes.h"
#include "game/sio/device.h"
#include "game/skate/profile_manager.h"
#include "game/mth/random.h"
#include "game/mth/vec4.h"
#include "game/obj/compositemgr.h"
#include "game/obj/game_objects.h"
#include "game/obj/entity_hub.h"
#include "game/obj/tracker.h"
#include "game/script/cscript.h"
#include "game/script/param_struct.h"
#include "game/ass/resource_registry.h"
#include "game/front/front_end.h"
#include "game/net/gamenet_manager.h"
#include "game/net/player_info.h"
#include "game/obj/anim_blender.h"
#include "game/obj/control_pad.h"
#include "game/obj/model_component.h"
#include "game/obj/proximity.h"
#include "game/obj/rigidbody_component.h"
#include "game/obj/shadow_component.h"
#include "game/obj/skeleton_component.h"
#include "game/obj/vibration_component.h"
#include "game/obj/walk_hang.h"
#include "game/sfx/sfx.h"
#include "game/skate/career.h"
#include "game/skater/core_physics.h"
#include "game/skater/end_run.h"
#include "game/skater/flip_and_rotate.h"
#include "game/skater/gap_entry.h"
#include "game/skater/looping_sound.h"
#include "game/skater/nonlocal_net_logic.h"
#include "game/skater/physics_control.h"
#include "game/skater/skater_score.h"
#include "game/skater/skater_state.h"
#include "game/skater/special_item.h"
#include "game/skater/stats_manager.h"
#include "game/skater/tagging.h"
#include "game/skater/thaw_bmx.h"
#include "game/skater/trick_component.h"
#include "game/skater/walk_camera.h"
#include "game/skater/walk_physics.h"
#include "game/sys/game_clock.h"
#include "game/goals/create_a_trick.h"
#include "game/goals/goal_manager.h"
#include "game/gfx/texture_dict.h"
#include "game/gfx/viewport.h"
#include "game/obj/ped_logic.h"
#include "game/script/game_cfuncs.h"
#include "game/skate/game_mode.h"
#include "game/skate/point_tally.h"
#include "game/skate/skate_module.h"
#include "game/skater/balance_trick.h"
#include "game/skater/skater_camera_component.h"
#include "game/ed/park_editor.h"
#include "game/core/code_address.h"
#include "game/obj/entity.h"
#include "game/core/stubs.h"
#include "game/obj/car_physics.h"
#include "game/skater/state_history.h"
#include "game/skater/local_net_logic.h"
#include "game/gfx/engine_geom.h"
#include "game/gfx/engine_texture.h"
#include "game/goals/objective.h"

namespace GameObjects {

float g_skaterDownLightColour;
float g_skaterLightColour;
uint32_t g_skaterLightsMade;

namespace {

extern void** const kSkaterVtable;

// Names.
constexpr uint32_t kIsPro = 0xD82F8AC8;          // is_pro
constexpr uint32_t kIsCustom = 0x5A3264BB;       // (a flag: not a pro)
constexpr uint32_t kIsMale = 0x3F813177;         // is_male
constexpr uint32_t kIsFemale = 0xE5C2F84C;       // is_female
constexpr uint32_t kIsNamed = 0xC2E3D008;        // is_named
constexpr uint32_t kStance = 0x7D02BCC3;         // stance
constexpr uint32_t kRegular = 0xB58EFC2B;        // regular
constexpr uint32_t kGoofy = 0x287FCD4E;          // goofy
constexpr uint32_t kStyle = 0xCC424795;          // style
constexpr uint32_t kSparksOn = 0x0DC85C7B;       // SkaterParticles_SparksOn (a script)
constexpr uint32_t kSparksOff = 0xABD22E8D;      // SkaterParticles_SparksOff (a script)
constexpr uint32_t kTricks = 0x1E26FD3E;         // tricks
constexpr uint32_t kOther = 0x26A7CADF;          // other
constexpr uint32_t kAnimations = 0x6B19FC8F;     // Animations
constexpr uint32_t kRotations = 0x2E628EE6;      // rotations
constexpr uint32_t kKillLoadPremadeCats = 0x33FE5817; // kill_load_premade_cats (a script)
constexpr uint32_t kList44B66A9F = 0x44B66A9F;
constexpr uint32_t kChapterStatus = 0xDFA17D68;  // CHAPTER_STATUS
constexpr uint32_t kChapterIntros = 0x2C6437CD;  // CHAPTER_INTROS_VIEWED
constexpr uint32_t kChapterOuttros = 0x4B1ED398; // CHAPTER_OUTTROS_VIEWED
constexpr uint32_t kSaveChapterStatus = 0x8E8EE0C0;  // (the save's names for them)
constexpr uint32_t kSaveChapterIntros = 0xD5F46FDA;
constexpr uint32_t kSaveChapterOuttros = 0x72FC1AE3;
constexpr uint32_t kSkaterCorePhysics = 0x5BD63B29; // SkaterCorePhysics (a component)
constexpr uint32_t kSkaterPhysicsControl = 0xD9151D4D; // SkaterPhysicsControl (a component)
constexpr uint32_t kSkaterPhysics = 0x8CBC89DD;  // skater_physics
constexpr uint32_t kChainsawPhysics = 0xD3D12913; // chainsaw_physics
constexpr uint32_t kSkaterDefaultStats = 0x6357D57C; // Skater_Default_Stats
constexpr uint32_t kCheatStats13 = 0x6B0F24BC;   // CHEAT_STATS_13
constexpr uint32_t kSwitch = 0x9016B4E7;         // switch
constexpr uint32_t kDiff = 0xBA8FB854;           // diff
constexpr uint32_t kLimit = 0x8069179F;          // limit
constexpr uint32_t kIsClassic = 0x5A2C589A;      // is_classic
constexpr uint32_t kSkaterCam0 = 0x967C138C;     // skatercam0
constexpr uint32_t kSkaterCam1 = 0xE17B231A;     // skatercam1
constexpr uint32_t kModelComponent = 0x286A8D26; // model (a component)
constexpr uint32_t kPauseValue = 0x9816E1E3;     // (a physics integer: pausing freezes the whole game)

uint8_t* Skate() { return reinterpret_cast<uint8_t*>(GameModules::g_skateModule); }
OnlinePlay::Hub* NetLink() { return OnlinePlay::g_gameNetManager; }
bool Rule5A1180() { return static_cast<uint8_t>(ScriptCommands::CFunc_InSplitScreenGame(nullptr, nullptr)) != 0; }

template <typename T>
T& At(void* base, uint32_t offset)
{
    return *reinterpret_cast<T*>(static_cast<uint8_t*>(base) + offset);
}

} // namespace

// 0x0057FE30
void CRider::SetStatOverride(int index, float value)
{
    if (Rule5A1180()) {
        if (value > 10.0f)
            value = 10.0f;
        else if (value < 0.0f)
            value = 0.0f;
    }
    m_statOverrides[index] = value;
}

// 0x0057FEB0 / 0x0057FEC0
void* CRider::ObtainActiveLens() { return Engine3D::FetchLiveCamera(m_viewSlot); }
int CRider::GetSkaterNumber() { return m_skaterNumber; }

// 0x0057FED0
bool CRider::MatchesParams(Scripts::CStruct* params)
{
    if (params->HasBareFlag(kIsPro) && !m_pro)
        return false;
    if (params->HasBareFlag(kIsCustom) && m_pro)
        return false;
    if (params->HasBareFlag(kIsMale) && !m_male)
        return false;
    if (params->HasBareFlag(kIsFemale) && m_male)
        return false;
    uint32_t value;
    if (params->ReadHashValue(kIsNamed, &value, 0) && value != m_label)
        return false;
    if (params->ReadHashValue(kStance, &value, 0) && value != (m_goofy ? kGoofy : kRegular))
        return false;
    if (params->ReadHashValue(kStyle, &value, 0) && value != m_style)
        return false;
    return true;
}

// 0x0057FFE0 / 0x00580000
void CRider::SparksOn()
{
    Scripts::LaunchScript(kSparksOn, nullptr, this, false, nullptr);
    m_sparks = true;
}
void CRider::SparksOff()
{
    if (!m_sparks)
        return;
    Scripts::LaunchScript(kSparksOff, nullptr, this, false, nullptr);
    m_sparks = false;
}

// 0x00580030
Maths::Mat4* CRider::GetDrawMatrix() { return reinterpret_cast<Maths::Mat4*>(m_corePhysics + 0x18); }

// 0x00580040 / 0x00580050
void* CRider::GetBlock238() { return m_block238; }
bool CRider::GetFlag2C4() { return m_flag2C4; }

// 0x00580060
void CRider::LoadCATInfo(Scripts::CStruct* params)
{
    DebugOut::Printf("LoadCATInfo was called\n");
    Scripts::CArray* tricks = nullptr;
    params->FetchList(kTricks, &tricks, 0);
    for (int i = 0; i < 11; ++i) {
        auto* trick = reinterpret_cast<Scripts::CStruct*>(tricks->FetchPart(i));
        uint8_t* info = m_trickInfo[i];
        Scripts::CStruct* other = nullptr;
        trick->ReadSubStruct(kOther, &other, 1);
        At<Scripts::CStruct*>(info, 4)->AppendStructureIfAny(other);
        Scripts::CArray* animations = nullptr;
        trick->FetchList(kAnimations, &animations, 1);
        Scripts::DuplicateArray(At<Scripts::CArray*>(info, 0xC), animations);
        Scripts::CArray* rotations = nullptr;
        trick->FetchList(kRotations, &rotations, 1);
        Scripts::DuplicateArray(At<Scripts::CArray*>(info, 8), rotations);
    }
    Scripts::LaunchScript(kKillLoadPremadeCats, nullptr, nullptr, false, nullptr);
}

// 0x00580150
void CRider::LoadList44B66A9F(Scripts::CStruct* params)
{
    reinterpret_cast<void(__thiscall*)(void*, int)>((*static_cast<void***>(m_list28C))[5])(m_list28C, 0);
    Scripts::CArray* entries = nullptr;
    params->FetchList(kList44B66A9F, &entries, 0);
    for (int i = 0; i < 0x46; ++i)
        static_cast<CAttributeTracker*>(m_list28C)->SetGoalStatus(i, static_cast<int>(entries->FetchPart(i)));
}

// 0x005801B0
void CRider::WriteChapterInfo(Scripts::CStruct* save)
{
    save->AppendArrayValue(kSaveChapterStatus, Scripts::FetchList(kChapterStatus));
    save->AppendArrayValue(kSaveChapterIntros, Scripts::FetchList(kChapterIntros));
    save->AppendArrayValue(kSaveChapterOuttros, Scripts::FetchList(kChapterOuttros));
}

// 0x00580210
void CRider::ReadChapterInfo(Scripts::CStruct* save)
{
    Scripts::CArray* array = nullptr;
    if (save->FetchList(kSaveChapterStatus, &array, 0))
        Scripts::DuplicateArray(Scripts::FetchList(kChapterStatus), array);
    if (save->FetchList(kSaveChapterIntros, &array, 0))
        Scripts::DuplicateArray(Scripts::FetchList(kChapterIntros), array);
    if (save->FetchList(kSaveChapterOuttros, &array, 0))
        Scripts::DuplicateArray(Scripts::FetchList(kChapterOuttros), array);
}

// 0x005802C0
void CRider::Set42BD60(float value) { SetRigidBodyPusherReach(this, value); }

// 0x005802E0
Scripts::CStruct* __cdecl GetPhysicsStructure(CGameEntity* skater)
{
    if (!skater || !At<bool>(skater, 0xBC))
        return Scripts::ReadSubStruct(kSkaterPhysics);
    auto* core = reinterpret_cast<uint8_t*>(skater->FetchModule(kSkaterCorePhysics));
    // Not original - on THAW's bike, THAW's bicycle_physics (thaw_bicycle_physics: game/skater/thaw_bmx.h), as
    // THAW's own GetPhysicsStructure (0x0058ED90) picks it.
    // Original behaviour: the vehicle's, the chainsaw's or the skater's below.
    if (Skater::ThawBmx::IsSwitchedOn()) {
        if (Scripts::CStruct* bike = Skater::ThawBmx::PhysicsStructure(core))
            return bike;
    }
    if (At<bool>(core, 0x2ED)) {
        switch (At<uint32_t>(core, 0x2F0)) { // what it rides
        case 0x3029FB1F: return Scripts::ReadSubStruct(0x8B3B782B); // Segway: segway_physics
        case 0x61C62B2D: return Scripts::ReadSubStruct(0x05716042); // mechbull: mechbull_physics
        case 0x86929898: return Scripts::ReadSubStruct(0x8711BC92); // minikart: minikart_physics
        case 0xB2C99BF0: return Scripts::ReadSubStruct(0x871FFB73); // Wheelchair: wheelchair_physics
        case 0x906F06E2: return Scripts::ReadSubStruct(0x43DA544E); // tricycle: tricycle_physics
        case 0xD8D2D646: return Scripts::ReadSubStruct(0x08C9AD5E); // gurney: gurney_physics
        case 0x14A3A4CF: return Scripts::ReadSubStruct(kChainsawPhysics); // chainsaw
        default: return Scripts::ReadSubStruct(kSkaterPhysics); // (slalom, shoppingcart, anything else)
        }
    }
    if (At<bool>(core, 0x302))
        return Scripts::ReadSubStruct(kChainsawPhysics);
    return Scripts::ReadSubStruct(kSkaterPhysics);
}

// 0x00580400
float __cdecl ReadTuningFloat(uint32_t name, int assert, CGameEntity* skater)
{
    Scripts::CStruct* physics = GetPhysicsStructure(skater);
    float value;
    if (physics->FetchFloat(name, &value, 0))
        return value;
    (void)assert;
    return Scripts::FetchFloat(name);
}

// 0x00580440
int __cdecl ReadTuningInt(uint32_t name, int assert, CGameEntity* skater)
{
    Scripts::CStruct* physics = GetPhysicsStructure(skater);
    int value;
    if (physics->FetchIntValue(name, &value, 0))
        return value;
    (void)assert;
    return Scripts::FetchIntValue(name);
}

// 0x00580480
void* CRider::GetPointsTotal() { return At<void*>(m_scoreHolder, 0x18); }

// 0x00580490
float CRider::FetchTally(int index)
{
    float stat = At<float>(Skate(), 0x3D0); // (forced by the skate module unless 0)
    if (stat == 0.0f) {
        uint32_t bits;
        std::memcpy(&bits, &m_statOverrides[index], 4);
        stat = bits == 0xBF800000 ? Scripts::FetchFloat(kSkaterDefaultStats) : m_statOverrides[index];
    }
    // Online: the server's rules for this player.
    if (NetLink()->InNetGame()) {
        auto* player = reinterpret_cast<OnlinePlay::PlayerRecord*>(NetLink()->PlayerByObjectId(static_cast<uint16_t>(m_ident)));
        if (!player)
            stat = 10.0f;
        else if (player->HoldsCrown())
            stat = 1.0f;
        else if (player->IsCarryingFlag())
            stat = 6.0f;
        else
            stat = 10.0f;
    } else if (Rule5A1180()) {
        auto* player = reinterpret_cast<OnlinePlay::PlayerRecord*>(NetLink()->PlayerByObjectId(static_cast<uint16_t>(m_ident)));
        if (player && player->HoldsCrown())
            stat = 1.0f;
    }
    void* cheats = At<void*>(Skate(), 0x20);
    float result = static_cast<CRiderStoryProgress*>(cheats)->IsCodeOn(static_cast<uint32_t>(Scripts::FetchIntValue(kCheatStats13)))
                       ? 15.0f
                       : stat;
    if (At<bool>(At<uint8_t*>(m_scoreHolder, 0x18), 0x74))
        result = static_cast<float>(result + 3.0);
    return result;
}

// 0x005805B0
float CRider::GetScaledValue(Scripts::CStruct* params)
{
    Scripts::CPair range, switchRange;
    range.PrepareAll();
    switchRange.PrepareAll();
    params->FetchPair(0u, &range, false);
    int statIndex;
    double stat = params->FetchIntValue(0u, &statIndex, 0) ? FetchTally(statIndex) : 10.0f;
    const float low = range.x, high = range.y;
    float result = static_cast<float>((static_cast<double>(high) - low) * stat * 0.1f + low);
    // Riding switch: times the switch pair, by the switch stat (0-1).
    bool goofy = At<bool>(At<uint8_t*>(m_corePhysics, 0xC), 0x110);
    uint8_t flipped = At<uint8_t>(At<uint8_t*>(m_corePhysics, 0x1528), 0x28);
    bool riding = goofy ? flipped != 0 : flipped == 0;
    if (riding && params->FetchPair(kSwitch, &switchRange, false)) {
        double s = FetchTally(6);
        double scale = (static_cast<double>(switchRange.y) - switchRange.x) * s * 0.1f + switchRange.x;
        if (scale < 0.0)
            scale = 0.0;
        else if (scale > 1.0)
            scale = 1.0;
        result = static_cast<float>(scale * result);
    }
    // The game's difficulty (one more in classic mode): times the diff pair's first (0) or second (2).
    GoalSystem::CGoalManager* mode = GameModules::Skate::Sole()->GetObjectiveBoard();
    int difficulty = mode->GetSkillTier(false);
    if (static_cast<GoalSystem::CMatchRules*>(static_cast<void*>(GoalSystem::g_gameMode))->IsTrue(kIsClassic))
        ++difficulty;
    if (difficulty != 1) {
        Scripts::CPair diff;
        diff.PrepareAll();
        if (params->FetchPair(kDiff, &diff, false)) {
            if (difficulty == 0)
                result = static_cast<float>(static_cast<double>(diff.x) * result);
            else if (difficulty == 2)
                result = static_cast<float>(static_cast<double>(diff.y) * result);
        }
    }
    // The limit: a maximum for a rising range, a minimum otherwise.
    float limit;
    if (!params->FetchFloat(kLimit, &limit, 0))
        return result;
    if (low < high)
        return result > limit ? limit : result;
    return result < limit ? limit : result;
}

// 0x00580790
float CRider::GetScaledPhysics(uint32_t name)
{
    Scripts::CStruct* params = nullptr;
    CEntity* object = CompositeObjectManager()->LookUpEntity(m_ident);
    Scripts::CStruct* physics = GetPhysicsStructure(static_cast<CGameEntity*>(object));
    if (!physics->ReadSubStruct(name, &params, 0))
        params = Scripts::ReadSubStruct(name);
    return GetScaledValue(params);
}

// 0x005807F0
float CRider::GetScaledParam(uint32_t name, Scripts::CStruct* params)
{
    Scripts::CStruct* structure = nullptr;
    params->ReadSubStruct(name, &structure, 0);
    return GetScaledValue(structure);
}

// 0x00580830
void CRider::MoveToRandomRestart()
{
    uint8_t* skate = Skate();
    uint32_t restart;
    int tries = 0;
    do {
        restart = reinterpret_cast<GameModules::Skate*>(skate)->SkipToRestartPoint(static_cast<uint32_t>(Maths::Rnd(8)));
        ++tries;
    } while (restart == m_lastRestart && tries < 20);
    m_lastRestart = restart;
    if (restart)
        reinterpret_cast<void(__thiscall*)(CRider*, uint32_t, int)>(m_vtable[0x3C / 4])(this, restart, 0);
}

// 0x00580880
CEntity* CRider::GetSkaterCamera()
{
    return CompositeObjectManager()->LookUpEntity(m_viewSlot == 1 ? kSkaterCam1 : kSkaterCam0);
}

// 0x005808A0
void CRider::SetPauseState(int state)
{
    void* frontEnd = reinterpret_cast<void*>(GameModules::g_frontEndInstance);
    if (m_pauseState == state || !FetchModule(kSkaterPhysicsControl))
        return;
    m_pauseState = state;
    auto pauseGame = [frontEnd](bool pause) {
        static_cast<GameModules::FrontEnd*>(frontEnd)->PauseGame(pause);
    };
    auto control = [](void* physicsControl, int paused) {
        static_cast<CRiderBoardPhysics*>(physicsControl)->SetFrozen(paused != 0);
    };
    switch (state) {
    case 0:
        if (ReadTuningInt(kPauseValue, 0, nullptr)) {
            DebugOut::Printf("UnPausing game\n");
            pauseGame(false);
        }
        break;
    case 1:
        if (ReadTuningInt(kPauseValue, 0, nullptr)) {
            DebugOut::Printf("Freezing game\n");
            pauseGame(true);
        } else if (CObjectModule* physicsControl = FetchModule(kSkaterPhysicsControl); At<uint8_t>(physicsControl, 0x18) != 1) {
            control(physicsControl, 1);
        }
        break;
    case 2:
        if (!ReadTuningInt(kPauseValue, 0, nullptr)) {
            CObjectModule* physicsControl = FetchModule(kSkaterPhysicsControl);
            if (At<uint8_t>(physicsControl, 0x18))
                control(physicsControl, 0);
        }
        break;
    default:
        break;
    }
}

// 0x005809C0
void CRider::UpdateShadowLight(int, int)
{
    uint32_t& made = g_skaterLightsMade;
    auto* light = &g_skaterLightColour; // (4 floats)
    auto* down = &g_skaterDownLightColour;  // (4 floats)
    if (!(made & 1)) {
        made |= 1;
        light[0] = 0.8f, light[1] = -0.8f, light[2] = 0.3f, light[3] = 1.0f;
    }
    if (!(made & 2)) {
        made |= 2;
        down[0] = 0.0f, down[1] = -1.0f, down[2] = 0.0f, down[3] = 1.0f;
    }
    // The model's light, reversed; kept at least 0.65 below the horizon.
    if (FetchModule(kModelComponent) && At<void*>(FetchModule(kModelComponent), 0x208)) {
        // (the model's +0x2C - read through the folded getter 0x00532EB0 - and its virtual slot 5 - through the jump
        // 0x00456B30)
        void* lights = At<void*>(At<void*>(FetchModule(kModelComponent), 0x208), 0x2C);
        if (lights) {
            const float* v = reinterpret_cast<float*(__thiscall*)(void*, int)>((*static_cast<void***>(lights))[5])(lights, 0);
            double x = v[0] * -1.0, y = v[1] * -1.0, z = v[2] * -1.0, w = v[3] * -1.0;
            light[0] = static_cast<float>(x);
            light[1] = static_cast<float>(y);
            const float zStored = static_cast<float>(z); // (the original reads z back as stored)
            light[2] = zStored;
            light[3] = static_cast<float>(w);
            if (y > -0.65f) {
                double flat = static_cast<double>(zStored) * zStored + x * x;
                double scale = flat > 0.0 ? std::sqrt(0.5775f / flat) : 0.0;
                light[1] = -0.65f;
                light[0] = static_cast<float>(x * scale);
                light[2] = static_cast<float>(zStored * scale);
            }
        }
    }
    // In the air (or not on the ground): fade towards straight down; else back.
    bool fading;
    uint8_t* state = m_state284;
    if (state && !At<bool>(At<uint8_t*>(state, 0x2C), 0x118) && !At<uint32_t>(At<uint8_t*>(state, 0x2C), 0x1C))
        fading = At<uint32_t>(At<uint8_t*>(m_corePhysics, 0x1528), 0x18) == 0;
    else if (!m_state288)
        fading = true;
    else
        fading = At<uint32_t>(m_state288, 0x18) != 1;
    if (fading)
        m_shadowFade = m_shadowFade > 0 ? m_shadowFade - 1 : 0;
    else
        m_shadowFade = m_shadowFade < 16 ? m_shadowFade + 1 : 16;
    Maths::Vec4 direction;
    Walk::Lerp(&direction, reinterpret_cast<const Maths::Vec4*>(light), reinterpret_cast<const Maths::Vec4*>(down),
               static_cast<float>(m_shadowFade * 0.0625f));
    static_cast<CGroundShadowModule*>(m_shadow)->SetLightDirection(&direction);
}

namespace {

constexpr uint32_t kStatNames[10] = {0x439F4704, 0xAF895B3F, 0x9B65D7B8, 0xF0D90109, 0xEDF5DB70,
                                     0x6DCB497C, 0x9016B4E7, 0xF73A13E3, 0xAE798769, 0xB1FC0722};
constexpr uint32_t kPushStyle = 0xC15DBF86;          // pushstyle
constexpr uint32_t kSkateStyle = 0x76FD33E8;         // skatestyle
constexpr uint32_t kFirstName = 0x562E3ECD;          // first_name
constexpr uint32_t kSkaterFlipAndRotate = 0x488AF07A; // SkaterFlipAndRotate (a component)

int Clock() { return FrameClock::GameClockElapsed() + g_clockOffset; }

// The score's two balance displays off (0x00563370 / 0x00563390).
void BalanceDisplaysOff(void* score)
{
    static_cast<GameModules::PointTally*>(score)->SetTeeterGauge(false, 0.0f);
    static_cast<GameModules::PointTally*>(score)->SetWheelieGauge(false, 0.0f);
}

// An input flag (a byte and the clock when it last changed) set to `on` if it isn't already.
void SetInputFlag(uint8_t* flag, uint8_t on)
{
    if (flag[0] == on)
        return;
    flag[0] = on;
    *reinterpret_cast<int*>(flag + 4) = Clock();
}

void* Elements() { return reinterpret_cast<void*>(Engine3D::g_gameTextureDict); }

} // namespace

// 0x00580BE0
void CRider::SetStatsFromProfile(CRiderIdentity* profile)
{
    void* classicStats = At<void*>(Skate(), 0x20);
    if (static_cast<GoalSystem::CMatchRules*>(static_cast<void*>(GoalSystem::g_gameMode))->IsTrue(kIsClassic)) {
        for (int i = 0; i < 10; ++i)
            SetStatOverride(i, static_cast<float>(static_cast<CRiderStoryProgress*>(classicStats)->GetInfoInteger(kStatNames[i])));
        return;
    }
    for (int i = 0; i < 10; ++i)
        SetStatOverride(i, static_cast<float>(profile->FetchTally(kStatNames[i])));
}

// 0x00580EB0
void CRider::UpdateFromProfile(CRiderIdentity* profile)
{
    MemSys::Hub* manager = MemSys::Hub::Pull();
    manager->EnterHeapScope(manager->m_skaterHeaps[m_skaterNumber]);
    auto checksum = [profile](uint32_t name) {
        return profile->GetNameKeyField(name);
    };
    m_strokeKind = checksum(kPushStyle);
    m_goofy = checksum(kStance) == kGoofy;
    m_pro = profile->IsPro();
    m_shownName = profile->FetchShownName();
    Scripts::CStruct* info = profile->InfoStructure();
    m_style = 0;
    info->FetchIntValue(kIsMale, &m_male, 1);
    info->ReadHashValue(kSkateStyle, &m_style, 0);
    const char* firstName;
    info->FetchText(kFirstName, &firstName, 1);
    for (int i = 0; (m_givenName[i] = firstName[i]) != 0; ++i) { // (copied inline, unchecked)
    }
    m_label = profile->FetchTitle();
    if (m_unknownBC) {
        uint8_t* stanceFlag = At<uint8_t*>(m_corePhysics, 0x1528) + 0x28;
        SetInputFlag(stanceFlag, m_goofy ? 0 : 1);
        static_cast<CRiderSpinAndFlip*>(FetchModule(kSkaterFlipAndRotate))->ApplyFlipToAnims();
    }
    MemSys::Hub::Pull()->LeaveHeapScope();
}

// 0x00580FF0
void CRider::ResetInput()
{
    reinterpret_cast<CRiderPointKeeper*>(m_scoreHolder)->ResetScore();
    static_cast<CRiderTeeterMoves*>(FetchModule(0x589E5FB3))->ClearBestTimes();
    reinterpret_cast<void(__thiscall*)(CRider*)>(m_vtable[0x48 / 4])(this);
    for (int i = 0; i < 24; ++i) {
        SetInputFlag(At<uint8_t*>(m_corePhysics, 0x1528) + 0x20 + 8 * i, 1);
        if (i == 7)
            At<uint8_t>(At<uint8_t*>(m_corePhysics, 0x1514), 0x1E) = 1;
        SetInputFlag(At<uint8_t*>(m_corePhysics, 0x1528) + 0x20 + 8 * i, 0);
        if (i == 7)
            At<uint8_t>(At<uint8_t*>(m_corePhysics, 0x1514), 0x1E) = 0;
    }
    reinterpret_cast<CPadState*>(m_unknown264 + 0x20)->Clear();
    Scripts::LaunchScript(0xAE5438AF, nullptr, this, false, nullptr);
}

// 0x005810E0
void CRider::ResetState()
{
    auto* physicsControl = reinterpret_cast<uint8_t*>(FetchModule(kSkaterPhysicsControl));
    if (physicsControl[0x18])
        reinterpret_cast<CRiderBoardPhysics*>(physicsControl)->SetFrozen(false);
    static_cast<CRiderSpinAndFlip*>(FetchModule(kSkaterFlipAndRotate))->ClearAfterFlags();
    auto* tricks = static_cast<CMoveExecutor*>(m_unknown258);
    tricks->ClearExtraTricks();
    tricks->ClearBlock1238();
    tricks->ClearBlock1280();
    tricks->ClearEventBuffer();
    tricks->ClearQueue();
    tricks->ResetTrickObjects();
    static_cast<CRiderPoseState*>(FetchModule(0x43686585))->ClearDoingTrick();
    m_unknown250[0x1F4] = 0;
    BalanceDisplaysOff(At<void*>(m_scoreHolder, 0x18));
    if (m_unknown25C)
        static_cast<CPadRumbleModule*>(m_unknown25C)->StopAll();
    static_cast<CGroundShadowModule*>(m_shadow)->ChangePlace(&m_atPoint);
    static_cast<CGroundShadowModule*>(m_shadow)->SetNormal(&m_basisFrame.row[1]);
    At<int>(FetchModule(0x21FC3301), 0x18) = -1;
}

// 0x005811E0
void CRider::SetUpNetwork()
{
    auto* client = reinterpret_cast<uint8_t*>(NetLink()->FetchPatron(m_skaterNumber));
    auto* flipAndRotate = static_cast<CRiderSpinAndFlip*>(FetchModule(kSkaterFlipAndRotate));
    flipAndRotate->SetBoardRotated(m_ident, false, At<uint32_t>(client, 8), m_unknownBC != 0);
    flipAndRotate->ApplyFlipToAnims();
    reinterpret_cast<CMeshInstanceModule*>(m_unknown250)->ConcealMesh(0xA7A9D4B8, false, m_unknownBC != 0);
    static_cast<CAnimBlenderComponent*>(m_unknown24C)->ResetChannels();
}

// 0x00581260
void CRider::ShowBoardElement(uint32_t type)
{
    uint32_t name;
    switch (type) {
    case 0x23A1DF80: name = 0x53409C6B; break;
    case 0x07CB4FA9: name = 0xC4BFAEC3; break;
    case 0x5B288E0B: name = 0xEF76A8DB; break;
    case 0xD0C656A2: name = 0xFFE9F52A; break;
    default: return;
    }
    Scripts::CStruct* params = Scripts::ReadSubStruct(name);
    if (!params)
        return;
    MemSys::Hub::Pull()->EnterHeapScope(MemSys::Hub::Pull()->m_topDownHeap);
    uint32_t modelSwap, modelSwap2;
    float width, height;
    const char* texture;
    params->ReadHashValue(0xA01371B1, &modelSwap, 1);
    params->FetchFloat(0x6AA8D676, &width, 1);
    params->FetchFloat(0xCCDFDDC2, &height, 1);
    params->ReadHashValue(0x676F1DF1, &modelSwap2, 1);
    params->FetchText(0xC553C612, &texture, 1);
    auto newElement = [](const char* text) {
        return static_cast<Engine3D::CTextureSet*>(Elements())->IngestSkin(text, 0, 0, true);
    };
    auto reset = [](void* element) { reinterpret_cast<void(__thiscall*)(void*, int, int)>(&Engine3D::VirtualSlot8Jump)(element, 0, 0); };
    void* element = newElement(texture);
    reset(element);
    void* parent = reinterpret_cast<void*(__thiscall*)(void*)>(CodeAddress(&GoalSystem::CObjective::FetchArgs))(FetchModule(0x5B512EDC));
    reset(parent);
    reinterpret_cast<void(__thiscall*)(void*, void*, int)>(&Engine3D::VirtualSlot15Jump)(element, parent, 1);
    reinterpret_cast<void(__thiscall*)(void*, uint32_t)>(&Engine3D::VirtualSlot19Jump)(element, 0); // (black)
    reinterpret_cast<void(__thiscall*)(void*, float, float)>(&Engine3D::VirtualSlot16Jump)(element, width, height);
    int flag;
    if (params->FetchIntValue(0xC876ADF3, &flag, 0))
        reinterpret_cast<void(__thiscall*)(void*, float, float, float)>(&Engine3D::VirtualSlot17Jump)(element, 1.5707964f, -1.0f, -1.0f);
    bool second = false;
    void* overlay = nullptr;
    int unused;
    if (params->FetchIntValue(0x25A0E7E9, &unused, 0)) {
        second = true;
        const char* overlayTexture;
        params->FetchText(0x499F4BE9, &overlayTexture, 1);
        overlay = newElement(overlayTexture);
        reset(overlay);
        reinterpret_cast<void(__thiscall*)(void*, void*, int)>(&Engine3D::VirtualSlot41Jump)(overlay, element, 1);
        reinterpret_cast<void(__thiscall*)(void*, int, int)>(&Engine3D::VirtualSlot9Jump)(overlay, 1, 0);
    }
    reinterpret_cast<void(__thiscall*)(void*, int, int)>(&Engine3D::VirtualSlot9Jump)(element, 1, 0);
    const char* model;
    params->FetchText(0x49B318FD, &model, 1);
    // (the third is a checksum where SwapTexture takes a texture name: passed on as it is)
    static_cast<CMeshInstanceModule*>(FetchModule(kModelComponent))
        ->SwapTexture(modelSwap, model, reinterpret_cast<const char*>(static_cast<uintptr_t>(modelSwap2)));
    if (second)
        static_cast<Engine3D::CTextureSet*>(Elements())->UnloadTexture(static_cast<Engine3D::CBitmapTexture*>(overlay));
    static_cast<Engine3D::CTextureSet*>(Elements())->UnloadTexture(static_cast<Engine3D::CBitmapTexture*>(element));
    reinterpret_cast<void(__thiscall*)(void*)>(&Engine3D::VirtualSlot10Jump)(parent);
    MemSys::Hub::Pull()->LeaveHeapScope();
}

// 0x005814D0
void CRider::Reset27C()
{
    if (!m_unknownBC)
        static_cast<CRiderRemoteSync*>(FetchModule(0x850EEF66))->ResetPlayback();
    At<uint32_t>(m_unknown27C, 0x18) = 0;
}

// 0x00581510
void CRider::ConcealSkater()
{
    if (m_unknownBC)
        static_cast<CRiderRollingAudio*>(FetchModule(0xB5F1E9D2))->StopSound();
    static_cast<CGroundShadowModule*>(m_shadow)->DestroyShadow();
    Conceal(true);
    m_flag2C4 = false;
}

// 0x00581550
void CRider::ShowSkater()
{
    reinterpret_cast<GameModules::Skate*>(Skate())->SetSkaterShadow(this);
    Conceal(false);
    m_flag2C4 = true;
}

// 0x00581580
const char* CRider::FetchShownName()
{
    if (NetLink()->InNetGame())
        return reinterpret_cast<const char*>(NetLink()->PlayerByObjectId(static_cast<uint16_t>(m_ident))) + 0x38;
    return m_shownName.c_str();
}

// 0x005815C0
void CRider::GlideToNode(uint32_t name)
{
    m_basisFrame.row[0] = {1.0f, 0.0f, 0.0f, 0.0f};
    m_basisFrame.row[1] = {0.0f, 1.0f, 0.0f, 0.0f};
    m_basisFrame.row[2] = {0.0f, 0.0f, 1.0f, 0.0f};
    m_basisFrame.row[3] = {0.0f, 0.0f, 0.0f, 1.0f};
    if (!name)
        return;
    Scripts::CStruct* node = LevelNodes::FetchVertex(name, !NetLink()->InNetGame());
    if (!node)
        return;
    LevelNodes::FetchPlace(name, &m_atPoint);
    m_oldPos = m_atPoint;
    At<Maths::Vec4>(m_corePhysics, 0x98) = m_atPoint;
    MarkJumpedAhead(true);
    Maths::Vec4 angles;
    LevelNodes::ReadTurnAngles(node, &angles);
    m_basisFrame.InitFromAngles(angles);
    auto* object = At<CGameEntity*>(m_corePhysics, 0xC);
    At<Maths::Mat4>(m_corePhysics, 0x18) = object->m_basisFrame;
    At<Maths::Vec4>(m_corePhysics, 0x68) = m_basisFrame.row[1];
    At<Maths::Vec4>(m_corePhysics, 0x78) = At<Maths::Vec4>(m_corePhysics, 0x68);
    At<Maths::Vec4>(m_corePhysics, 0x58) = At<Maths::Vec4>(m_corePhysics, 0x78);
    static_cast<CGroundShadowModule*>(m_shadow)->ChangePlace(&m_atPoint);
    static_cast<CGroundShadowModule*>(m_shadow)->SetNormal(&m_basisFrame.row[1]);
    m_displayMatrix = At<Maths::Mat4>(m_corePhysics, 0x18);
}

// 0x00581760
void CRider::Freeze()
{
    if (m_isHalted || !m_unknownBC)
        return;
    m_isHalted = true;
    m_compositeFlags |= 1;
    BalanceDisplaysOff(At<void*>(m_scoreHolder, 0x18));
    Scripts::LaunchScript(0x1277E3FD, nullptr, nullptr, false, nullptr);
    if (uint8_t* input = m_unknown264)
        At<Sio::InputPort*>(At<uint8_t*>(input, 0x490), 0x1C)->PauseActuators();
    SoundFx::g_sfxManager->UpdateVoiceVolumes();
}

// 0x005817F0
void CRider::Unpause()
{
    if (!m_isHalted || !m_unknownBC)
        return;
    m_isHalted = false;
    m_compositeFlags &= ~1u;
    if (uint8_t* input = m_unknown264)
        At<Sio::InputPort*>(At<uint8_t*>(input, 0x490), 0x1C)->UnpauseActuators();
    if (auto* component = reinterpret_cast<uint8_t*>(FetchModule(0xB5F1E9D2)))
        component[0xFE] = 1;
}

namespace {

constexpr uint32_t kComponentName = 0xB6015EA8; // component
constexpr uint32_t kNameChecksum = 0xA1DC81F9;  // name
constexpr uint32_t kCustomSkater = 0x0A7BE964;  // custom

// A component made from a structure the caller fills (CreateComponentFromStructure, vtable +0x24), the structure
// freed after.
template <typename Fill>
void AddComponentFromStructure(CRider* skater, Fill fill)
{
    Scripts::CStruct* memory = Scripts::NewStruct();
    Scripts::CStruct* params = memory ? memory->PrepareAll() : nullptr;
    fill(params);
    skater->VirtualCreateComponentFromStructure(params, nullptr);
    if (params) {
        params->Clear();
        Scripts::DeleteStruct(params);
    }
}

} // namespace

// 0x00581850
void CRider::PrepareAll(CRiderIdentity* profile)
{
    uint8_t* skate = Skate();
    MemSys::Hub* manager = MemSys::Hub::Pull();
    if (m_unknownBC) {
        // Its camera: found, or made from skatercam_composite_structure.
        manager->EnterHeapScope(manager->m_skaterInfoHeap);
        uint32_t cameraName = m_viewSlot == 1 ? kSkaterCam1 : kSkaterCam0;
        auto* camera = static_cast<CGameEntity*>(CompositeObjectManager()->LookUpEntity(cameraName));
        if (camera) {
            static_cast<CRiderChaseView*>(camera->FetchModule(0x5E43A604))->BindRider(this); // skatercamera
            auto* walkCamera = reinterpret_cast<uint8_t*>(camera->FetchModule(0xC7D37EC2));                     // walkcamera
            reinterpret_cast<CFootView*>(walkCamera)->BindRider(this);
            *reinterpret_cast<int*>(walkCamera + 0x18) = 3;
        } else {
            Scripts::CStruct* memory = Scripts::NewStruct();
            Scripts::CStruct* params = memory ? memory->PrepareAll() : nullptr;
            params->PutNameKey(0x4C48DA58, m_ident);
            params->PutNameKey(kNameChecksum, cameraName);
            camera = CompositeObjectManager()->SpawnEntityFromNode(Scripts::FetchList("skatercam_composite_structure"),
                                                                             params, true);
            if (params) {
                params->Clear();
                Scripts::DeleteStruct(params);
            }
        }
        static_cast<CFootMovement*>(FetchModule(0x726E85AA))->ChooseCamera(camera); // Walk
        ScriptCommands::MakeCameraActive(cameraName, m_viewSlot, false);
        At<uint32_t>(camera, 8) |= 2;
        static_cast<CRiderChaseView*>(camera->FetchModule(0x5E43A604))->ChangeRegime(2, 0.0f);
        MemSys::Hub::Pull()->LeaveHeapScope();
    }
    // Its script; its anim set (skater_asset_info's skater_skeleton, else thps6_human).
    void* script = Scripts::CScript::operator new(0xF4);
    m_runScript = script ? static_cast<Scripts::CScript*>(script)->MakeNew() : nullptr;
    uint32_t skeleton = 0x9FE476E2;
    if (Scripts::CStruct* assetInfo = Scripts::ReadSubStruct(0x962E5CF7))
        assetInfo->ReadHashValue(0xCEEFD662, &skeleton, 0);
    AddMotionComponents(this);
    AddComponentFromStructure(this, [skeleton](Scripts::CStruct* params) {
        params->PutNameKey(kComponentName, 0x7149B753); // animblender
        params->PutNameKey(0x09794932, skeleton);        // skeletonName
        params->PutNameKey(0x5B8C6DC2, 0x3FEC31A6);      // AnimEventTableName SkaterAnimEventTable
    });
    m_unknown24C = FetchModule(0x7149B753);
    AddComponentFromStructure(this, [skeleton](Scripts::CStruct* params) {
        params->PutNameKey(kComponentName, 0x222756D5); // skeleton
        params->PutNameKey(0x222756D5, skeleton);
        params->PutInt(0xD3982061, 0); // max_bone_skip_lod
    });
    static_cast<CAnimBlenderComponent*>(m_unknown24C)->SetFlag4C(true);
    bool local = m_unknownBC != 0;
    AddComponentFromStructure(this, [local](Scripts::CStruct* params) {
        params->PutNameKey(0u, 0x10079F2D);       // UseModelLights
        params->PutInt(0xD223B8BD, 1);         // max_scene_lights
        params->PutInt(0x2A1F92C0, local ? 1 : 0);
        params->PutNameKey(kComponentName, kModelComponent);
    });
    m_unknown250 = reinterpret_cast<uint8_t*>(FetchModule(kModelComponent));
    // Its geometry, in its geometry heap, with the asset manager's flag off meanwhile.
    MemSys::Hub::Pull()->EnterHeapScope(MemSys::Hub::Pull()->m_skaterGeomHeaps[m_skaterNumber]);
    auto* assets = AssetStore::g_assetManager;
    bool flag = assets->GetPermanentByDefault();
    assets->SetPermanentByDefault(false);
    reinterpret_cast<void(__thiscall*)(void*, CRiderIdentity*, int, int, int)>((*reinterpret_cast<void***>(m_unknown250))[0x34 / 4])(
        m_unknown250, profile, 0, m_skaterNumber, 0);
    assets->SetPermanentByDefault(flag);
    MemSys::Hub::Pull()->LeaveHeapScope();
    AddComponentFromStructure(this, [](Scripts::CStruct* params) {
        params->PutNameKey(kComponentName, 0x8A897DD2); // shadow
        params->PutNameKey(0x9AC24B18, 0x76A54CD1);     // ShadowType
    });
    m_shadow = FetchModule(0x8A897DD2);
    At<uint8_t>(m_shadow, 0x44) = 0;
    if (m_unknownBC) {
        AddComponentFromStructure(this, [](Scripts::CStruct* params) { params->PutNameKey(kComponentName, 0x404EB936); }); // vibration
        m_unknown25C = FetchModule(0x404EB936);
        AddComponentFromStructure(this, [](Scripts::CStruct* params) { params->PutNameKey(kComponentName, 0x21FC3301); }); // SkaterStancePanel
    } else {
        m_unknown25C = nullptr;
    }
    if (m_unknownBC) {
        AddComponentFromStructure(this, [](Scripts::CStruct* params) { params->PutNameKey(kComponentName, 0x187574FA); }); // skatersound
        m_unknown268 = FetchModule(0x187574FA);
        m_unknown26C = FetchModule(0x1859F772); // SkaterVehicleSound
    }
    reinterpret_cast<void(__thiscall*)(CRider*)>(m_vtable[0x20 / 4])(this); // Finalize
    if (m_unknownBC) {
        // The pad's settings for its viewport (the skate module's, 8 bytes each from +0x394).
        At<uint8_t>(m_corePhysics, 0x2D9) = skate[0x395 + 8 * m_viewSlot];
        At<uint8_t>(m_corePhysics, 0x2DA) = skate[0x396 + 8 * m_viewSlot];
        static_cast<CPadRumbleModule*>(m_unknown25C)->ChangeLive(skate[0x394 + 8 * m_viewSlot] != 0);
        if (m_unknownBC)
            m_runScript->BindScript("SkaterInit", nullptr, this);
    }
    UpdateFromProfile(profile);
    SetStatsFromProfile(profile);
    if (m_unknownBC) {
        float maxSpeed = GetScaledPhysics(0xCC5F87AA); // Skater_Max_Max_Speed_Stat
        At<float>(FetchModule(0x187574FA), 0x20) = maxSpeed;
    }
    if (void* state = m_unknown258) {
        static_cast<CMoveExecutor*>(state)->SetTrickMappings(profile);
        auto* tricks = static_cast<CMoveExecutor*>(m_unknown258);
        tricks->ClearEventBuffer();
        tricks->ClearQueue();
        tricks->ClearBlock1238();
        tricks->ClearBlock1280();
        At<void*>(m_unknown258, 0x1C) = At<void*>(m_scoreHolder, 0x18);
    }
    if (uint8_t* other = m_unknown270)
        At<void*>(other, 0x44) = At<void*>(m_scoreHolder, 0x18);
    if (CObjectModule* collision = FetchModule(0xA432DC41)) {
        // (its +0x18, read through the folded getter 0x005480F0)
        if (At<uint8_t*>(collision, 0x18))
            At<uint8_t*>(collision, 0x18)[8] |= 2;
    }
    reinterpret_cast<CMeshInstanceModule*>(m_unknown250)->JumpTo(); // the model's update
    Scripts::LaunchScript(0xAE5438AF, nullptr, this, false, nullptr);
    static_cast<CAnimBlenderComponent*>(m_unknown24C)->PlayAnimation(0x1CA1FF20, std::bit_cast<uint32_t>(0.3f)); // default
    if (m_unknownBC) {
        for (int i = 0; i < 11; ++i) {
            void* memory = MemSys::Alloc(0x10);
            m_trickInfo[i] = memory ? reinterpret_cast<uint8_t*>(static_cast<GoalSystem::CCreateATrick*>(memory)->MakeNew()) : nullptr;
        }
        Scripts::LaunchScript(0x0BDC1C89, nullptr, this, false, nullptr); // spawn_add_premade_cats_to_skater
        CGameEntity::InvokeMethodByName(0x5C038F9B, nullptr, nullptr); // SkaterPhysicsControl_SwitchWalkingToSkating
        for (int i = 0; i < 5; ++i) {
            char name[32];
            std::snprintf(name, sizeof(name), "projectile_%i", i);
            m_projectiles[i].name = Checksums::HashFromText(name);
            m_projectiles[i].active = false;
        }
    }
    AddRigidBodyPusher(this);
    // Its controls' tag (a custom skater's own, if it has one) and sticker.
    Scripts::CStruct* info = profile->InfoStructure();
    const char* tag;
    info->FetchText(0x4FA8DF27, &tag, 1); // tag_texture
    CObjectModule* controls = FetchModule(0x5B512EDC);
    if (m_label == kCustomSkater) {
        int hasCustomTag;
        info->FetchIntValue(0xB78D4AAE, &hasCustomTag, 1);
        if (hasCustomTag)
            static_cast<CTaggingComponent*>(controls)->ApplyCustomTagTexture();
        else
            static_cast<CTaggingComponent*>(controls)->SetGraffitiTexture(tag);
    } else {
        static_cast<CTaggingComponent*>(controls)->SetGraffitiTexture(tag);
    }
    const char* sticker;
    if (info->FetchText(0x130618A0, &sticker, 0)) // sticker_texture
        static_cast<CTaggingComponent*>(controls)->SetStickerTexture(sticker);
}

// 0x005822A0
void CRider::InitFromProfile(CRiderIdentity* profile)
{
    PrepareAll(profile);
    OnlinePlay::Hub* net = NetLink();
    if (m_unknownBC && static_cast<uint8_t>(ScriptCommands::CFunc_InMultiplayerGame(nullptr, nullptr)) != 0) {
        // Its network messages.
        auto* client = reinterpret_cast<uint8_t*>(net->FetchPatron(m_viewSlot));
        auto* dispatcher = reinterpret_cast<NetLink::Router*>(client + 0xC);
        const struct {
            uint8_t id;
            void* code;
        } handlers[] = {{0x3B, CodeAddress(&GameModules::PointTally::HandleScoreMessage)},
                        {0x44, CodeAddress(&CRiderFrameLog::HandleCollisionMessages)},
                        {0x43, CodeAddress(&CRiderFrameLog::HandleCollisionMessages)},
                        {0x85, CodeAddress(&CRiderFrameLog::HandleHitMessages)},
                        {0x84, CodeAddress(&CRiderFrameLog::HandleHitMessages)},
                        {0x4B, CodeAddress(&CRiderHostSync::HandleGraffitiSteal)}};
        for (const auto& handler : handlers)
            dispatcher->AppendListener(handler.id, reinterpret_cast<NetLink::HandlerSlotValue>(handler.code), 0, this, 0x80);
    }
    // Never suspended (its suspend component's distances).
    if (auto* suspend = reinterpret_cast<uint8_t*>(FetchModule(0xCE0CA665))) {
        At<float>(suspend, 0x34) = 3000.0f;
        At<float>(suspend, 0x38) = 5000.0f;
        At<float>(suspend, 0x3C) = 10000000.0f;
        At<float>(suspend, 0x40) = 10000000.0f;
    }
}

// 0x005823A0
void CRider::RunStateScript(bool walk, bool other)
{
    if (!m_runScript) {
        void* memory = Scripts::CScript::operator new(0xF4);
        m_runScript = memory ? static_cast<Scripts::CScript*>(memory)->MakeNew() : nullptr;
    }
    Scripts::CStruct* params = nullptr;
    if (walk || other) {
        Scripts::CStruct* memory = Scripts::NewStruct();
        params = memory ? memory->PrepareAll() : nullptr;
        if (walk)
            params->PutNameKey(0u, 0x726E85AA); // Walk
        if (other)
            params->PutNameKey(0u, 0x7B738580);
    }
    m_runScript->BindScript(0xE740F458, params, this);
    if (m_unknownBC)
        m_runScript->TickFrame();
    if (params) {
        params->Clear();
        Scripts::DeleteStruct(params);
    }
}

// 0x00582510
void CRider::SwitchToScript(uint32_t name, Scripts::CStruct* params)
{
    if (!m_runScript) {
        void* memory = Scripts::CScript::operator new(0xF4);
        m_runScript = memory ? static_cast<Scripts::CScript*>(memory)->MakeNew() : nullptr;
    }
    if (CObjectModule* flipAndRotate = FetchModule(kSkaterFlipAndRotate))
        static_cast<CRiderSpinAndFlip*>(flipAndRotate)->DoAfterAnim();
    m_runScript->BindScript(name, params, this);
    m_runScript->TickFrame();
}

// 0x005825E0
void CRider::WriteCATInfo(Scripts::CStruct* save)
{
    Scripts::CArray* memory = Scripts::NewArray();
    Scripts::CArray* tricks = memory ? memory->PrepareAll() : nullptr;
    tricks->ConfigureSizeKind(11, Scripts::QBVAL_STRUCT);
    for (int i = 0; i < 11; ++i) {
        Scripts::CStruct* structMemory = Scripts::NewStruct();
        Scripts::CStruct* trick = structMemory ? structMemory->PrepareAll() : nullptr;
        uint8_t* info = m_trickInfo[i];
        trick->PutSubStruct(kOther, At<Scripts::CStruct*>(info, 4));
        trick->AppendArrayValue(kAnimations, At<Scripts::CArray*>(info, 0xC));
        trick->AppendArrayValue(kRotations, At<Scripts::CArray*>(info, 8));
        tricks->SetElement(i, reinterpret_cast<uint32_t>(trick));
    }
    save->AppendArrayRef(kTricks, tricks);
}

// 0x00582740
void CRider::WriteList44B66A9F(Scripts::CStruct* save)
{
    Scripts::CArray* memory = Scripts::NewArray();
    Scripts::CArray* entries = memory ? memory->PrepareAll() : nullptr;
    entries->ConfigureSizeKind(0x46, Scripts::QBVAL_INTEGER);
    for (int i = 0; i < 0x46; ++i)
        entries->SetElement(i, static_cast<uint32_t>(static_cast<CAttributeTracker*>(m_list28C)->GetGoalStatus(i)));
    save->AppendArrayRef(kList44B66A9F, entries);
}

// 0x00582800
void CRider::LaunchProjectile()
{
    // Its flat heading.
    float headingX = m_basisFrame.row[2].x, headingY = 0.0f, headingZ = m_basisFrame.row[2].z;
    float best = 1000.0f;
    CGameEntity* target = nullptr;
    double length = std::sqrt(static_cast<double>(headingZ) * headingZ + static_cast<double>(headingX) * headingX);
    if (length > 0.0) {
        double inverse = 1.0 / length;
        headingX = static_cast<float>(headingX * inverse);
        headingY = static_cast<float>(0.0 * inverse);
        headingZ = static_cast<float>(headingZ * inverse);
    }
    // From a wrist (the right one, or the left by its stance and whether it's walking).
    uint8_t* walking = At<uint8_t*>(m_state284, 0x2C);
    bool flipped = At<uint8_t>(At<uint8_t*>(m_corePhysics, 0x1528), 0x28) != 0;
    bool walk = !At<bool>(walking, 0x118) && At<int>(walking, 0x1C) == 1;
    uint32_t wrist = (walk ? flipped : !flipped) ? 0x7EE14CFE : 0x84EE719D; // Bone_Wrist_L / Bone_Wrist_R
    Maths::Vec4 bone, rotated;
    static_cast<CBoneRigModule*>(FetchModule(0x222756D5))->GetLocalBonePosition(wrist, &bone);
    const Maths::Vec4* turned = &m_basisFrame.RotateVector(rotated, bone);
    const Maths::Vec4 origin = {static_cast<float>(static_cast<double>(turned->x) + m_atPoint.x),
                                static_cast<float>(static_cast<double>(turned->y) + m_atPoint.y),
                                static_cast<float>(static_cast<double>(turned->z) + m_atPoint.z),
                                static_cast<float>(static_cast<double>(turned->w) + m_atPoint.w)};
    // The nearest projectile target (projectileproximity) ahead within the targeting distance and angle; a ped
    // that's been shot is left out, and a ped is aimed at a bone it chooses.
    float toTargetX = 0.0f, toTargetZ = 0.0f;
    Maths::Vec4 aim = {};
    for (CObjectModule* component = CompositeObjectManager()->FindFirstModuleOfKind(0x3618B878); component;
         component = component->mp_nextOfType) {
        CGameEntity* object = component->m_entity;
        if (!object)
            continue;
        bool ped = false;
        if (object->FetchModule(0x6EFDDFB8)) { // PedLogic
            uint32_t state = static_cast<CPedestrianBrainModule*>(object->FetchModule(0x6EFDDFB8))->GetLogicState();
            ped = true;
            if (state == 0x54F87744) // shot
                continue;
        }
        double dz = static_cast<double>(object->m_atPoint.z) - origin.z;
        double dy = static_cast<double>(object->m_atPoint.y) - origin.y;
        double dx = static_cast<double>(object->m_atPoint.x) - origin.x;
        float distance = static_cast<float>(std::sqrt(dx * dx + dy * dy + dz * dz));
        float toZ = static_cast<float>(dz);
        float toX = static_cast<float>(dx);
        double toY = 0.0;
        float flat = static_cast<float>(std::sqrt(static_cast<double>(toX) * toX + static_cast<double>(toZ) * toZ));
        if (flat > 0.0f) {
            float inverse = static_cast<float>(1.0 / flat);
            toX = static_cast<float>(static_cast<double>(toX) * inverse);
            toY = static_cast<double>(inverse) * 0.0;
            toZ = static_cast<float>(static_cast<double>(toZ) * inverse);
        }
        float dot = static_cast<float>(static_cast<double>(toZ) * headingZ + toY * headingY + static_cast<double>(toX) * headingX);
        if (!(Scripts::FetchFloat(0x39186247) > distance)) // Physics_Projectile_Targeting_Dist
            continue;
        double cone = std::cos(Scripts::FetchFloat(0x195A7DBA) * static_cast<double>(0.017453292f)); // ..._Angle
        if (!(cone < dot))
            continue;
        if (!(distance < best))
            continue;
        target = object;
        best = distance;
        toTargetX = toX;
        toTargetZ = toZ;
        if (ped) {
            Scripts::LaunchScript(0x5DB74F79, nullptr, nullptr, false, nullptr); // choose_projectile_target_bone
            uint32_t targetBone = Scripts::GetSymbolValue(0x07A15639);       // target_bone
            Maths::Vec4 at;
            static_cast<CBoneRigModule*>(object->FetchModule(0x222756D5))->GetLocalBonePosition(targetBone, &at);
            // (turned by the object's rotation in place: 0x00411840, Nx's VectorTurn::TurnBy)
            Maths::Vec4 turnedAt;
            object->m_basisFrame.RotateVector(turnedAt, at);
            at = turnedAt;
            aim = {static_cast<float>(static_cast<double>(at.x) + object->m_atPoint.x),
                   static_cast<float>(static_cast<double>(at.y) + object->m_atPoint.y),
                   static_cast<float>(static_cast<double>(at.z) + object->m_atPoint.z),
                   static_cast<float>(static_cast<double>(at.w) + object->m_atPoint.w)};
        } else {
            aim = object->m_atPoint;
        }
    }
    // LaunchProjectile from there (at the target, when there is one beyond 100).
    Scripts::CStruct* memory = Scripts::NewStruct();
    Scripts::CStruct* params = memory ? memory->PrepareAll() : nullptr;
    params->PutVec3("pos", {origin.x, origin.y, origin.z, origin.w});
    if (target && best > 100.0f) {
        params->PushFloat("ped_dist", best);
        params->PushFloat("heading_x", toTargetX);
        params->PushFloat("heading_z", toTargetZ);
        params->PutVec3("ped_pos", {aim.x, aim.y, aim.z, aim.w});
    }
    RunSubScript(0x86919CB6, params); // LaunchProjectile
    if (params) {
        params->Clear();
        Scripts::DeleteStruct(params);
    }
}

// 0x00582D90
CRider* CRider::MakeNew(int viewport, CEntityHub* manager, bool local, uint32_t id, int skaterNumber,
                            int /*unused*/)
{
    CGameEntity::MakeNew();
    m_vtable = kSkaterVtable;
    new (&m_shownName) TextUtil::Chars();
    // Its node in the skater list (ListKit::Elem: vtable, data, priority, next, prev).
    auto* node = reinterpret_cast<uint32_t*>(m_block238);
    node[0] = reinterpret_cast<uint32_t>(GameModules::kSkaterNodeVtable);
    node[1] = reinterpret_cast<uint32_t>(this);
    node[2] = 0;
    node[3] = reinterpret_cast<uint32_t>(node);
    node[4] = reinterpret_cast<uint32_t>(node);
    m_unknownBC = local;
    AddBaseComponents(this);
    m_kindTag = 0xBAF9B341;
    m_flag2C4 = false;
    m_skaterNumber = skaterNumber;
    m_ident = id;
    if (skaterNumber == 0)
        Tracker()->RegisterNickname(0x5B8AB877, this);
    else if (skaterNumber == 1)
        Tracker()->RegisterNickname(0x06ED3FA7, this);
    At<int>(this, 0x30) = -500;
    reinterpret_cast<void(__thiscall*)(CEntityHub*, CRider*)>((*reinterpret_cast<void***>(manager))[1])(manager, this);
    m_viewSlot = viewport;
    m_unknown31C = 0;
    DebugOut::Printf("D:%s:%i: m_skater_number = %i\n", "jects\\skater.cpp", 0x43C, viewport);
    DebugOut::Printf("D:%s:%i: m_id = %i\n", "jects\\skater.cpp", 0x43D, m_ident);
    m_unknown24C = nullptr;
    m_unknown250 = nullptr;
    m_shadow = nullptr;
    m_unknown258 = nullptr;
    m_unknown25C = nullptr;
    m_unknown264 = nullptr;
    m_unknown268 = nullptr;
    m_unknown270 = nullptr;
    m_scoreHolder = nullptr;
    m_corePhysics = nullptr;
    m_list28C = nullptr;
    m_state288 = nullptr;
    m_state284 = nullptr;
    // Its components, one structure (component = ...) changed for each.
    Scripts::CStruct* memory = Scripts::NewStruct();
    Scripts::CStruct* params = memory ? memory->PrepareAll() : nullptr;
    auto add = [this, params](uint32_t type) {
        params->PutNameKey(kComponentName, type);
        CreateComponentFromStructure(params, nullptr);
    };
    add(0x43686585);
    add(0xF27A294D);
    if (m_unknownBC) {
        add(0x27D7CD28);
        m_unknown264 = reinterpret_cast<uint8_t*>(FetchModule(0x27D7CD28));
        add(0xD262802F);
        m_scoreHolder = reinterpret_cast<uint8_t*>(FetchModule(0xD262802F));
        add(0xBF518E27);
        add(0x270F56E1);
        m_unknown258 = FetchModule(0x270F56E1);
        add(0xD9151D4D); // SkaterPhysicsControl
        m_state284 = reinterpret_cast<uint8_t*>(FetchModule(0xD9151D4D));
        add(0x5BD63B29); // SkaterCorePhysics
        m_corePhysics = reinterpret_cast<uint8_t*>(FetchModule(0x5BD63B29));
        add(0x52D61A0D);
        add(0x11B4C67F);
        m_unknown270 = reinterpret_cast<uint8_t*>(FetchModule(0x11B4C67F));
        add(0x9F9CC949);
        add(0xE594F0A2);
        add(0x9B373E58);
        add(0xD8CC3428);
        add(0x726E85AA); // Walk
        m_state288 = reinterpret_cast<uint8_t*>(FetchModule(0x726E85AA));
        add(0x7CD3E6D5);
        add(0xB1D5A3E1);
        add(0x589E5FB3);
        add(0xB5F1E9D2);
        add(0x1859F772); // SkaterVehicleSound
        add(0x4FF8DAC6);
        m_list28C = FetchModule(0x4FF8DAC6);
        add(0x408D2AA9);
        add(0x8BCE7E66);
        add(0x380D909A);
    } else {
        add(0xD262802F);
        m_scoreHolder = reinterpret_cast<uint8_t*>(FetchModule(0xD262802F));
        add(0x850EEF66);
        add(0xD8CC3428);
        add(0x125044E2);
        add(0xB1D5A3E1);
        add(0xB5F1E9D2);
    }
    add(0x18223FD6);
    m_unknown27C = reinterpret_cast<uint8_t*>(FetchModule(0x18223FD6));
    add(kSkaterFlipAndRotate);
    // The controls, told its skater number.
    Scripts::CStruct* extraMemory = Scripts::NewStruct();
    Scripts::CStruct* extra = extraMemory ? extraMemory->PrepareAll() : nullptr;
    extra->PutInt(0xA2B033FD, m_skaterNumber);
    params->PutNameKey(kComponentName, 0x5B512EDC);
    CreateComponentFromStructure(params, extra);
    add(0xD1515392);
    if (extra) {
        extra->Clear();
        Scripts::DeleteStruct(extra);
    }
    if (params) {
        params->Clear();
        Scripts::DeleteStruct(params);
    }
    return this;
}

// 0x00583400
void CRider::ResetComponents()
{
    if (m_unknownBC) {
        reinterpret_cast<CRiderGapTracker*>(m_unknown270)->ClearGaps();
        static_cast<CRiderTeeterMoves*>(FetchModule(0x589E5FB3))->ClearParams();
    }
    static_cast<CPickupItemHandler*>(FetchModule(0xF27A294D))->DestroyItems();
    static_cast<CAnimBlenderComponent*>(m_unknown24C)->ResetChannels();
    if (Scripts::CScript* script = m_runScript) {
        script->Teardown();
        Scripts::CScript::operator delete(script);
        m_runScript = nullptr;
    }
    // Not original - the special items gone, THAW's bike with them: off the bike (game/skater/thaw_bmx.h).
    if (Skater::ThawBmx::IsSwitchedOn())
        Skater::ThawBmx::OnSkaterReset(this);
}

// 0x00583470
void CRider::Relaunch(uint32_t a, uint32_t b)
{
    uint8_t* walking = At<uint8_t*>(m_state284, 0x2C);
    if (!At<bool>(walking, 0x118) && At<int>(walking, 0x1C) == 1) {
        // Walking: back to skating first.
        Scripts::CStruct* memory = Scripts::NewStruct();
        Scripts::CStruct* params = memory ? memory->PrepareAll() : nullptr;
        params->PutNameKey(0x91D0D784, 0x1DDE4804);
        SwitchToScript(0x93A4E402, params);
        if (params) {
            params->Clear();
            Scripts::DeleteStruct(params);
        }
    }
    if (m_runScript)
        m_runScript->RunOnExitScripts();
    reinterpret_cast<Skater::CorePhysicsComponent*>(m_corePhysics)->Reset();
    auto* camera = static_cast<CGameEntity*>(CompositeObjectManager()->LookUpEntity(m_viewSlot == 1 ? kSkaterCam1 : kSkaterCam0));
    static_cast<CRiderChaseView*>(camera->FetchModule(0x5E43A604))->RestoreDefaultState();
    reinterpret_cast<void(__thiscall*)(CRider*)>(m_vtable[0x44 / 4])(this);
    ResetComponents();
    reinterpret_cast<void(__thiscall*)(CRider*)>(m_vtable[0x48 / 4])(this);
    reinterpret_cast<void(__thiscall*)(CRider*, uint32_t, uint32_t)>(m_vtable[0x4C / 4])(this, a, b);
    static_cast<CRiderSessionEnd*>(FetchModule(0xB1D5A3E1))->ClearForced();
}

namespace {

// The restart (vtable +0x3C): skipped to the restart node `node`, walking or not.
void SkipToRestartVirtual(CRider* skater, uint32_t node, uint32_t walking)
{
    reinterpret_cast<void(__thiscall*)(CRider*, uint32_t, uint32_t)>(skater->m_vtable[0x3C / 4])(skater, node, walking);
}

// What the script gives back (its +0x14).
Scripts::CStruct* ScriptResults(Scripts::CScript* script) { return At<Scripts::CStruct*>(script, 0x14); }

} // namespace

// The custom restart's position and orientation put back (SkipToCustomRestart, and SetCustomRestart Set).
void CRider::SkipToCustomRestart()
{
    SkipToRestartVirtual(this, 0, m_customRestartWalking);
    m_atPoint = m_customRestartPos;
    m_basisFrame = m_customRestartMatrix;
    At<Maths::Vec4>(m_corePhysics, 0x98) = m_atPoint;
    m_oldPos = m_atPoint;
    MarkJumpedAhead(true);
    At<Maths::Mat4>(m_corePhysics, 0x18) = At<CGameEntity*>(m_corePhysics, 0xC)->m_basisFrame;
    At<Maths::Vec4>(m_corePhysics, 0x68) = m_basisFrame.row[1];
    At<Maths::Vec4>(m_corePhysics, 0x78) = At<Maths::Vec4>(m_corePhysics, 0x68);
    At<Maths::Vec4>(m_corePhysics, 0x58) = At<Maths::Vec4>(m_corePhysics, 0x78);
    static_cast<CGroundShadowModule*>(m_shadow)->ChangePlace(&m_atPoint);
    static_cast<CGroundShadowModule*>(m_shadow)->SetNormal(&m_basisFrame.row[1]);
}

// 0x005835D0
bool CRider::InvokeMethodByName(uint32_t name, Scripts::CStruct* params, Scripts::CScript* script)
{
    switch (name) {
    case 0x1B572ABF: { // KillSkater
        // (the original keeps the flag in the low byte of its params slot: the restart is told that word)
        uint32_t walking = (reinterpret_cast<uint32_t>(params) & 0xFFFFFF00u) | (params->HasEntryNamed(0xD6F06BF6) ? 1u : 0u);
        uint32_t prefix = 0;
        const char* text = nullptr;
        if (!params->ReadHashValue(0x6C4E7971, &prefix, 0) && params->FetchText(0x6C4E7971, &text, 0)) // prefix
            prefix = Checksums::HashFromText(text);
        if (prefix) {
            Scripts::CStruct* node = LevelNodes::FindClosestNodeWithPrefix(prefix, &m_atPoint);
            SkipToRestartVirtual(this, LevelNodes::GetNodeName(node), walking);
            return true;
        }
        uint32_t node = 0;
        if (params->ReadHashValue(0x7A8017BA, &node, 0)) { // node
            SkipToRestartVirtual(this, node, walking);
            return true;
        }
        // The first restart linked from the script's node; none: the skate module's (unless in the park editor).
        uint32_t from = At<uint32_t>(script, 0xB8);
        int count = static_cast<int>(LevelNodes::GetLinkCount(from));
        int i = 0;
        for (; i < count; ++i) {
            uint32_t link = LevelNodes::FetchLink(from, i);
            if (IsRestartNode(link)) {
                SkipToRestartVirtual(this, link, walking);
                break;
            }
        }
        if (i == count && At<uint32_t>(reinterpret_cast<uint8_t*>(ParkEditing::g_parkEditor), 0x40) != 0xD443A2BC)
            reinterpret_cast<GameModules::Skate*>(Skate())->TeleportToSpawn(this, 0, walking);
        return true;
    }
    case 0x0C09C1B7: // AddSkaterToWorld
        reinterpret_cast<GameModules::Skate*>(Skate())->ConcealSkater(this, false);
        return true;
    case 0xBCE85494: // RemoveSkaterFromWorld
        reinterpret_cast<GameModules::Skate*>(Skate())->ConcealSkater(this, true);
        return true;
    case 0x1C5612EE: { // SkaterIsNamed
        uint32_t named;
        params->ReadHashValue(0u, &named, 1);
        return named == m_label;
    }
    case 0x3E8EB713: // ShouldMongo
        if (!m_unknownBC)
            return false;
        if (m_strokeKind == 0x0CDBA4F9) // mongo_when_switch
            return (reinterpret_cast<Skater::CorePhysicsComponent*>(m_corePhysics)->IsSidesSwapped() & 0xFF) != 0;
        return m_strokeKind == 0x16C6DF0B; // always_mongo
    case 0x3F06191C: // LaunchDirtyProjectile
        DebugOut::Printf("LaunchDirtyProjectile\n");
        LaunchProjectile();
        return true;
    case 0x5436A335: // SparksOff
        Scripts::LaunchScript(kSparksOff, params, this, false, nullptr);
        m_sparks = false;
        return true;
    case 0xE62F94A2: // SparksOn
        Scripts::LaunchScript(kSparksOn, params, this, false, nullptr);
        m_sparks = true;
        params->HasBareFlag("RailNotRequired"); // (read, unused)
        return true;
    case 0x42832375: // SetCustomRestart
        if (params->HasBareFlag(0x19EBDA23)) { // Set: here and now - and skipped to
            m_customRestartSet = true;
            m_customRestartPos = m_atPoint;
            m_customRestartMatrix = m_basisFrame;
            m_customRestartWalking = reinterpret_cast<CRiderBoardPhysics*>(m_state284)->IsOnFoot();
            SkipToCustomRestart();
            return true;
        }
        if (params->HasBareFlag(0x1A4E0EF9)) { // clear
            m_customRestartSet = false;
            return true;
        }
        return m_customRestartSet;
    case 0x5A3C19E9: // SkipToCustomRestart
        SkipToCustomRestart();
        return true;
    case 0x7015BC33: // IsLocalSkater
        return m_unknownBC != 0;
    case 0x72AECFC0: // ResetRigidBodyCollisionRadiusBoost
        SetRigidBodyPusherReach(this, 0.0f);
        return true;
    case 0x97C8285A: // GetCameraId
        ScriptResults(script)->PutNameKey(0x7A039889, m_viewSlot == 1 ? kSkaterCam1 : kSkaterCam0);
        return true;
    case 0x7915AA31: { // FetchTally
        int stat;
        params->FetchIntValue(0xDF4700DE, &stat, 1);
        Scripts::CStruct* results = ScriptResults(script);
        results->PushFloat(0x8EAF7ADD, FetchTally(stat)); // stat_value
        return true;
    }
    case 0x9ABE8A21: { // GetScriptedStat
        Scripts::CStruct* stat = nullptr;
        params->ReadSubStruct(0u, &stat, 1);
        Scripts::CStruct* results = ScriptResults(script);
        results->PushFloat(0x8EAF7ADD, GetScaledValue(stat));
        return true;
    }
    case 0xBF450BAB: // MoveToRandomRestart
        MoveToRandomRestart();
        return true;
    case 0xD38E8B6C: { // PlaceBeforeCamera: 144 in front of the camera, facing it, moving away at 10
        if (!Engine3D::FetchLiveCamera(m_viewSlot))
            return true;
        auto* camera = Engine3D::FetchLiveCamera(m_viewSlot);
        const Maths::Vec4* cameraPos = camera->FetchLocation();
        camera = Engine3D::FetchLiveCamera(m_viewSlot);
        const Maths::Mat4* cameraMatrix = camera->FetchMat();
        m_basisFrame.ResetToIdentity();
        m_atPoint = *cameraPos;
        m_atPoint -= cameraMatrix->row[2] * 144.0f;
        m_oldPos = m_atPoint;
        At<Maths::Vec4>(m_corePhysics, 0x98) = m_atPoint;
        m_basisFrame = *cameraMatrix;
        m_basisFrame.row[2] = -m_basisFrame.row[2];
        m_basisFrame.row[0] = -m_basisFrame.row[0];
        At<Maths::Mat4>(m_corePhysics, 0x18) = At<CGameEntity*>(m_corePhysics, 0xC)->m_basisFrame;
        At<Maths::Vec4>(m_corePhysics, 0x68) = m_basisFrame.row[1];
        At<Maths::Vec4>(m_corePhysics, 0x78) = At<Maths::Vec4>(m_corePhysics, 0x68);
        At<Maths::Vec4>(m_corePhysics, 0x58) = At<Maths::Vec4>(m_corePhysics, 0x78);
        static_cast<CGroundShadowModule*>(m_shadow)->ChangePlace(&m_atPoint);
        static_cast<CGroundShadowModule*>(m_shadow)->SetNormal(&m_basisFrame.row[1]);
        m_vel = -*cameraPos;
        m_vel.y = 0.0f;
        m_vel.SetLength(10.0f);
        MarkJumpedAhead(false);
        return true;
    }
    case 0xC6870028: // die
        return true;
    case 0xF4C52F72: // GetSkaterNumber
        ScriptResults(script)->PutInt(0xF3CF5755, m_viewSlot); // skaternumber
        return true;
    default:
        return CGameEntity::InvokeMethodByName(name, params, script);
    }
}

// 0x00583DA0 (vtable +0x3C)
void CRider::JumpToSpawn(uint32_t node, uint32_t walking)
{
    OnlinePlay::Hub* net = NetLink();
    At<uint8_t>(FetchModule(kSkaterPhysicsControl), 0x26) = 1;
    if (m_unknownBC) {
        m_unknown264[0x498] = 0;
        reinterpret_cast<void(__thiscall*)(CRider*)>(m_vtable[0x30 / 4])(this);
        if (void* list = m_list28C)
            At<uint8_t>(list, 0x18) = 1;
        if (net->InNetGame()) {
            ScriptCommands::CFunc_NotifyBailDone(nullptr, nullptr);
            uint8_t* flag = At<uint8_t*>(m_corePhysics, 0x1528) + 0x70;
            if (flag[0]) {
                flag[0] = 0;
                *reinterpret_cast<int*>(flag + 4) = Clock();
            }
        }
    }
    if (m_unknown25C)
        static_cast<CPadRumbleModule*>(m_unknown25C)->ResetDevice();
    if (node)
        reinterpret_cast<void(__thiscall*)(CRider*, uint32_t)>(m_vtable[0x40 / 4])(this, node);
    Relaunch(walking, 0);
    if (m_unknownBC) {
        static_cast<CRiderSpinAndFlip*>(FetchModule(kSkaterFlipAndRotate))->ApplyFlipToAnims();
        if (m_unknownBC) {
            if (m_pauseState == 0) {
                MarkJumpedAhead(false);
                auto* camera = static_cast<CGameEntity*>(
                    CompositeObjectManager()->LookUpEntity(m_viewSlot == 1 ? kSkaterCam1 : kSkaterCam0));
                if (camera)
                    camera->TickFrame();
            }
            // The restart node's TriggerScript, run on it.
            if (node && LevelNodes::FetchVertex(node, !NetLink()->InNetGame())) {
                uint32_t script;
                if (LevelNodes::FetchVertex(node, true)->ReadHashValue(0x2CA8A299, &script, 0))
                    StartScriptTask(script, static_cast<int>(node), false, false, nullptr);
            }
        }
    }
    MarkJumpedAhead(true);
    // In these game modes the server is told (message 0x57).
    auto mode = [] { return static_cast<GoalSystem::CMatchRules*>(static_cast<void*>(GoalSystem::g_gameMode))->GetNameKey(); };
    if (mode() == 0x6C5FF266 || mode() == 0x6EF8FDA0 || mode() == 0x5D32129C) {
        NetLink::NetMessageInfo message;
        message.MakeNew();
        auto* client = static_cast<NetLink::Guest*>(net->FetchPatron(m_viewSlot));
        if (uint8_t* player = net->PlayerByObjectId(static_cast<uint16_t>(m_ident)))
            reinterpret_cast<OnlinePlay::PlayerRecord*>(player)->MarkFaceTextureSent();
        message.m_ident = 0x57;
        message.m_queue = NetLink::NetMessageInfo::kQueueSequenced;
        message.m_groupId = 8;
        client->SendToHost(&message);
    }
}

// 0x00583FB0
void CRider::DestroyBody()
{
    m_vtable = kSkaterVtable;
    if (m_isHalted && m_unknownBC && m_unknown264) {
        At<Sio::InputPort*>(At<uint8_t*>(m_unknown264, 0x490), 0x1C)->UnpauseActuators();
        At<Sio::InputPort*>(At<uint8_t*>(m_unknown264, 0x490), 0x1C)->StopActuators();
    }
    ResetComponents();
    Scripts::LaunchScript(0xF429E0AC, nullptr, this, false, nullptr);
    for (int i = 0; i < 11; ++i) {
        if (uint8_t* info = m_trickInfo[i])
            reinterpret_cast<void*(__thiscall*)(void*, int)>((*reinterpret_cast<void***>(info))[0])(info, 1);
    }
    if (m_unknownBC && m_viewSlot == 1) {
        if (CEntity* camera = CompositeObjectManager()->LookUpEntity(kSkaterCam1))
            reinterpret_cast<void(__thiscall*)(CEntity*)>(camera->m_vtable[0x18 / 4])(camera); // (marked dead)
    }
    // Out of the skater list.
    auto* node = reinterpret_cast<uint32_t*>(m_block238);
    node[0] = reinterpret_cast<uint32_t>(GameModules::kSkaterNodeVtable);
    reinterpret_cast<uint32_t*>(node[4])[3] = node[3];
    reinterpret_cast<uint32_t*>(node[3])[4] = node[4];
    node[3] = reinterpret_cast<uint32_t>(node);
    node[4] = reinterpret_cast<uint32_t>(node);
    m_shownName.~Chars();
    CGameEntity::DestroyBody();
}

// 0x005840E0
CRider* CRider::Teardown(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        MemSys::Hub::Pull()->Dealloc(this);
    return this;
}

} // namespace Obj

namespace GameObjects {
namespace {

static VtableSlot kSkaterVtable_slots[] = {&GameObjects::CRider::Teardown,
         &GameObjects::CEntity::ChangeTraits,
         static_cast<bool (GameObjects::CEntity::*)(Scripts::CMemberFunctionEntry*, Scripts::CStruct*, Scripts::CScript*)>(&GameObjects::CEntity::InvokeMethodByName),
         &GameObjects::CRider::InvokeMethodByName,
         Stubs::IgnoreArgument,
         &GameObjects::CEntity::PassEventToScript,
         &GameObjects::CGameEntity::FlagAsDead,
         &GameObjects::CGameEntity::ShouldUpdateScripts,
         &GameObjects::CGameEntity::Seal,
         &GameObjects::CGameEntity::CreateComponentFromStructure,
         Stubs::DoNothing,
         &GameObjects::CRider::Freeze,
         &GameObjects::CRider::Unpause,
         &GameObjects::CRider::InitFromProfile,
         &GameObjects::CRider::ResetInput,
         &GameObjects::CRider::JumpToSpawn,
         &GameObjects::CRider::GlideToNode,
         &GameObjects::CRider::ResetState,
         &GameObjects::CRider::SetUpNetwork,
         &GameObjects::CRider::RunStateScript};
void** const kSkaterVtable = reinterpret_cast<void**>(kSkaterVtable_slots);

} // namespace
} // namespace Obj
