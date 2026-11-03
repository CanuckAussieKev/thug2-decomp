// The PedLogic component (GameObjects::PedestrianBrainModule): see ped_logic.h. Its making, states, settings and script member
// functions; from the disassembly of 0x004ED140-0x004F2560 (the member functions 0x004ED150, 0x004ED160, 0x004ED1D0,
// 0x004ED220, 0x004ED2A0, 0x004EDD80, 0x004EDDA0, 0x004EDE00, 0x004EDE20, 0x004EDEA0, 0x004EDEC0, 0x004EE100,
// 0x004EE230, 0x004EE280, 0x004F20E0, 0x004F24E0 and 0x004F2770, and 0x004EDC10, aren't Ghidra functions).

#include "engine/objects/ped_logic.h"

#include <cstdint>
#include <new>
#include <gsl/pointers>
#include <array>

#include "engine/core/checksums.h"
#include "engine/core/dbg.h"
#include "engine/objects/model_component.h"
#include "engine/objects/motion.h"
#include "engine/objects/motion_path.h"
#include "engine/script/cscript.h"
#include "engine/script/names.h"
#include "engine/script/nodes.h"
#include "engine/script/param_struct.h"
#include "game/skate/profile_manager.h"
#include "game/rider/looping_sound.h"
#include "game/rider/skater_sound.h"
#include "engine/objects/car_physics.h"
#include "engine/objects/node_components.h"
#include "engine/objects/entity.h"
#include "engine/objects/camera_lookaround.h"
#include "engine/core/down_cast.h"
#include "engine/core/checked_index.h"

namespace objects {
namespace {
constexpr uint32_t kMotionFlagBailing = 0x100;
constexpr uint32_t kPedSetLogicState = 0x3685B765; // Ped_SetLogicState
constexpr uint32_t kPedLogic = 0x6EFDDFB8; // PedLogic
constexpr uint32_t kPedGetLogicState = 0x423B7B29; // Ped_GetLogicState
constexpr uint32_t kPedInitPath = 0xE9906815; // Ped_InitPath
constexpr uint32_t kPedStartMoving = 0x0D711542; // Ped_StartMoving
constexpr uint32_t kPedStopMoving = 0xCC85ADFE; // Ped_StopMoving
constexpr uint32_t kPedIsMoving = 0x68D298B0; // Ped_IsMoving
constexpr uint32_t kPedSetStickToGroundDist = 0x2CC086DD; // Ped_SetStickToGroundDist
constexpr uint32_t kPedSetIsGrinding = 0xC643E8F5; // Ped_SetIsGrinding
constexpr uint32_t kPedGetCurrentVelocity = 0x13713760; // Ped_GetCurrentVelocity
constexpr uint32_t kPedStoreMaxVelocity = 0xF5627596; // Ped_StoreMaxVelocity
constexpr uint32_t kPedGetOriginalMaxVelocity = 0xE575EFA9; // Ped_GetOriginalMaxVelocity
constexpr uint32_t kPedApplySpeedBoost = 0xB4445051; // Ped_ApplySpeedBoost
constexpr uint32_t kPedRemoveSpeedBoost = 0x744CD649; // Ped_RemoveSpeedBoost
constexpr uint32_t kPedBail = 0xEE24A410; // ped_bail
constexpr uint32_t kPedSetIsBailing = 0x0C076D74; // Ped_SetIsBailing
constexpr uint32_t kPedInitVertTrick = 0xDA7BE9E2; // Ped_InitVertTrick
constexpr uint32_t kPedHitWaypoint = 0x1AFFFFB3; // Ped_HitWaypoint
constexpr uint32_t kPedSetIsSkater = 0x5C794C57; // Ped_SetIsSkater
constexpr uint32_t kPedNearVertNode = 0x5BB23915; // Ped_NearVertNode
constexpr uint32_t kPedPlayJumpSound = 0x56D9F55F; // Ped_PlayJumpSound
constexpr uint32_t kPedPlayLandSound = 0x749DE914; // Ped_PlayLandSound
constexpr uint32_t kPedGetCurrentNodeNames = 0x58A588B5; // Ped_GetCurrentNodeNames
constexpr uint32_t kPedTurnToCurrentWaypointOrientation = 0x5CFBFB31; // Ped_TurnToCurrentWaypointOrientation
constexpr uint32_t kPedGetGender = 0x9C37FBF5; // Ped_GetGender
constexpr uint32_t kPedActivateAuxScript = 0x35D3CA5E; // Ped_ActivateAuxScript
constexpr uint32_t kPedDeactivateAuxScript = 0x9C209251; // Ped_DeactivateAuxScript
constexpr uint32_t kPedHideBoard = 0xC33754F7; // Ped_HideBoard
constexpr uint32_t kPedGetVoicePrefix = 0x5F041C7D; // Ped_GetVoicePrefix
} // namespace

std::array<script::MemberFunctionEntry, kPedLogicMemberFunctionCount>& ped_logic_member_functions()
{
    static std::array<script::MemberFunctionEntry, kPedLogicMemberFunctionCount> ped_logic_member_functions{{
        {.callsign_ = kPedSetLogicState, .owner_kind_ = kPedLogic}, {.callsign_ = kPedGetLogicState, .owner_kind_ = kPedLogic}, {.callsign_ = kPedInitPath, .owner_kind_ = kPedLogic}, {.callsign_ = kPedStartMoving, .owner_kind_ = kPedLogic},
        {.callsign_ = kPedStopMoving, .owner_kind_ = kPedLogic}, {.callsign_ = kPedIsMoving, .owner_kind_ = kPedLogic}, {.callsign_ = kPedSetStickToGroundDist, .owner_kind_ = kPedLogic}, {.callsign_ = kPedSetIsGrinding, .owner_kind_ = kPedLogic},
        {.callsign_ = kPedGetCurrentVelocity, .owner_kind_ = kPedLogic}, {.callsign_ = kPedStoreMaxVelocity, .owner_kind_ = kPedLogic}, {.callsign_ = kPedGetOriginalMaxVelocity, .owner_kind_ = kPedLogic}, {.callsign_ = kPedApplySpeedBoost, .owner_kind_ = kPedLogic},
        {.callsign_ = kPedRemoveSpeedBoost, .owner_kind_ = kPedLogic}, {.callsign_ = kPedBail, .owner_kind_ = kPedLogic}, {.callsign_ = kPedSetIsBailing, .owner_kind_ = kPedLogic}, {.callsign_ = kPedInitVertTrick, .owner_kind_ = kPedLogic},
        {.callsign_ = kPedHitWaypoint, .owner_kind_ = kPedLogic}, {.callsign_ = kPedSetIsSkater, .owner_kind_ = kPedLogic}, {.callsign_ = kPedNearVertNode, .owner_kind_ = kPedLogic}, {.callsign_ = kPedPlayJumpSound, .owner_kind_ = kPedLogic},
        {.callsign_ = kPedPlayLandSound, .owner_kind_ = kPedLogic}, {.callsign_ = kPedGetCurrentNodeNames, .owner_kind_ = kPedLogic}, {.callsign_ = kPedTurnToCurrentWaypointOrientation, .owner_kind_ = kPedLogic}, {.callsign_ = kPedGetGender, .owner_kind_ = kPedLogic},
        {.callsign_ = kPedActivateAuxScript, .owner_kind_ = kPedLogic}, {.callsign_ = kPedDeactivateAuxScript, .owner_kind_ = kPedLogic}, {.callsign_ = kPedHideBoard, .owner_kind_ = kPedLogic}, {.callsign_ = kPedGetVoicePrefix, .owner_kind_ = kPedLogic},
        {.callsign_ = 0, .owner_kind_ = kPedLogic}}};
    return ped_logic_member_functions;
}

namespace {

constexpr uint32_t kMotion = 0x0A015E17;
constexpr uint32_t kModel = 0x286A8D26;
constexpr uint32_t kSkaterLoopingSound = 0xB5F1E9D2;
constexpr uint32_t kSkaterSound = 0x187574FA;

constexpr uint32_t kNull = 0xDA3403B0;                                     // null (the checksum of "NULL")
constexpr uint32_t kSkeletonName = 0x09794932;                             // SkeletonName
constexpr uint32_t kPedFemale = 0x8642CA2A;                                // Ped_Female
constexpr uint32_t kProfile = 0x7EA855F0;                                  // profile
constexpr uint32_t kBoard = 0xA7A9D4B8;                                    // board
constexpr uint32_t kDescId = 0x4BB2084E;                                   // desc_id
constexpr uint32_t kName = 0xA1DC81F9;                                     // Name
constexpr uint32_t kDistAbove = 0x367C4A1B, kDistBelow = 0x86F46E7B;       // (configure_from_data's)
constexpr uint32_t kDistAboveCmd = 0xA8A5A3C7, kDistBelowCmd = 0x182D87A7; // DistAbove / DistBelow
constexpr uint32_t kStickAbove = 0x379D93D2;                               // ped_skater_stick_dist_above
constexpr uint32_t kStickBelow = 0x8715B7B2;                               // ped_skater_stick_dist_below
constexpr uint32_t kPedInitializeMovement = 0x1B7DE0FB;                    // ped_initialize_movement
constexpr uint32_t kGenericMoveUpdate = 0x5B4E8427;                        // generic_move_update
constexpr uint32_t kSkaterMoveUpdate = 0xA631E964;                         // skater_move_update
constexpr uint32_t kNoMoveUpdate = 0x520A0B40;                             // no_move_update
constexpr uint32_t kPedInit = 0x2115FC7A;                                  // ped_init (a tag)
constexpr uint32_t kUniqueStateTable = 0x1DD08028;                         // UniqueStateTable (a tag)
constexpr uint32_t kPedStateLogicTable = 0x51F1BB49;                       // Ped_StateLogic_Table
constexpr uint32_t kScriptPedState = 0x9958DCFD;                           // script_ped_state
constexpr uint32_t kPedUpdateType = 0x959E031F;                            // ped_update_type
constexpr uint32_t kExitStateAction = 0xC1868052;                          // exit_state_action
constexpr uint32_t kWaitAnimFinished = 0x2E64106C;                         // wait_anim_finished
constexpr uint32_t kHighPriority = 0x0030AC01;                             // high_priority
constexpr uint32_t kDelayedState = 0x927BA4D7;                             // DelayedState (a tag)
constexpr uint32_t kDelayedTable = 0xC7C1F96A;                             // DelayedTable (a tag)
constexpr uint32_t kDelayWaitAnim = 0xF4C1639E;                            // STATE_DELAY_waitanim
constexpr uint32_t kDelayWaitAnimScript = 0x9C6561DC;                      // STATECONTROL_DELAY_waitanim
constexpr uint32_t kHideBoardGeom = 0x1F22FA86;                            // hide_boardgeom
constexpr uint32_t kTable = 0x09D670B9;                                    // Table
constexpr uint32_t kStateParams = 0x889A999C;                              // StateParams
constexpr uint32_t kLogicState = 0x7BA5C584;                               // LogicState
constexpr uint32_t kGrind = 0x255ED86F;                                    // Grind
constexpr uint32_t kVelocity = 0x41272956;                                 // Velocity
constexpr uint32_t kOriginalMaxVelocity = 0xEB4E99B3;                      // original_max_velocity
constexpr uint32_t kPercent = 0x9E497FC6;                                  // percent
constexpr uint32_t kDeltaV = 0x904403EC;                                   // ped_skater_deltaV
constexpr uint32_t kNodeTo = 0xD14CD5BC, kNodeFrom = 0x8141D319, kNodeAfter = 0x3C746255; // node_to / node_from
constexpr uint32_t kGender = 0x38B8F5BD, kMale = 0xAA721F56, kFemale = 0xB58B7593;        // Gender: male / female
constexpr uint32_t kParams = 0x7031F10C;                                                  // Params
constexpr uint32_t kStartMoving = 0xBED339D4;                                             // ped_skater_start_moving
constexpr uint32_t kProfileName = 0x17140E6D;                                             // profile_name (a tag)
constexpr uint32_t kCustom = 0x0A7BE964;                                                  // Custom
constexpr uint32_t kMasterSkaterList = 0xF31E8FF8;                                        // master_skater_list
constexpr uint32_t kLastName = 0x37F745A5;                                                // last_name
constexpr uint32_t kVoicePrefix = 0xF75A5D96;                                             // VoicePrefix
constexpr uint32_t kVoicePrefixParam = 0xAB88FD0D;                                        // voice_prefix
constexpr uint32_t kVoice = 0x1804A7C4;                                                   // voice
constexpr uint32_t kPlayerVoices = 0xCA1DB456;                                            // player_voices
constexpr uint32_t kId = 0x40C698AF;                                                      // id
constexpr uint32_t kStreamName = 0x16CBB85A;                                              // stream_name
constexpr uint32_t kIsMale = 0x3F813177;                                                  // is_male
constexpr uint32_t kSkaterPedStateTable = 0x46196541;                                     // SkaterPed_StateTable
constexpr uint32_t kStateBail = 0x6259CA27, kStateBailGrind = 0x423431E1; // STATE_bail / STATE_bailgrind
constexpr uint32_t kLedgeMember = 0x315D9ED4; // (the member function the skater's core physics calls Ledge)

constexpr float kMaxBoostedSpeed = 1100.0f; // 0x0064A8CC
constexpr float kDefaultBoost = 1.25f;
constexpr float kPercentScale = 0.01f; // 0x0064544C

KinematicsModule* get_motion(GameEntity* object)
{
    return core::down_cast<KinematicsModule>(object->fetch_module(kMotion));
}

} // namespace

// 0x004EDC10
GroundTriangle& get_assign(GroundTriangle& triangle, const GroundTriangle& source)
{
    for (int i = 0; i < 3; ++i)
        core::at(triangle.corners, i) = core::at(source.corners, i);
    return triangle;
}

// 0x004F0B10
gsl::owner<ObjectModule*> PedestrianBrainModule::fabricate() { return new PedestrianBrainModule; }

// 0x004ECFB0
void PedestrianBrainModule::publish_method_table()
{
    script::register_member_function_table(ped_logic_member_functions());
}

// 0x004ECFD0
int PedestrianBrainModule::invoke_method_by_name(uint32_t name, script::Struct* /*params*/, script::Script* /*script*/)
{
    return name == kLedgeMember ? 1 : 2;
}

// 0x004EE4F0
PedestrianBrainModule::PedestrianBrainModule()
    : cur_phase_(checksums::hash_from_text("NULL")), state_table_(checksums::hash_from_text("NULL")),
      update_type_(checksums::hash_from_text("NULL")), stick_above_(script::fetch_float(kStickAbove)),
      stick_below_(script::fetch_float(kStickBelow))
{
    set_kind_tag(kType);
    for (float& bias : bias_)
        bias = 0.0f;
    for (maths::Vec4& position : past_frames_)
        position = {.x = 0.0f, .y = 0.0f, .z = 0.0f, .w = 1.0f};
    for (float& avoid : avoid_)
        avoid = 0.0f;
    next_node_pos_ = {.x = 0.0f, .y = 0.0f, .z = 0.0f, .w = 1.0f};
}

// 0x004F2120 (0x004F2540 the deleting one)
PedestrianBrainModule::~PedestrianBrainModule()
{
    if (script::Script* script = aux_script_) {
        script::Script::destroy(script);
        aux_script_ = nullptr;
    }
}

// 0x004ED2C0
void PedestrianBrainModule::configure_from_data(script::Struct* params)
{
    kind_ = 0;
    female_ = 0;
    uint32_t skeleton = 0;
    if (params->read_hash_value(kSkeletonName, &skeleton, 0) && skeleton == kPedFemale)
        female_ = 1;
    script::Struct* profile = nullptr;
    params->read_sub_struct(kProfile, &profile, 0);
    if (profile) {
        script::Struct* board = nullptr;
        profile->read_sub_struct(kBoard, &board, 0);
        if (board) {
            uint32_t descriptor_id = 0;
            if (board->read_hash_value(kDescId, &descriptor_id, 0))
                flag_bits_ |= kPedFlagBoardHideable;
        }
    }
    params->read_hash_value(kName, &list_link_, 1);
    level_nodes::fetch_place(list_link_, &node_pos_);
    up_blend_ = 0.0f;
    up_target_ = core::at(entity()->basis_frame().row, 1);
    up_dir_ = up_target_;
    up_shown_ = up_dir_;
    params->fetch_float(kDistAbove, &stick_above_, 0);
    params->fetch_float(kDistBelow, &stick_below_, 0);
    saved_matrix_ = entity()->display_matrix();
}

// 0x004ED460
void PedestrianBrainModule::initialize_movement()
{
    script::launch_script(kPedInitializeMovement, nullptr, entity(), false, nullptr);
    if (update_type_ == kGenericMoveUpdate || update_type_ == kSkaterMoveUpdate)
        flag_bits_ |= kPedFlagMoving;
}

// 0x004ED4A0
void PedestrianBrainModule::no_move_update()
{
    GameEntity* object = entity();
    if (get_motion(object)->is_spinning())
        saved_matrix_ = object->basis_frame();
    else
        object->display_matrix() = saved_matrix_;
}

// 0x004ED4F0
void PedestrianBrainModule::push_position()
{
    for (int i = 0; i < 4; ++i)
        core::at(past_frames_, i) = core::at(past_frames_, i + 1);
    core::at(past_frames_, 4) = entity()->at_point();
}

// 0x004EDC60
void PedestrianBrainModule::set_sound_state(int state)
{
    if (auto* looping = core::down_cast<RiderRollingAudio>(entity()->fetch_module(kSkaterLoopingSound))) {
        looping->sound_changed() |= looping->cur_phase() != state;
        looping->set_cur_phase(state);
    }
    if (auto* sound = core::down_cast<RiderSfxPlayer>(entity()->fetch_module(kSkaterSound)))
        sound->set_cur_phase(state);
}

// 0x004EDCB0
void PedestrianBrainModule::set_sound_terrain(uint8_t terrain)
{
    if (auto* looping = core::down_cast<RiderRollingAudio>(entity()->fetch_module(kSkaterLoopingSound))) {
        looping->sound_changed() |= looping->surface_type() != terrain;
        looping->set_surface_type(terrain);
    }
    if (auto* sound = core::down_cast<RiderSfxPlayer>(entity()->fetch_module(kSkaterSound))) {
        sound->set_surface_type(terrain);
        sound->set_last_ground(terrain);
    }
}

// 0x004EDD00
bool PedestrianBrainModule::hide_board(int hide)
{
    if (!(flag_bits_ & kPedFlagBoardHideable))
        return false;
    if (auto* model = core::down_cast<MeshInstanceModule>(entity()->fetch_module(kModel))) {
        if (hide) {
            if (!(flag_bits_ & kPedFlagBoardHidden)) {
                model->conceal_mesh(kBoard, true, true);
                flag_bits_ |= kPedFlagBoardHidden;
                return true;
            }
        } else if (flag_bits_ & kPedFlagBoardHidden) {
            model->conceal_mesh(kBoard, false, true);
            flag_bits_ &= ~kPedFlagBoardHidden;
        }
    }
    return true;
}

// 0x004EFB10
gsl::owner<script::Struct*> PedestrianBrainModule::get_state_params(script::Struct* params)
{
    gsl::owner<script::Struct*> out = script::new_struct();
    out->append_structure_if_any(params);
    out->append_structure_if_any(entity()->tag_table());
    return out;
}

// 0x004F0B70
bool PedestrianBrainModule::change_phase(uint32_t state, uint32_t table, script::Struct* params)
{
    int ped_init = 0;
    entity()->read_int_tag(kPedInit, &ped_init);
    if (!ped_init)
        return false;
    if (!table && !entity()->read_hash_tag(kUniqueStateTable, &table))
        table = kPedStateLogicTable;
    script::Struct* table_struct = script::read_sub_struct(table);
    if (!table_struct)
        return false;
    script::Struct* state_struct = nullptr;
    if (!table_struct->read_sub_struct(state, &state_struct, 0)) {
        if (table == kPedStateLogicTable)
            return false;
        // Not in its own table: the common one's (looked for without a null check).
        table = kPedStateLogicTable;
        if (!script::read_sub_struct(kPedStateLogicTable)->read_sub_struct(state, &state_struct, 0))
            return false;
    }
    uint32_t script = 0;
    state_struct->read_hash_value(kScriptPedState, &script, 1);
    uint32_t update_type = kNoMoveUpdate;
    state_struct->read_hash_value(kPedUpdateType, &update_type, 0);
    // Leaving the old state.
    if (cur_phase_ != kNull) {
        if (script::Struct* old_table = script::read_sub_struct(state_table_)) {
            script::Struct* old_state = nullptr;
            if (old_table->read_sub_struct(cur_phase_, &old_state, 0)) {
                uint32_t exit_action = 0;
                if (old_state->read_hash_value(kExitStateAction, &exit_action, 0)) {
                    if (exit_action == kWaitAnimFinished) {
                        if (!state_struct->has_bare_flag(kHighPriority)) {
                            // The new state waits for the animation to finish.
                            entity()->write_hash_tag(kDelayedState, state);
                            entity()->write_hash_tag(kDelayedTable, table);
                            table = kPedStateLogicTable;
                            state = kDelayWaitAnim;
                            script = kDelayWaitAnimScript;
                            update_type = kNoMoveUpdate;
                        }
                    } else if (script::script_exists(exit_action)) {
                        script::launch_script(exit_action, nullptr, entity(), false, nullptr);
                    }
                }
                if (old_state->has_bare_flag(kHideBoardGeom))
                    hide_board(0);
            }
        }
    }
    state_table_ = table;
    cur_phase_ = state;
    update_type_ = update_type;
    entity()->replace_script(script, params);
    if (script::Script* aux = aux_script_) {
        script::Script::destroy(aux);
        aux_script_ = nullptr;
    }
    initialize_movement();
    if (state_struct->has_bare_flag(kHideBoardGeom))
        hide_board(1);
    if (entity()->own_script())
        entity()->own_script()->tick_frame();
    return true;
}

// 0x004F2240
void PedestrianBrainModule::activate_aux_script(uint32_t script, script::Struct* params)
{
    if (!script)
        return;
    if (!aux_script_)
        aux_script_ = script::Script::create();
    aux_script_->bind_script(script, params, entity());
    if (aux_script_->has_attached_script()) {
        aux_script_->tick_frame();
        return;
    }
    if (script::Script* aux = aux_script_) {
        script::Script::destroy(aux);
        aux_script_ = nullptr;
    }
}

// 0x004F1E10
bool PedestrianBrainModule::get_bail()
{
    if (get_motion(entity())->motion_flags() & kMotionFlagBailing)
        return false;
    if (stored_max_velocity_ > 0.0f)
        get_motion(entity())->set_path_max_speed(stored_max_velocity_);
    change_phase((flag_bits_ & kPedFlagGrinding) ? kStateBailGrind : kStateBail, kSkaterPedStateTable, nullptr);
    flag_bits_ &= ~(kPedFlagGrinding | kPedFlagSpinning | kPedFlagVertRotating);
    return true;
}

// 0x004EE2C0
const char* profile_voice()
{
    RiderIdentity* profile = profile_roster()->fetch_active_account();
    auto* info = profile->details_params();
    uint32_t voice = 0;
    if (!info->read_hash_value(kVoice, &voice, 0)) {
        int male = 0; // (the original's is uninitialised: is_male is asserted)
        info->fetch_int_value(kIsMale, &male, 1);
        return male == 1 ? "Male1" : "Female1";
    }
    script::Array* voices = script::fetch_list(kPlayerVoices);
    const int count = static_cast<int>(voices->extent());
    for (int i = 0; i < count; ++i) {
        auto* entry = voices->struct_at(static_cast<uint32_t>(i));
        uint32_t id = 0;
        entry->read_hash_value(kId, &id, 1);
        if (id == voice) {
            const char* name = nullptr;
            entry->fetch_text(kStreamName, &name, 1);
            return name;
        }
    }
    return nullptr;
}

// --- script member functions ---

// 0x004F2040
bool PedestrianBrainModule::ped_set_logic_state(script::Struct* params, script::Script* /*script*/)
{
    uint32_t state = 0;
    uint32_t table = 0;
    params->read_hash_value(0u, &state, 1);
    if (state == cur_phase_)
        return true;
    params->read_hash_value(kTable, &table, 0);
    script::Struct* state_params = nullptr;
    if (params)
        params->read_sub_struct(kStateParams, &state_params, 0);
    return change_phase(state, table, state_params);
}

// 0x004EDD80
bool PedestrianBrainModule::ped_get_logic_state(script::Struct* /*params*/, script::Script* script) const
{
    script->args()->put_name_key(checksums::Name{kLogicState}, cur_phase_);
    return true;
}

// 0x004F24B0
bool PedestrianBrainModule::ped_start_moving(script::Struct* /*params*/, script::Script* /*script*/)
{
    if (flag_bits_ & kPedFlagStopping) {
        activate_aux_script(kStartMoving, nullptr);
        flag_bits_ &= ~kPedFlagStopping;
    }
    flag_bits_ |= kPedFlagMoving;
    return true;
}

// 0x004ED150
bool PedestrianBrainModule::ped_stop_moving(script::Struct* /*params*/, script::Script* /*script*/)
{
    flag_bits_ &= ~kPedFlagMoving;
    return true;
}

// 0x004ED160
bool PedestrianBrainModule::ped_is_moving(script::Struct* /*params*/, script::Script* /*script*/) const
{
    return (flag_bits_ & kPedFlagMoving) != 0;
}

// 0x004ED170
bool PedestrianBrainModule::ped_set_stick_distances(script::Struct* params, script::Script* /*script*/)
{
    params->fetch_float(kDistAboveCmd, &stick_above_, 0);
    params->fetch_float(kDistBelowCmd, &stick_below_, 0);
    debug::printf("StickToGround distances set to %f (above), %f (below)\n", static_cast<double>(stick_above_),
                  static_cast<double>(stick_below_));
    return true;
}

// 0x004ED1D0
bool PedestrianBrainModule::ped_set_is_grinding(script::Struct* params, script::Script* /*script*/)
{
    int grinding = 0;
    params->fetch_int_value(0u, &grinding, 1);
    if (grinding)
        flag_bits_ |= kPedFlagGrinding;
    else
        flag_bits_ &= ~kPedFlagGrinding;
    return true;
}

// 0x004ED220
bool PedestrianBrainModule::ped_set_is_bailing(script::Struct* params, script::Script* /*script*/)
{
    // (The original reads the value over its `params` argument: missing - it's asserted - it stays that pointer.)
    int bailing = 1; // (missing: on - the original read an argument's stack slot, an address: never 0)
    params->fetch_int_value(0u, &bailing, 1);
    if (params->has_bare_flag(kGrind)) {
        if (bailing)
            flag_bits_ |= kPedFlagGrindBailing;
        else
            flag_bits_ &= ~kPedFlagGrindBailing;
    } else if (bailing) {
        flag_bits_ |= kPedFlagBailing;
    } else {
        flag_bits_ &= ~kPedFlagBailing;
    }
    return true;
}

// 0x004ED2A0
bool PedestrianBrainModule::ped_near_vert_node(script::Struct* /*params*/, script::Script* /*script*/) const
{
    return (flag_bits_ & (kPedFlagVertNext | kPedFlagVertLast | kPedFlagVertAfter)) != 0;
}

// 0x004EDDA0
bool PedestrianBrainModule::ped_get_current_velocity(script::Struct* /*params*/, script::Script* script)
{
    script->args()->push_float(checksums::Name{kVelocity}, get_motion(entity())->path_speed());
    return true;
}

// 0x004EDDD0
bool PedestrianBrainModule::ped_store_max_velocity(script::Struct* /*params*/, script::Script* /*script*/)
{
    stored_max_velocity_ = get_motion(entity())->path_max_speed();
    return true;
}

// 0x004EDE00
bool PedestrianBrainModule::ped_get_original_max_velocity(script::Struct* /*params*/, script::Script* script) const
{
    script->args()->push_float(checksums::Name{kOriginalMaxVelocity}, stored_max_velocity_);
    return true;
}

// 0x004EDE20
bool PedestrianBrainModule::ped_apply_speed_boost(script::Struct* params, script::Script* /*script*/)
{
    float boost = kDefaultBoost;
    if (params->fetch_float(kPercent, &boost, 0))
        boost = static_cast<float>(static_cast<double>(boost) * kPercentScale + 1.0);
    if (KinematicsModule* motion = get_motion(entity())) {
        const double speed = static_cast<double>(boost) * motion->path_max_speed();
        motion->set_path_max_speed(static_cast<float>(speed));
        if (speed > kMaxBoostedSpeed)
            motion->set_path_max_speed(kMaxBoostedSpeed);
    }
    return true;
}

// 0x004EDEA0
bool PedestrianBrainModule::ped_remove_speed_boost(script::Struct* /*params*/, script::Script* /*script*/)
{
    if (KinematicsModule* motion = get_motion(entity()))
        motion->set_path_speed(motion->path_max_speed());
    return true;
}

// 0x004EE100
bool PedestrianBrainModule::ped_set_is_skater(script::Struct* params, script::Script* /*script*/)
{
    // (The value is read over the `params` argument: missing - it's asserted - it stays that pointer.)
    int skater = 1; // (missing: on - the original read an argument's stack slot, an address: never 0)
    params->fetch_int_value(0u, &skater, 1);
    if (!skater) {
        kind_ = 0;
        return true;
    }
    kind_ = 2;
    if (KinematicsModule* motion = get_motion(entity())) {
        motion->assign_path_acceleration(script::fetch_float(kDeltaV));
        motion->assign_path_deceleration(script::fetch_float(kDeltaV));
    }
    return true;
}

// 0x004EE180
bool PedestrianBrainModule::ped_play_jump_sound(script::Struct* /*params*/, script::Script* /*script*/)
{
    if (auto* sound = core::down_cast<RiderSfxPlayer>(entity()->fetch_module(kSkaterSound)))
        sound->play_jump_here(speed_ratio_);
    return true;
}

// 0x004EE1B0
bool PedestrianBrainModule::ped_play_land_sound(script::Struct* /*params*/, script::Script* /*script*/)
{
    if (auto* sound = core::down_cast<RiderSfxPlayer>(entity()->fetch_module(kSkaterSound)))
        sound->play_land_here(speed_ratio_);
    return true;
}

// 0x004EE1E0
bool PedestrianBrainModule::ped_get_current_node_names(script::Struct* /*params*/, script::Script* script) const
{
    script->args()->put_name_key(checksums::Name{kNodeTo}, next_node_);
    script->args()->put_name_key(checksums::Name{kNodeFrom}, list_link_);
    script->args()->put_name_key(checksums::Name{kNodeAfter}, node_after_);
    return true;
}

// 0x004EE230
bool PedestrianBrainModule::ped_get_gender(script::Struct* /*params*/, script::Script* script) const
{
    script->args()->put_name_key(checksums::Name{kGender}, female_ == 1 ? kFemale : kMale);
    return true;
}

// 0x004F24E0
bool PedestrianBrainModule::ped_activate_aux_script(script::Struct* params, script::Script* /*script*/)
{
    uint32_t name = 0;
    if (params->read_hash_value(kName, &name, 1)) {
        script::Struct* script_params = nullptr;
        params->read_sub_struct(kParams, &script_params, 0);
        activate_aux_script(name, script_params);
    }
    return true;
}

// 0x004F20F0
bool PedestrianBrainModule::ped_deactivate_aux_script(script::Struct* /*params*/, script::Script* /*script*/)
{
    if (script::Script* aux = aux_script_) {
        script::Script::destroy(aux);
        aux_script_ = nullptr;
    }
    return true;
}

// 0x004EE280
bool PedestrianBrainModule::ped_hide_board(script::Struct* params, script::Script* /*script*/)
{
    int hide = 0;
    params->fetch_int_value(0u, &hide, 0);
    return hide_board(hide);
}

// 0x004F20E0
bool PedestrianBrainModule::ped_bail(script::Struct* /*params*/, script::Script* /*script*/)
{
    get_bail();
    return true;
}

// 0x004EE3A0
bool PedestrianBrainModule::ped_get_voice_prefix(script::Struct* /*params*/, script::Script* script)
{
    const GameEntity* object = entity();
    uint32_t profile_name = 0;
    if (object->tag_table() && object->tag_table()->read_hash_value(kProfileName, &profile_name, 0)) {
        if (profile_name == kCustom) {
            if (const char* voice = profile_voice()) {
                script->args()->append_word(kVoicePrefix, voice);
                return true;
            }
        } else {
            script::Array* skaters = script::fetch_list(kMasterSkaterList);
            const int count = static_cast<int>(skaters->extent());
            for (int i = 0; i < count; ++i) {
                auto* skater = skaters->struct_at(static_cast<uint32_t>(i));
                uint32_t name = 0;
                skater->read_hash_value(kName, &name, 1);
                if (name == profile_name) {
                    // (Read over the slot that held `this`: missing - it's asserted - the original's text was that
                    // pointer; empty here.)
                    const char* last_name = "";
                    skater->fetch_text(kLastName, &last_name, 1);
                    script->args()->append_word(kVoicePrefix, last_name);
                    return true;
                }
            }
        }
    }
    // The ped's node's profile's voice_prefix.
    if (script::Struct* node = level_nodes::fetch_vertex(entity()->ident(), true)) {
        script::Struct* profile = nullptr;
        node->read_sub_struct(kProfile, &profile, 0);
        if (profile) {
            const char* prefix = nullptr;
            profile->fetch_text(kVoicePrefixParam, &prefix, 0);
            if (prefix) {
                script->args()->append_word(kVoicePrefix, prefix);
                return true;
            }
        }
    }
    return false;
}

} // namespace objects
