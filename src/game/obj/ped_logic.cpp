// The PedLogic component (Obj::CPedLogicComponent): see ped_logic.h. Its making, states, settings and script member
// functions; from the disassembly of 0x004ED140-0x004F2560 (the member functions 0x004ED150, 0x004ED160, 0x004ED1D0,
// 0x004ED220, 0x004ED2A0, 0x004EDD80, 0x004EDDA0, 0x004EDE00, 0x004EDE20, 0x004EDEA0, 0x004EDEC0, 0x004EE100,
// 0x004EE230, 0x004EE280, 0x004F20E0, 0x004F24E0 and 0x004F2770, and 0x004EDC10, aren't Ghidra functions).

#include "game/obj/ped_logic.h"

#include <cstdint>

#include "game/core/crc.h"
#include "game/core/dbg.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/obj/model_component.h"
#include "game/obj/motion.h"
#include "game/obj/motion_path.h"
#include "game/script/cscript.h"
#include "game/script/names.h"
#include "game/script/nodes.h"
#include "game/script/struct.h"
#include "game/skate/profile_manager.h"
#include "game/skater/looping_sound.h"
#include "game/skater/skater_sound.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Obj {

namespace {

void** const kVtable = reinterpret_cast<void**>(0x0064B7A8);

constexpr uint32_t kMotion = 0x0A015E17;
constexpr uint32_t kModel = 0x286A8D26;
constexpr uint32_t kSkaterLoopingSound = 0xB5F1E9D2;
constexpr uint32_t kSkaterSound = 0x187574FA;

constexpr uint32_t kNull = 0xDA3403B0;              // null (the checksum of "NULL")
constexpr uint32_t kSkeletonName = 0x09794932;      // SkeletonName
constexpr uint32_t kPedFemale = 0x8642CA2A;         // Ped_Female
constexpr uint32_t kProfile = 0x7EA855F0;           // profile
constexpr uint32_t kBoard = 0xA7A9D4B8;             // board
constexpr uint32_t kDescId = 0x4BB2084E;            // desc_id
constexpr uint32_t kName = 0xA1DC81F9;              // Name
constexpr uint32_t kDistAbove = 0x367C4A1B, kDistBelow = 0x86F46E7B; // (InitFromStructure's)
constexpr uint32_t kDistAboveCmd = 0xA8A5A3C7, kDistBelowCmd = 0x182D87A7; // DistAbove / DistBelow
constexpr uint32_t kStickAbove = 0x379D93D2;        // ped_skater_stick_dist_above
constexpr uint32_t kStickBelow = 0x8715B7B2;        // ped_skater_stick_dist_below
constexpr uint32_t kPedInitializeMovement = 0x1B7DE0FB; // ped_initialize_movement
constexpr uint32_t kGenericMoveUpdate = 0x5B4E8427; // generic_move_update
constexpr uint32_t kSkaterMoveUpdate = 0xA631E964;  // skater_move_update
constexpr uint32_t kNoMoveUpdate = 0x520A0B40;      // no_move_update
constexpr uint32_t kPedInit = 0x2115FC7A;           // ped_init (a tag)
constexpr uint32_t kUniqueStateTable = 0x1DD08028;  // UniqueStateTable (a tag)
constexpr uint32_t kPedStateLogicTable = 0x51F1BB49; // Ped_StateLogic_Table
constexpr uint32_t kScriptPedState = 0x9958DCFD;    // script_ped_state
constexpr uint32_t kPedUpdateType = 0x959E031F;     // ped_update_type
constexpr uint32_t kExitStateAction = 0xC1868052;   // exit_state_action
constexpr uint32_t kWaitAnimFinished = 0x2E64106C;  // wait_anim_finished
constexpr uint32_t kHighPriority = 0x0030AC01;      // high_priority
constexpr uint32_t kDelayedState = 0x927BA4D7;      // DelayedState (a tag)
constexpr uint32_t kDelayedTable = 0xC7C1F96A;      // DelayedTable (a tag)
constexpr uint32_t kDelayWaitAnim = 0xF4C1639E;     // STATE_DELAY_waitanim
constexpr uint32_t kDelayWaitAnimScript = 0x9C6561DC; // STATECONTROL_DELAY_waitanim
constexpr uint32_t kHideBoardGeom = 0x1F22FA86;     // hide_boardgeom
constexpr uint32_t kTable = 0x09D670B9;             // Table
constexpr uint32_t kStateParams = 0x889A999C;       // StateParams
constexpr uint32_t kLogicState = 0x7BA5C584;        // LogicState
constexpr uint32_t kGrind = 0x255ED86F;             // Grind
constexpr uint32_t kVelocity = 0x41272956;          // Velocity
constexpr uint32_t kOriginalMaxVelocity = 0xEB4E99B3; // original_max_velocity
constexpr uint32_t kPercent = 0x9E497FC6;           // percent
constexpr uint32_t kDeltaV = 0x904403EC;            // ped_skater_deltaV
constexpr uint32_t kNodeTo = 0xD14CD5BC, kNodeFrom = 0x8141D319, kNodeAfter = 0x3C746255; // node_to / node_from
constexpr uint32_t kGender = 0x38B8F5BD, kMale = 0xAA721F56, kFemale = 0xB58B7593; // Gender: male / female
constexpr uint32_t kParams = 0x7031F10C;            // Params
constexpr uint32_t kStartMoving = 0xBED339D4;       // ped_skater_start_moving
constexpr uint32_t kProfileName = 0x17140E6D;       // profile_name (a tag)
constexpr uint32_t kCustom = 0x0A7BE964;            // Custom
constexpr uint32_t kMasterSkaterList = 0xF31E8FF8;  // master_skater_list
constexpr uint32_t kLastName = 0x37F745A5;          // last_name
constexpr uint32_t kVoicePrefix = 0xF75A5D96;       // VoicePrefix
constexpr uint32_t kVoicePrefixParam = 0xAB88FD0D;  // voice_prefix
constexpr uint32_t kVoice = 0x1804A7C4;             // voice
constexpr uint32_t kPlayerVoices = 0xCA1DB456;      // player_voices
constexpr uint32_t kId = 0x40C698AF;                // id
constexpr uint32_t kStreamName = 0x16CBB85A;        // stream_name
constexpr uint32_t kIsMale = 0x3F813177;            // is_male
constexpr uint32_t kSkaterPedStateTable = 0x46196541; // SkaterPed_StateTable
constexpr uint32_t kStateBail = 0x6259CA27, kStateBailGrind = 0x423431E1; // STATE_bail / STATE_bailgrind
constexpr uint32_t kLedgeMember = 0x315D9ED4;       // (the member function the skater's core physics calls Ledge)

constexpr float kMaxBoostedSpeed = 1100.0f;         // 0x0064A8CC
constexpr float kDefaultBoost = 1.25f;
constexpr float kPercentScale = 0.01f;              // 0x0064544C

CMotionComponent* Motion(CCompositeObject* object) { return static_cast<CMotionComponent*>(object->GetComponent(kMotion)); }

Script::CStruct* NewParams()
{
    Script::CStruct* memory = Script::NewStruct();
    return memory ? memory->Init() : nullptr;
}

// How the original makes and frees its scripts (the pooled operator new and Construct; Destroy and the pooled
// operator delete).
Script::CScript* NewScript()
{
    void* memory = Script::CScript::operator new(sizeof(Script::CScript));
    return memory ? static_cast<Script::CScript*>(memory)->Construct() : nullptr;
}
void DeleteScript(Script::CScript* script)
{
    script->Destroy();
    Script::CScript::operator delete(script);
}

} // namespace

// 0x004EDC10
GroundTriangle& GroundTriangle::Assign(const GroundTriangle& source)
{
    for (int i = 0; i < 3; ++i)
        corners[i] = source.corners[i];
    return *this;
}

// 0x004F0B10
CBaseComponent* __cdecl CPedLogicComponent::Create()
{
    void* memory = Mem::Calloc(sizeof(CPedLogicComponent));
    return memory ? static_cast<CPedLogicComponent*>(memory)->Construct() : nullptr;
}

// 0x004ECFB0
void __cdecl CPedLogicComponent::RegisterMemberFunctions()
{
    Script::RegisterMemberFunctionTable(reinterpret_cast<void*>(kType), &GameVariable<uint32_t>(0x00683730), 0x18);
}

// 0x004ECFD0
int CPedLogicComponent::CallMemberFunction(uint32_t name, Script::CStruct* /*params*/, Script::CScript* /*script*/)
{
    return name == kLedgeMember ? 1 : 2;
}

// 0x004EE4F0
CPedLogicComponent* CPedLogicComponent::Construct()
{
    CBaseComponent::Construct();
    m_vtable = kVtable;
    m_type = kType;
    for (float& bias : m_bias)
        bias = 0.0f;
    for (Mth::Vector& position : m_history)
        position = {0.0f, 0.0f, 0.0f, 1.0f};
    for (float& avoid : m_avoid)
        avoid = 0.0f;
    m_turnFrame = 0;
    m_turnFrames = 0;
    mp_peds = nullptr;
    m_stickAbove = Script::GetFloat(kStickAbove);
    m_stickBelow = Script::GetFloat(kStickBelow);
    m_flags = 0;
    m_storedMaxVelocity = 0.0f;
    m_state = Crc::GenerateCRCFromString("NULL");
    m_stateTable = Crc::GenerateCRCFromString("NULL");
    m_updateType = Crc::GenerateCRCFromString("NULL");
    mp_auxScript = nullptr;
    m_nextNodePos = {0.0f, 0.0f, 0.0f, 1.0f};
    return this;
}

// 0x004F2120
void CPedLogicComponent::DestroyBody()
{
    m_vtable = kVtable;
    if (Script::CScript* script = mp_auxScript) {
        DeleteScript(script);
        mp_auxScript = nullptr;
    }
    CBaseComponent::DestroyBody();
}

// 0x004F2540
CPedLogicComponent* CPedLogicComponent::Destroy(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x004ED2C0
void CPedLogicComponent::InitFromStructure(Script::CStruct* params)
{
    m_kind = 0;
    m_female = 0;
    uint32_t skeleton = 0;
    if (params->GetChecksum(kSkeletonName, &skeleton, 0) && skeleton == kPedFemale)
        m_female = 1;
    Script::CStruct* profile = nullptr;
    params->GetStructure(kProfile, &profile, 0);
    if (profile) {
        Script::CStruct* board = nullptr;
        profile->GetStructure(kBoard, &board, 0);
        if (board) {
            uint32_t descId = 0;
            if (board->GetChecksum(kDescId, &descId, 0))
                m_flags |= PED_FLAG_BOARD_HIDEABLE;
        }
    }
    params->GetChecksum(kName, &m_node, 1);
    SkateScript::GetPosition(m_node, &m_nodePos);
    m_upBlend = 0.0f;
    m_upTarget = m_object->m_matrix.row[1];
    m_up = m_upTarget;
    m_upShown = m_up;
    params->GetFloat(kDistAbove, &m_stickAbove, 0);
    params->GetFloat(kDistBelow, &m_stickBelow, 0);
    m_savedMatrix = m_object->m_displayMatrix;
}

// 0x004ED460
void CPedLogicComponent::InitializeMovement()
{
    Script::RunScript(kPedInitializeMovement, nullptr, m_object, false, nullptr);
    if (m_updateType == kGenericMoveUpdate || m_updateType == kSkaterMoveUpdate)
        m_flags |= PED_FLAG_MOVING;
}

// 0x004ED4A0
void CPedLogicComponent::NoMoveUpdate()
{
    CCompositeObject* object = m_object;
    if (reinterpret_cast<CMotionPathMembers*>(Motion(object))->IsRotating())
        m_savedMatrix = object->m_matrix;
    else
        object->m_displayMatrix = m_savedMatrix;
}

// 0x004ED4F0
void CPedLogicComponent::PushPosition()
{
    for (int i = 0; i < 4; ++i)
        m_history[i] = m_history[i + 1];
    m_history[4] = m_object->m_pos;
}

// 0x004EDC60
void CPedLogicComponent::SetSoundState(int state)
{
    if (auto* looping = static_cast<CSkaterLoopingSoundComponent*>(m_object->GetComponent(kSkaterLoopingSound))) {
        looping->m_soundChanged |= looping->m_state != state;
        looping->m_state = state;
    }
    if (auto* sound = static_cast<CSkaterSoundComponent*>(m_object->GetComponent(kSkaterSound)))
        sound->m_state = state;
}

// 0x004EDCB0
void CPedLogicComponent::SetSoundTerrain(uint8_t terrain)
{
    if (auto* looping = static_cast<CSkaterLoopingSoundComponent*>(m_object->GetComponent(kSkaterLoopingSound))) {
        looping->m_soundChanged |= looping->m_terrain != terrain;
        looping->m_terrain = terrain;
    }
    if (auto* sound = static_cast<CSkaterSoundComponent*>(m_object->GetComponent(kSkaterSound))) {
        sound->m_terrain = terrain;
        sound->m_lastGround = terrain;
    }
}

// 0x004EDD00
bool CPedLogicComponent::HideBoard(int hide)
{
    if (!(m_flags & PED_FLAG_BOARD_HIDEABLE))
        return false;
    if (auto* model = static_cast<CModelComponent*>(m_object->GetComponent(kModel))) {
        if (hide) {
            if (!(m_flags & PED_FLAG_BOARD_HIDDEN)) {
                model->HideGeom(kBoard, true, true);
                m_flags |= PED_FLAG_BOARD_HIDDEN;
                return true;
            }
        } else if (m_flags & PED_FLAG_BOARD_HIDDEN) {
            model->HideGeom(kBoard, false, true);
            m_flags &= ~PED_FLAG_BOARD_HIDDEN;
        }
    }
    return true;
}

// 0x004EFB10
Script::CStruct* CPedLogicComponent::StateParams(Script::CStruct* params)
{
    Script::CStruct* out = NewParams();
    out->AppendStructureIfAny(params);
    out->AppendStructureIfAny(m_object->mp_tags);
    return out;
}

// 0x004F0B70
bool CPedLogicComponent::SetState(uint32_t state, uint32_t table, Script::CStruct* params)
{
    int pedInit = 0;
    m_object->GetIntegerTag(kPedInit, &pedInit);
    if (!pedInit)
        return false;
    if (!table && !m_object->GetChecksumTag(kUniqueStateTable, &table))
        table = kPedStateLogicTable;
    Script::CStruct* tableStruct = Script::GetStructure(table);
    if (!tableStruct)
        return false;
    Script::CStruct* stateStruct = nullptr;
    if (!tableStruct->GetStructure(state, &stateStruct, 0)) {
        if (table == kPedStateLogicTable)
            return false;
        // Not in its own table: the common one's (looked for without a null check).
        table = kPedStateLogicTable;
        if (!Script::GetStructure(kPedStateLogicTable)->GetStructure(state, &stateStruct, 0))
            return false;
    }
    uint32_t script = 0;
    stateStruct->GetChecksum(kScriptPedState, &script, 1);
    uint32_t updateType = kNoMoveUpdate;
    stateStruct->GetChecksum(kPedUpdateType, &updateType, 0);
    // Leaving the old state.
    if (m_state != kNull) {
        if (Script::CStruct* oldTable = Script::GetStructure(m_stateTable)) {
            Script::CStruct* oldState = nullptr;
            if (oldTable->GetStructure(m_state, &oldState, 0)) {
                uint32_t exitAction;
                if (oldState->GetChecksum(kExitStateAction, &exitAction, 0)) {
                    if (exitAction == kWaitAnimFinished) {
                        if (!stateStruct->ContainsFlag(kHighPriority)) {
                            // The new state waits for the animation to finish.
                            m_object->SetChecksumTag(kDelayedState, state);
                            m_object->SetChecksumTag(kDelayedTable, table);
                            table = kPedStateLogicTable;
                            state = kDelayWaitAnim;
                            script = kDelayWaitAnimScript;
                            updateType = kNoMoveUpdate;
                        }
                    } else if (Script::ScriptExists(exitAction)) {
                        Script::RunScript(exitAction, nullptr, m_object, false, nullptr);
                    }
                }
                if (oldState->ContainsFlag(kHideBoardGeom))
                    HideBoard(0);
            }
        }
    }
    m_stateTable = table;
    m_state = state;
    m_updateType = updateType;
    m_object->SwitchScript(script, params);
    if (Script::CScript* aux = mp_auxScript) {
        DeleteScript(aux);
        mp_auxScript = nullptr;
    }
    InitializeMovement();
    if (stateStruct->ContainsFlag(kHideBoardGeom))
        HideBoard(1);
    if (m_object->m_script)
        m_object->m_script->Update();
    return true;
}

// 0x004F2240
void CPedLogicComponent::ActivateAuxScript(uint32_t script, Script::CStruct* params)
{
    if (!script)
        return;
    if (!mp_auxScript)
        mp_auxScript = NewScript();
    mp_auxScript->SetScript(script, params, m_object);
    if (mp_auxScript->GotScript()) {
        mp_auxScript->Update();
        return;
    }
    if (Script::CScript* aux = mp_auxScript) {
        DeleteScript(aux);
        mp_auxScript = nullptr;
    }
}

// 0x004F1E10
bool CPedLogicComponent::Bail()
{
    if (Motion(m_object)->m_motionFlags & 0x100)
        return false;
    if (m_storedMaxVelocity > 0.0f)
        Motion(m_object)->m_pathMaxSpeed = m_storedMaxVelocity;
    SetState((m_flags & PED_FLAG_GRINDING) ? kStateBailGrind : kStateBail, kSkaterPedStateTable, nullptr);
    m_flags &= ~(PED_FLAG_GRINDING | PED_FLAG_SPINNING | PED_FLAG_VERT_ROTATING);
    return true;
}

// 0x004EE2C0
const char* ProfileVoice()
{
    CSkaterProfile* profile = GameVariable<CPlayerProfileManager*>(0x007CE4B4)->GetCurrentProfile();
    auto* info = reinterpret_cast<Script::CStruct*>(profile->m_info);
    uint32_t voice;
    if (!info->GetChecksum(kVoice, &voice, 0)) {
        int male = 0; // (the original's is uninitialised: is_male is asserted)
        info->GetInteger(kIsMale, &male, 1);
        return male == 1 ? "Male1" : "Female1";
    }
    Script::CArray* voices = Script::GetArray(kPlayerVoices);
    int count = static_cast<int>(voices->m_size);
    for (int i = 0; i < count; ++i) {
        auto* entry = reinterpret_cast<Script::CStruct*>(voices->GetElement(static_cast<uint32_t>(i)));
        uint32_t id;
        entry->GetChecksum(kId, &id, 1);
        if (id == voice) {
            const char* name = nullptr;
            entry->GetString(kStreamName, &name, 1);
            return name;
        }
    }
    return nullptr;
}

// --- script member functions ---

// 0x004F2040
bool CPedLogicComponent::Ped_SetLogicState(Script::CStruct* params, Script::CScript*)
{
    uint32_t state = 0, table = 0;
    params->GetChecksum(0u, &state, 1);
    if (state == m_state)
        return true;
    params->GetChecksum(kTable, &table, 0);
    Script::CStruct* stateParams = nullptr;
    if (params)
        params->GetStructure(kStateParams, &stateParams, 0);
    return SetState(state, table, stateParams);
}

// 0x004EDD80
bool CPedLogicComponent::Ped_GetLogicState(Script::CStruct*, Script::CScript* script)
{
    script->m_params->AddChecksum(kLogicState, m_state);
    return true;
}

// 0x004F24B0
bool CPedLogicComponent::Ped_StartMoving(Script::CStruct*, Script::CScript*)
{
    if (m_flags & PED_FLAG_STOPPING) {
        ActivateAuxScript(kStartMoving, nullptr);
        m_flags &= ~PED_FLAG_STOPPING;
    }
    m_flags |= PED_FLAG_MOVING;
    return true;
}

// 0x004ED150
bool CPedLogicComponent::Ped_StopMoving(Script::CStruct*, Script::CScript*)
{
    m_flags &= ~PED_FLAG_MOVING;
    return true;
}

// 0x004ED160
bool CPedLogicComponent::Ped_IsMoving(Script::CStruct*, Script::CScript*) { return (m_flags & PED_FLAG_MOVING) != 0; }

// 0x004ED170
bool CPedLogicComponent::Ped_SetStickDistances(Script::CStruct* params, Script::CScript*)
{
    params->GetFloat(kDistAboveCmd, &m_stickAbove, 0);
    params->GetFloat(kDistBelowCmd, &m_stickBelow, 0);
    Dbg::Printf("StickToGround distances set to %f (above), %f (below)\n", static_cast<double>(m_stickAbove),
                static_cast<double>(m_stickBelow));
    return true;
}

// 0x004ED1D0
bool CPedLogicComponent::Ped_SetIsGrinding(Script::CStruct* params, Script::CScript*)
{
    int grinding = 0;
    params->GetInteger(0u, &grinding, 1);
    if (grinding)
        m_flags |= PED_FLAG_GRINDING;
    else
        m_flags &= ~PED_FLAG_GRINDING;
    return true;
}

// 0x004ED220
bool CPedLogicComponent::Ped_SetIsBailing(Script::CStruct* params, Script::CScript*)
{
    // (The original reads the value over its `params` argument: missing - it's asserted - it stays that pointer.)
    int bailing = static_cast<int>(reinterpret_cast<uintptr_t>(params));
    params->GetInteger(0u, &bailing, 1);
    if (params->ContainsFlag(kGrind)) {
        if (bailing)
            m_flags |= PED_FLAG_GRIND_BAILING;
        else
            m_flags &= ~PED_FLAG_GRIND_BAILING;
    } else if (bailing) {
        m_flags |= PED_FLAG_BAILING;
    } else {
        m_flags &= ~PED_FLAG_BAILING;
    }
    return true;
}

// 0x004ED2A0
bool CPedLogicComponent::Ped_NearVertNode(Script::CStruct*, Script::CScript*)
{
    return (m_flags & (PED_FLAG_VERT_NEXT | PED_FLAG_VERT_LAST | PED_FLAG_VERT_AFTER)) != 0;
}

// 0x004EDDA0
bool CPedLogicComponent::Ped_GetCurrentVelocity(Script::CStruct*, Script::CScript* script)
{
    script->m_params->AddFloat(kVelocity, Motion(m_object)->m_pathSpeed);
    return true;
}

// 0x004EDDD0
bool CPedLogicComponent::Ped_StoreMaxVelocity(Script::CStruct*, Script::CScript*)
{
    m_storedMaxVelocity = Motion(m_object)->m_pathMaxSpeed;
    return true;
}

// 0x004EDE00
bool CPedLogicComponent::Ped_GetOriginalMaxVelocity(Script::CStruct*, Script::CScript* script)
{
    script->m_params->AddFloat(kOriginalMaxVelocity, m_storedMaxVelocity);
    return true;
}

// 0x004EDE20
bool CPedLogicComponent::Ped_ApplySpeedBoost(Script::CStruct* params, Script::CScript*)
{
    float boost = kDefaultBoost;
    if (params->GetFloat(kPercent, &boost, 0))
        boost = static_cast<float>(static_cast<double>(boost) * kPercentScale + 1.0);
    if (CMotionComponent* motion = Motion(m_object)) {
        double speed = static_cast<double>(boost) * motion->m_pathMaxSpeed;
        motion->m_pathMaxSpeed = static_cast<float>(speed);
        if (speed > kMaxBoostedSpeed)
            motion->m_pathMaxSpeed = kMaxBoostedSpeed;
    }
    return true;
}

// 0x004EDEA0
bool CPedLogicComponent::Ped_RemoveSpeedBoost(Script::CStruct*, Script::CScript*)
{
    if (CMotionComponent* motion = Motion(m_object))
        motion->m_pathSpeed = motion->m_pathMaxSpeed;
    return true;
}

// 0x004EE100
bool CPedLogicComponent::Ped_SetIsSkater(Script::CStruct* params, Script::CScript*)
{
    // (The value is read over the `params` argument: missing - it's asserted - it stays that pointer.)
    int skater = static_cast<int>(reinterpret_cast<uintptr_t>(params));
    params->GetInteger(0u, &skater, 1);
    if (!skater) {
        m_kind = 0;
        return true;
    }
    m_kind = 2;
    if (CMotionComponent* motion = Motion(m_object)) {
        motion->m_pathAcceleration = Script::GetFloat(kDeltaV);
        motion->m_pathDeceleration = Script::GetFloat(kDeltaV);
    }
    return true;
}

// 0x004EE180
bool CPedLogicComponent::Ped_PlayJumpSound(Script::CStruct*, Script::CScript*)
{
    if (auto* sound = static_cast<CSkaterSoundComponent*>(m_object->GetComponent(kSkaterSound)))
        sound->PlayJumpHere(m_speedFraction);
    return true;
}

// 0x004EE1B0
bool CPedLogicComponent::Ped_PlayLandSound(Script::CStruct*, Script::CScript*)
{
    if (auto* sound = static_cast<CSkaterSoundComponent*>(m_object->GetComponent(kSkaterSound)))
        sound->PlayLandHere(m_speedFraction);
    return true;
}

// 0x004EE1E0
bool CPedLogicComponent::Ped_GetCurrentNodeNames(Script::CStruct*, Script::CScript* script)
{
    script->m_params->AddChecksum(kNodeTo, m_nextNode);
    script->m_params->AddChecksum(kNodeFrom, m_node);
    script->m_params->AddChecksum(kNodeAfter, m_nodeAfter);
    return true;
}

// 0x004EE230
bool CPedLogicComponent::Ped_GetGender(Script::CStruct*, Script::CScript* script)
{
    script->m_params->AddChecksum(kGender, m_female == 1 ? kFemale : kMale);
    return true;
}

// 0x004F24E0
bool CPedLogicComponent::Ped_ActivateAuxScript(Script::CStruct* params, Script::CScript*)
{
    uint32_t name = 0;
    if (params->GetChecksum(kName, &name, 1)) {
        Script::CStruct* scriptParams = nullptr;
        params->GetStructure(kParams, &scriptParams, 0);
        ActivateAuxScript(name, scriptParams);
    }
    return true;
}

// 0x004F20F0
bool CPedLogicComponent::Ped_DeactivateAuxScript(Script::CStruct*, Script::CScript*)
{
    if (Script::CScript* aux = mp_auxScript) {
        DeleteScript(aux);
        mp_auxScript = nullptr;
    }
    return true;
}

// 0x004EE280
bool CPedLogicComponent::Ped_HideBoard(Script::CStruct* params, Script::CScript*)
{
    int hide = 0;
    params->GetInteger(0u, &hide, 0);
    return HideBoard(hide);
}

// 0x004F20E0
bool CPedLogicComponent::Ped_Bail(Script::CStruct*, Script::CScript*)
{
    Bail();
    return true;
}

// 0x004EE3A0
bool CPedLogicComponent::Ped_GetVoicePrefix(Script::CStruct*, Script::CScript* script)
{
    CCompositeObject* object = m_object;
    uint32_t profileName;
    if (object->mp_tags && object->mp_tags->GetChecksum(kProfileName, &profileName, 0)) {
        if (profileName == kCustom) {
            if (const char* voice = ProfileVoice()) {
                script->m_params->AddString(kVoicePrefix, voice);
                return true;
            }
        } else {
            Script::CArray* skaters = Script::GetArray(kMasterSkaterList);
            int count = static_cast<int>(skaters->m_size);
            for (int i = 0; i < count; ++i) {
                auto* skater = reinterpret_cast<Script::CStruct*>(skaters->GetElement(static_cast<uint32_t>(i)));
                uint32_t name;
                skater->GetChecksum(kName, &name, 1);
                if (name == profileName) {
                    // (Read over the slot that held `this`: missing - it's asserted - it stays that pointer.)
                    const char* lastName = reinterpret_cast<const char*>(this);
                    skater->GetString(kLastName, &lastName, 1);
                    script->m_params->AddString(kVoicePrefix, lastName);
                    return true;
                }
            }
        }
    }
    // The ped's node's profile's voice_prefix.
    if (Script::CStruct* node = SkateScript::GetNode(m_object->m_id, true)) {
        Script::CStruct* profile = nullptr;
        node->GetStructure(kProfile, &profile, 0);
        if (profile) {
            const char* prefix = nullptr;
            profile->GetString(kVoicePrefixParam, &prefix, 0);
            if (prefix) {
                script->m_params->AddString(kVoicePrefix, prefix);
                return true;
            }
        }
    }
    return false;
}

} // namespace Obj

REPLACE(0x004F0B10, Obj::CPedLogicComponent::Create);
REPLACE(0x004ECFB0, Obj::CPedLogicComponent::RegisterMemberFunctions);
REPLACE(0x004ECFD0, &Obj::CPedLogicComponent::CallMemberFunction);
REPLACE(0x004EE4F0, &Obj::CPedLogicComponent::Construct);
REPLACE(0x004F2120, &Obj::CPedLogicComponent::DestroyBody);
REPLACE(0x004F2540, &Obj::CPedLogicComponent::Destroy);
REPLACE(0x004ED2C0, &Obj::CPedLogicComponent::InitFromStructure);
REPLACE_SOURCE_ONLY(0x004ED140, &Obj::CPedLogicComponent::GetLogicState);
REPLACE(0x004ED460, &Obj::CPedLogicComponent::InitializeMovement);
REPLACE(0x004ED4A0, &Obj::CPedLogicComponent::NoMoveUpdate);
REPLACE(0x004ED4F0, &Obj::CPedLogicComponent::PushPosition);
REPLACE(0x004EDC10, &Obj::GroundTriangle::Assign);
REPLACE(0x004EDC60, &Obj::CPedLogicComponent::SetSoundState);
REPLACE(0x004EDCB0, &Obj::CPedLogicComponent::SetSoundTerrain);
REPLACE(0x004EDD00, &Obj::CPedLogicComponent::HideBoard);
REPLACE(0x004EFB10, &Obj::CPedLogicComponent::StateParams);
REPLACE(0x004F0B70, &Obj::CPedLogicComponent::SetState);
REPLACE(0x004F2240, &Obj::CPedLogicComponent::ActivateAuxScript);
REPLACE(0x004F1E10, &Obj::CPedLogicComponent::Bail);
REPLACE(0x004EE2C0, Obj::ProfileVoice);
REPLACE(0x004F2040, &Obj::CPedLogicComponent::Ped_SetLogicState);
REPLACE(0x004EDD80, &Obj::CPedLogicComponent::Ped_GetLogicState);
REPLACE(0x004F24B0, &Obj::CPedLogicComponent::Ped_StartMoving);
REPLACE(0x004ED150, &Obj::CPedLogicComponent::Ped_StopMoving);
REPLACE(0x004ED160, &Obj::CPedLogicComponent::Ped_IsMoving);
REPLACE(0x004ED170, &Obj::CPedLogicComponent::Ped_SetStickDistances);
REPLACE(0x004ED1D0, &Obj::CPedLogicComponent::Ped_SetIsGrinding);
REPLACE(0x004ED220, &Obj::CPedLogicComponent::Ped_SetIsBailing);
REPLACE(0x004ED2A0, &Obj::CPedLogicComponent::Ped_NearVertNode);
REPLACE(0x004EDDA0, &Obj::CPedLogicComponent::Ped_GetCurrentVelocity);
REPLACE(0x004EDDD0, &Obj::CPedLogicComponent::Ped_StoreMaxVelocity);
REPLACE(0x004EDE00, &Obj::CPedLogicComponent::Ped_GetOriginalMaxVelocity);
REPLACE(0x004EDE20, &Obj::CPedLogicComponent::Ped_ApplySpeedBoost);
REPLACE(0x004EDEA0, &Obj::CPedLogicComponent::Ped_RemoveSpeedBoost);
REPLACE(0x004EE100, &Obj::CPedLogicComponent::Ped_SetIsSkater);
REPLACE(0x004EE180, &Obj::CPedLogicComponent::Ped_PlayJumpSound);
REPLACE(0x004EE1B0, &Obj::CPedLogicComponent::Ped_PlayLandSound);
REPLACE(0x004EE1E0, &Obj::CPedLogicComponent::Ped_GetCurrentNodeNames);
REPLACE(0x004EE230, &Obj::CPedLogicComponent::Ped_GetGender);
REPLACE(0x004F24E0, &Obj::CPedLogicComponent::Ped_ActivateAuxScript);
REPLACE(0x004F20F0, &Obj::CPedLogicComponent::Ped_DeactivateAuxScript);
REPLACE(0x004EE280, &Obj::CPedLogicComponent::Ped_HideBoard);
REPLACE(0x004F20E0, &Obj::CPedLogicComponent::Ped_Bail);
REPLACE(0x004EE3A0, &Obj::CPedLogicComponent::Ped_GetVoicePrefix);
