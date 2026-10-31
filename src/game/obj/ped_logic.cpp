// The PedLogic component (GameObjects::CPedestrianBrainModule): see ped_logic.h. Its making, states, settings and script member
// functions; from the disassembly of 0x004ED140-0x004F2560 (the member functions 0x004ED150, 0x004ED160, 0x004ED1D0,
// 0x004ED220, 0x004ED2A0, 0x004EDD80, 0x004EDDA0, 0x004EDE00, 0x004EDE20, 0x004EDEA0, 0x004EDEC0, 0x004EE100,
// 0x004EE230, 0x004EE280, 0x004F20E0, 0x004F24E0 and 0x004F2770, and 0x004EDC10, aren't Ghidra functions).

#include "game/obj/ped_logic.h"

#include <cstdint>
#include <new>

#include "game/core/checksums.h"
#include "game/core/dbg.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/obj/model_component.h"
#include "game/obj/motion.h"
#include "game/obj/motion_path.h"
#include "game/script/cscript.h"
#include "game/script/names.h"
#include "game/script/nodes.h"
#include "game/script/param_struct.h"
#include "game/skate/profile_manager.h"
#include "game/skater/looping_sound.h"
#include "game/skater/skater_sound.h"
#include "game/core/code_address.h"
#include "game/core/stubs.h"
#include "game/obj/car_physics.h"
#include "game/obj/node_components.h"
#include "game/obj/entity.h"
#include "game/obj/camera_lookaround.h"

namespace GameObjects {

uint32_t g_pedLogicMemberFunctions[174]{
            0x3685B765, 0x6EFDDFB8, 0, 0, 0, 0,
            0x423B7B29, 0x6EFDDFB8, 0, 0, 0, 0,
            0xE9906815, 0x6EFDDFB8, 0, 0, 0, 0,
            0x0D711542, 0x6EFDDFB8, 0, 0, 0, 0,
            0xCC85ADFE, 0x6EFDDFB8, 0, 0, 0, 0,
            0x68D298B0, 0x6EFDDFB8, 0, 0, 0, 0,
            0x2CC086DD, 0x6EFDDFB8, 0, 0, 0, 0,
            0xC643E8F5, 0x6EFDDFB8, 0, 0, 0, 0,
            0x13713760, 0x6EFDDFB8, 0, 0, 0, 0,
            0xF5627596, 0x6EFDDFB8, 0, 0, 0, 0,
            0xE575EFA9, 0x6EFDDFB8, 0, 0, 0, 0,
            0xB4445051, 0x6EFDDFB8, 0, 0, 0, 0,
            0x744CD649, 0x6EFDDFB8, 0, 0, 0, 0,
            0xEE24A410, 0x6EFDDFB8, 0, 0, 0, 0,
            0x0C076D74, 0x6EFDDFB8, 0, 0, 0, 0,
            0xDA7BE9E2, 0x6EFDDFB8, 0, 0, 0, 0,
            0x1AFFFFB3, 0x6EFDDFB8, 0, 0, 0, 0,
            0x5C794C57, 0x6EFDDFB8, 0, 0, 0, 0,
            0x5BB23915, 0x6EFDDFB8, 0, 0, 0, 0,
            0x56D9F55F, 0x6EFDDFB8, 0, 0, 0, 0,
            0x749DE914, 0x6EFDDFB8, 0, 0, 0, 0,
            0x58A588B5, 0x6EFDDFB8, 0, 0, 0, 0,
            0x5CFBFB31, 0x6EFDDFB8, 0, 0, 0, 0,
            0x9C37FBF5, 0x6EFDDFB8, 0, 0, 0, 0,
            0x35D3CA5E, 0x6EFDDFB8, 0, 0, 0, 0,
            0x9C209251, 0x6EFDDFB8, 0, 0, 0, 0,
            0xC33754F7, 0x6EFDDFB8, 0, 0, 0, 0,
            0x5F041C7D, 0x6EFDDFB8, 0, 0, 0, 0,
            0, 0x6EFDDFB8, 0, 0, 0, 0};

namespace {

extern void** const kVtable;

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
constexpr uint32_t kDistAbove = 0x367C4A1B, kDistBelow = 0x86F46E7B; // (ConfigureFromData's)
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

CKinematicsModule* Motion(CGameEntity* object) { return static_cast<CKinematicsModule*>(object->FetchModule(kMotion)); }

Scripts::CStruct* NewParams()
{
    Scripts::CStruct* memory = Scripts::SpawnParamBag();
    return memory ? memory->PrepareAll() : nullptr;
}

// How the original makes and frees its scripts (the pooled operator new and Construct; Destroy and the pooled
// operator delete).
Scripts::CScript* NewScript()
{
    void* memory = Scripts::CScript::operator new(sizeof(Scripts::CScript));
    return memory ? static_cast<Scripts::CScript*>(memory)->MakeNew() : nullptr;
}
void DeleteScript(Scripts::CScript* script)
{
    script->Teardown();
    Scripts::CScript::operator delete(script);
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
CObjectModule* __cdecl CPedestrianBrainModule::Fabricate()
{
    void* memory = MemSys::AllocZeroed(sizeof(CPedestrianBrainModule));
    return memory ? static_cast<CPedestrianBrainModule*>(memory)->MakeNew() : nullptr;
}

// 0x004ECFB0
void __cdecl CPedestrianBrainModule::PublishMethodTable()
{
    Scripts::RegisterMemberFunctionTable(reinterpret_cast<void*>(kType), g_pedLogicMemberFunctions, 0x18);
}

// 0x004ECFD0
int CPedestrianBrainModule::InvokeMethodByName(uint32_t name, Scripts::CStruct* /*params*/, Scripts::CScript* /*script*/)
{
    return name == kLedgeMember ? 1 : 2;
}

// 0x004EE4F0
CPedestrianBrainModule* CPedestrianBrainModule::MakeNew()
{
    CObjectModule::MakeNew();
    m_vtable = kVtable;
    m_kindTag = kType;
    for (float& bias : m_bias)
        bias = 0.0f;
    for (Maths::Vec4& position : m_pastFrames)
        position = {0.0f, 0.0f, 0.0f, 1.0f};
    for (float& avoid : m_avoid)
        avoid = 0.0f;
    m_turnFrame = 0;
    m_turnFrames = 0;
    m_peds = nullptr;
    m_stickAbove = Scripts::FetchFloat(kStickAbove);
    m_stickBelow = Scripts::FetchFloat(kStickBelow);
    m_flagBits = 0;
    m_storedMaxVelocity = 0.0f;
    m_curPhase = Checksums::HashFromText("NULL");
    m_stateTable = Checksums::HashFromText("NULL");
    m_updateType = Checksums::HashFromText("NULL");
    m_auxScript = nullptr;
    m_nextNodePos = {0.0f, 0.0f, 0.0f, 1.0f};
    return this;
}

// 0x004F2120
void CPedestrianBrainModule::DestroyBody()
{
    m_vtable = kVtable;
    if (Scripts::CScript* script = m_auxScript) {
        DeleteScript(script);
        m_auxScript = nullptr;
    }
    CObjectModule::DestroyBody();
}

// 0x004F2540
CPedestrianBrainModule* CPedestrianBrainModule::Teardown(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        MemSys::Hub::Pull()->Dealloc(this);
    return this;
}

// 0x004ED2C0
void CPedestrianBrainModule::ConfigureFromData(Scripts::CStruct* params)
{
    m_kind = 0;
    m_female = 0;
    uint32_t skeleton = 0;
    if (params->ReadHashValue(kSkeletonName, &skeleton, 0) && skeleton == kPedFemale)
        m_female = 1;
    Scripts::CStruct* profile = nullptr;
    params->ReadSubStruct(kProfile, &profile, 0);
    if (profile) {
        Scripts::CStruct* board = nullptr;
        profile->ReadSubStruct(kBoard, &board, 0);
        if (board) {
            uint32_t descriptorId = 0;
            if (board->ReadHashValue(kDescId, &descriptorId, 0))
                m_flagBits |= PED_FLAG_BOARD_HIDEABLE;
        }
    }
    params->ReadHashValue(kName, &m_listLink, 1);
    LevelNodes::FetchPlace(m_listLink, &m_nodePos);
    m_upBlend = 0.0f;
    m_upTarget = m_entity->m_basisFrame.row[1];
    m_upDir = m_upTarget;
    m_upShown = m_upDir;
    params->FetchFloat(kDistAbove, &m_stickAbove, 0);
    params->FetchFloat(kDistBelow, &m_stickBelow, 0);
    m_savedMatrix = m_entity->m_displayMatrix;
}

// 0x004ED460
void CPedestrianBrainModule::InitializeMovement()
{
    Scripts::LaunchScript(kPedInitializeMovement, nullptr, m_entity, false, nullptr);
    if (m_updateType == kGenericMoveUpdate || m_updateType == kSkaterMoveUpdate)
        m_flagBits |= PED_FLAG_MOVING;
}

// 0x004ED4A0
void CPedestrianBrainModule::NoMoveUpdate()
{
    CGameEntity* object = m_entity;
    if (reinterpret_cast<CMotionPathMembers*>(Motion(object))->IsSpinning())
        m_savedMatrix = object->m_basisFrame;
    else
        object->m_displayMatrix = m_savedMatrix;
}

// 0x004ED4F0
void CPedestrianBrainModule::PushPosition()
{
    for (int i = 0; i < 4; ++i)
        m_pastFrames[i] = m_pastFrames[i + 1];
    m_pastFrames[4] = m_entity->m_atPoint;
}

// 0x004EDC60
void CPedestrianBrainModule::SetSoundState(int state)
{
    if (auto* looping = static_cast<CRiderRollingAudio*>(m_entity->FetchModule(kSkaterLoopingSound))) {
        looping->m_soundChanged |= looping->m_curPhase != state;
        looping->m_curPhase = state;
    }
    if (auto* sound = static_cast<CRiderSfxPlayer*>(m_entity->FetchModule(kSkaterSound)))
        sound->m_curPhase = state;
}

// 0x004EDCB0
void CPedestrianBrainModule::SetSoundTerrain(uint8_t terrain)
{
    if (auto* looping = static_cast<CRiderRollingAudio*>(m_entity->FetchModule(kSkaterLoopingSound))) {
        looping->m_soundChanged |= looping->m_surfaceType != terrain;
        looping->m_surfaceType = terrain;
    }
    if (auto* sound = static_cast<CRiderSfxPlayer*>(m_entity->FetchModule(kSkaterSound))) {
        sound->m_surfaceType = terrain;
        sound->m_lastGround = terrain;
    }
}

// 0x004EDD00
bool CPedestrianBrainModule::HideBoard(int hide)
{
    if (!(m_flagBits & PED_FLAG_BOARD_HIDEABLE))
        return false;
    if (auto* model = static_cast<CMeshInstanceModule*>(m_entity->FetchModule(kModel))) {
        if (hide) {
            if (!(m_flagBits & PED_FLAG_BOARD_HIDDEN)) {
                model->ConcealMesh(kBoard, true, true);
                m_flagBits |= PED_FLAG_BOARD_HIDDEN;
                return true;
            }
        } else if (m_flagBits & PED_FLAG_BOARD_HIDDEN) {
            model->ConcealMesh(kBoard, false, true);
            m_flagBits &= ~PED_FLAG_BOARD_HIDDEN;
        }
    }
    return true;
}

// 0x004EFB10
Scripts::CStruct* CPedestrianBrainModule::StateParams(Scripts::CStruct* params)
{
    Scripts::CStruct* out = NewParams();
    out->AppendStructureIfAny(params);
    out->AppendStructureIfAny(m_entity->m_tagTable);
    return out;
}

// 0x004F0B70
bool CPedestrianBrainModule::ChangePhase(uint32_t state, uint32_t table, Scripts::CStruct* params)
{
    int pedInit = 0;
    m_entity->ReadIntTag(kPedInit, &pedInit);
    if (!pedInit)
        return false;
    if (!table && !m_entity->ReadHashTag(kUniqueStateTable, &table))
        table = kPedStateLogicTable;
    Scripts::CStruct* tableStruct = Scripts::ReadSubStruct(table);
    if (!tableStruct)
        return false;
    Scripts::CStruct* stateStruct = nullptr;
    if (!tableStruct->ReadSubStruct(state, &stateStruct, 0)) {
        if (table == kPedStateLogicTable)
            return false;
        // Not in its own table: the common one's (looked for without a null check).
        table = kPedStateLogicTable;
        if (!Scripts::ReadSubStruct(kPedStateLogicTable)->ReadSubStruct(state, &stateStruct, 0))
            return false;
    }
    uint32_t script = 0;
    stateStruct->ReadHashValue(kScriptPedState, &script, 1);
    uint32_t updateType = kNoMoveUpdate;
    stateStruct->ReadHashValue(kPedUpdateType, &updateType, 0);
    // Leaving the old state.
    if (m_curPhase != kNull) {
        if (Scripts::CStruct* oldTable = Scripts::ReadSubStruct(m_stateTable)) {
            Scripts::CStruct* oldState = nullptr;
            if (oldTable->ReadSubStruct(m_curPhase, &oldState, 0)) {
                uint32_t exitAction;
                if (oldState->ReadHashValue(kExitStateAction, &exitAction, 0)) {
                    if (exitAction == kWaitAnimFinished) {
                        if (!stateStruct->HasBareFlag(kHighPriority)) {
                            // The new state waits for the animation to finish.
                            m_entity->WriteHashTag(kDelayedState, state);
                            m_entity->WriteHashTag(kDelayedTable, table);
                            table = kPedStateLogicTable;
                            state = kDelayWaitAnim;
                            script = kDelayWaitAnimScript;
                            updateType = kNoMoveUpdate;
                        }
                    } else if (Scripts::ScriptExists(exitAction)) {
                        Scripts::LaunchScript(exitAction, nullptr, m_entity, false, nullptr);
                    }
                }
                if (oldState->HasBareFlag(kHideBoardGeom))
                    HideBoard(0);
            }
        }
    }
    m_stateTable = table;
    m_curPhase = state;
    m_updateType = updateType;
    m_entity->ReplaceScript(script, params);
    if (Scripts::CScript* aux = m_auxScript) {
        DeleteScript(aux);
        m_auxScript = nullptr;
    }
    InitializeMovement();
    if (stateStruct->HasBareFlag(kHideBoardGeom))
        HideBoard(1);
    if (m_entity->m_ownScript)
        m_entity->m_ownScript->TickFrame();
    return true;
}

// 0x004F2240
void CPedestrianBrainModule::ActivateAuxScript(uint32_t script, Scripts::CStruct* params)
{
    if (!script)
        return;
    if (!m_auxScript)
        m_auxScript = NewScript();
    m_auxScript->BindScript(script, params, m_entity);
    if (m_auxScript->HasAttachedScript()) {
        m_auxScript->TickFrame();
        return;
    }
    if (Scripts::CScript* aux = m_auxScript) {
        DeleteScript(aux);
        m_auxScript = nullptr;
    }
}

// 0x004F1E10
bool CPedestrianBrainModule::Bail()
{
    if (Motion(m_entity)->m_motionFlags & 0x100)
        return false;
    if (m_storedMaxVelocity > 0.0f)
        Motion(m_entity)->m_pathMaxSpeed = m_storedMaxVelocity;
    ChangePhase((m_flagBits & PED_FLAG_GRINDING) ? kStateBailGrind : kStateBail, kSkaterPedStateTable, nullptr);
    m_flagBits &= ~(PED_FLAG_GRINDING | PED_FLAG_SPINNING | PED_FLAG_VERT_ROTATING);
    return true;
}

// 0x004EE2C0
const char* ProfileVoice()
{
    CRiderIdentity* profile = g_profileRoster->FetchActiveAccount();
    auto* info = reinterpret_cast<Scripts::CStruct*>(profile->m_details);
    uint32_t voice;
    if (!info->ReadHashValue(kVoice, &voice, 0)) {
        int male = 0; // (the original's is uninitialised: is_male is asserted)
        info->FetchIntValue(kIsMale, &male, 1);
        return male == 1 ? "Male1" : "Female1";
    }
    Scripts::CArray* voices = Scripts::FetchList(kPlayerVoices);
    int count = static_cast<int>(voices->m_extent);
    for (int i = 0; i < count; ++i) {
        auto* entry = reinterpret_cast<Scripts::CStruct*>(voices->FetchPart(static_cast<uint32_t>(i)));
        uint32_t id;
        entry->ReadHashValue(kId, &id, 1);
        if (id == voice) {
            const char* name = nullptr;
            entry->FetchText(kStreamName, &name, 1);
            return name;
        }
    }
    return nullptr;
}

// --- script member functions ---

// 0x004F2040
bool CPedestrianBrainModule::Ped_SetLogicState(Scripts::CStruct* params, Scripts::CScript*)
{
    uint32_t state = 0, table = 0;
    params->ReadHashValue(0u, &state, 1);
    if (state == m_curPhase)
        return true;
    params->ReadHashValue(kTable, &table, 0);
    Scripts::CStruct* stateParams = nullptr;
    if (params)
        params->ReadSubStruct(kStateParams, &stateParams, 0);
    return ChangePhase(state, table, stateParams);
}

// 0x004EDD80
bool CPedestrianBrainModule::Ped_GetLogicState(Scripts::CStruct*, Scripts::CScript* script)
{
    script->m_args->PutNameKey(kLogicState, m_curPhase);
    return true;
}

// 0x004F24B0
bool CPedestrianBrainModule::Ped_StartMoving(Scripts::CStruct*, Scripts::CScript*)
{
    if (m_flagBits & PED_FLAG_STOPPING) {
        ActivateAuxScript(kStartMoving, nullptr);
        m_flagBits &= ~PED_FLAG_STOPPING;
    }
    m_flagBits |= PED_FLAG_MOVING;
    return true;
}

// 0x004ED150
bool CPedestrianBrainModule::Ped_StopMoving(Scripts::CStruct*, Scripts::CScript*)
{
    m_flagBits &= ~PED_FLAG_MOVING;
    return true;
}

// 0x004ED160
bool CPedestrianBrainModule::Ped_IsMoving(Scripts::CStruct*, Scripts::CScript*) { return (m_flagBits & PED_FLAG_MOVING) != 0; }

// 0x004ED170
bool CPedestrianBrainModule::Ped_SetStickDistances(Scripts::CStruct* params, Scripts::CScript*)
{
    params->FetchFloat(kDistAboveCmd, &m_stickAbove, 0);
    params->FetchFloat(kDistBelowCmd, &m_stickBelow, 0);
    DebugOut::Printf("StickToGround distances set to %f (above), %f (below)\n", static_cast<double>(m_stickAbove),
                static_cast<double>(m_stickBelow));
    return true;
}

// 0x004ED1D0
bool CPedestrianBrainModule::Ped_SetIsGrinding(Scripts::CStruct* params, Scripts::CScript*)
{
    int grinding = 0;
    params->FetchIntValue(0u, &grinding, 1);
    if (grinding)
        m_flagBits |= PED_FLAG_GRINDING;
    else
        m_flagBits &= ~PED_FLAG_GRINDING;
    return true;
}

// 0x004ED220
bool CPedestrianBrainModule::Ped_SetIsBailing(Scripts::CStruct* params, Scripts::CScript*)
{
    // (The original reads the value over its `params` argument: missing - it's asserted - it stays that pointer.)
    int bailing = static_cast<int>(reinterpret_cast<uintptr_t>(params));
    params->FetchIntValue(0u, &bailing, 1);
    if (params->HasBareFlag(kGrind)) {
        if (bailing)
            m_flagBits |= PED_FLAG_GRIND_BAILING;
        else
            m_flagBits &= ~PED_FLAG_GRIND_BAILING;
    } else if (bailing) {
        m_flagBits |= PED_FLAG_BAILING;
    } else {
        m_flagBits &= ~PED_FLAG_BAILING;
    }
    return true;
}

// 0x004ED2A0
bool CPedestrianBrainModule::Ped_NearVertNode(Scripts::CStruct*, Scripts::CScript*)
{
    return (m_flagBits & (PED_FLAG_VERT_NEXT | PED_FLAG_VERT_LAST | PED_FLAG_VERT_AFTER)) != 0;
}

// 0x004EDDA0
bool CPedestrianBrainModule::Ped_GetCurrentVelocity(Scripts::CStruct*, Scripts::CScript* script)
{
    script->m_args->PushFloat(kVelocity, Motion(m_entity)->m_pathSpeed);
    return true;
}

// 0x004EDDD0
bool CPedestrianBrainModule::Ped_StoreMaxVelocity(Scripts::CStruct*, Scripts::CScript*)
{
    m_storedMaxVelocity = Motion(m_entity)->m_pathMaxSpeed;
    return true;
}

// 0x004EDE00
bool CPedestrianBrainModule::Ped_GetOriginalMaxVelocity(Scripts::CStruct*, Scripts::CScript* script)
{
    script->m_args->PushFloat(kOriginalMaxVelocity, m_storedMaxVelocity);
    return true;
}

// 0x004EDE20
bool CPedestrianBrainModule::Ped_ApplySpeedBoost(Scripts::CStruct* params, Scripts::CScript*)
{
    float boost = kDefaultBoost;
    if (params->FetchFloat(kPercent, &boost, 0))
        boost = static_cast<float>(static_cast<double>(boost) * kPercentScale + 1.0);
    if (CKinematicsModule* motion = Motion(m_entity)) {
        double speed = static_cast<double>(boost) * motion->m_pathMaxSpeed;
        motion->m_pathMaxSpeed = static_cast<float>(speed);
        if (speed > kMaxBoostedSpeed)
            motion->m_pathMaxSpeed = kMaxBoostedSpeed;
    }
    return true;
}

// 0x004EDEA0
bool CPedestrianBrainModule::Ped_RemoveSpeedBoost(Scripts::CStruct*, Scripts::CScript*)
{
    if (CKinematicsModule* motion = Motion(m_entity))
        motion->m_pathSpeed = motion->m_pathMaxSpeed;
    return true;
}

// 0x004EE100
bool CPedestrianBrainModule::Ped_SetIsSkater(Scripts::CStruct* params, Scripts::CScript*)
{
    // (The value is read over the `params` argument: missing - it's asserted - it stays that pointer.)
    int skater = static_cast<int>(reinterpret_cast<uintptr_t>(params));
    params->FetchIntValue(0u, &skater, 1);
    if (!skater) {
        m_kind = 0;
        return true;
    }
    m_kind = 2;
    if (CKinematicsModule* motion = Motion(m_entity)) {
        motion->m_pathAcceleration = Scripts::FetchFloat(kDeltaV);
        motion->m_pathDeceleration = Scripts::FetchFloat(kDeltaV);
    }
    return true;
}

// 0x004EE180
bool CPedestrianBrainModule::Ped_PlayJumpSound(Scripts::CStruct*, Scripts::CScript*)
{
    if (auto* sound = static_cast<CRiderSfxPlayer*>(m_entity->FetchModule(kSkaterSound)))
        sound->PlayJumpHere(m_speedRatio);
    return true;
}

// 0x004EE1B0
bool CPedestrianBrainModule::Ped_PlayLandSound(Scripts::CStruct*, Scripts::CScript*)
{
    if (auto* sound = static_cast<CRiderSfxPlayer*>(m_entity->FetchModule(kSkaterSound)))
        sound->PlayLandHere(m_speedRatio);
    return true;
}

// 0x004EE1E0
bool CPedestrianBrainModule::Ped_GetCurrentNodeNames(Scripts::CStruct*, Scripts::CScript* script)
{
    script->m_args->PutNameKey(kNodeTo, m_nextNode);
    script->m_args->PutNameKey(kNodeFrom, m_listLink);
    script->m_args->PutNameKey(kNodeAfter, m_nodeAfter);
    return true;
}

// 0x004EE230
bool CPedestrianBrainModule::Ped_GetGender(Scripts::CStruct*, Scripts::CScript* script)
{
    script->m_args->PutNameKey(kGender, m_female == 1 ? kFemale : kMale);
    return true;
}

// 0x004F24E0
bool CPedestrianBrainModule::Ped_ActivateAuxScript(Scripts::CStruct* params, Scripts::CScript*)
{
    uint32_t name = 0;
    if (params->ReadHashValue(kName, &name, 1)) {
        Scripts::CStruct* scriptParams = nullptr;
        params->ReadSubStruct(kParams, &scriptParams, 0);
        ActivateAuxScript(name, scriptParams);
    }
    return true;
}

// 0x004F20F0
bool CPedestrianBrainModule::Ped_DeactivateAuxScript(Scripts::CStruct*, Scripts::CScript*)
{
    if (Scripts::CScript* aux = m_auxScript) {
        DeleteScript(aux);
        m_auxScript = nullptr;
    }
    return true;
}

// 0x004EE280
bool CPedestrianBrainModule::Ped_HideBoard(Scripts::CStruct* params, Scripts::CScript*)
{
    int hide = 0;
    params->FetchIntValue(0u, &hide, 0);
    return HideBoard(hide);
}

// 0x004F20E0
bool CPedestrianBrainModule::Ped_Bail(Scripts::CStruct*, Scripts::CScript*)
{
    Bail();
    return true;
}

// 0x004EE3A0
bool CPedestrianBrainModule::Ped_GetVoicePrefix(Scripts::CStruct*, Scripts::CScript* script)
{
    CGameEntity* object = m_entity;
    uint32_t profileName;
    if (object->m_tagTable && object->m_tagTable->ReadHashValue(kProfileName, &profileName, 0)) {
        if (profileName == kCustom) {
            if (const char* voice = ProfileVoice()) {
                script->m_args->AppendWord(kVoicePrefix, voice);
                return true;
            }
        } else {
            Scripts::CArray* skaters = Scripts::FetchList(kMasterSkaterList);
            int count = static_cast<int>(skaters->m_extent);
            for (int i = 0; i < count; ++i) {
                auto* skater = reinterpret_cast<Scripts::CStruct*>(skaters->FetchPart(static_cast<uint32_t>(i)));
                uint32_t name;
                skater->ReadHashValue(kName, &name, 1);
                if (name == profileName) {
                    // (Read over the slot that held `this`: missing - it's asserted - it stays that pointer.)
                    const char* lastName = reinterpret_cast<const char*>(this);
                    skater->FetchText(kLastName, &lastName, 1);
                    script->m_args->AppendWord(kVoicePrefix, lastName);
                    return true;
                }
            }
        }
    }
    // The ped's node's profile's voice_prefix.
    if (Scripts::CStruct* node = LevelNodes::FetchVertex(m_entity->m_ident, true)) {
        Scripts::CStruct* profile = nullptr;
        node->ReadSubStruct(kProfile, &profile, 0);
        if (profile) {
            const char* prefix = nullptr;
            profile->FetchText(kVoicePrefixParam, &prefix, 0);
            if (prefix) {
                script->m_args->AppendWord(kVoicePrefix, prefix);
                return true;
            }
        }
    }
    return false;
}

} // namespace Obj

namespace GameObjects {
namespace {

static VtableSlot kVtable_slots[] = {&GameObjects::CPedestrianBrainModule::Teardown,
         &GameObjects::CPedestrianBrainModule::TickFrame,
         &GameObjects::CPedestrianBrainModule::ConfigureFromData,
         Stubs::DoNothing,
         Stubs::DoNothing,
         GameObjects::ReapplyFromData,
         Stubs::IgnoreArguments2,
         &GameObjects::CObjectModule::HoldOff,
         Stubs::IgnoreArgument,
         Stubs::DoNothing,
         Stubs::IgnoreArgument,
         &GameObjects::CObjectModule::DispatchScriptMethod,
         &GameObjects::CPedestrianBrainModule::InvokeMethodByName,
         &GameObjects::CPedestrianBrainModule::GenericMoveUpdate,
         &GameObjects::CPedestrianBrainModule::SkaterMoveUpdate,
         &GameObjects::CPedestrianBrainModule::NoMoveUpdate};
void** const kVtable = reinterpret_cast<void**>(kVtable_slots);

} // namespace
} // namespace Obj
