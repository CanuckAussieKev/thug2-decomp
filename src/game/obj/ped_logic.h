#pragma once

#include <cstddef>
#include <cstdint>

#include "game/mth/matrix.h"
#include "game/mth/vector.h"
#include "game/obj/object.h"

namespace Script {
class CScript;
class CStruct;
} // namespace Script

namespace Obj {

class CObjectGroup;

// The motion component's last ground triangle (CMotionComponent +0x40): three corners.
struct GroundTriangle {
    // 0x004EDC10 (not a Ghidra function): the copy assignment (ECX = this, RET 4; returns this).
    GroundTriangle& Assign(const GroundTriangle& source);

    Mth::Vector corners[3];
};
static_assert(sizeof(GroundTriangle) == 0x30, "layout must match the original");

// CPedLogicComponent::m_flags (+0x20)
constexpr uint32_t PED_FLAG_BOARD_HIDEABLE = 0x1;   // its profile's board has a desc_id: hide_boardgeom hides it
constexpr uint32_t PED_FLAG_MOVING = 0x2;           // following its path (Ped_StartMoving / Ped_StopMoving)
constexpr uint32_t PED_FLAG_GRINDING = 0x4;         // Ped_SetIsGrinding
constexpr uint32_t PED_FLAG_VERT_NEXT = 0x8;        // the node it's heading for has a vert SkateAction
constexpr uint32_t PED_FLAG_VERT_LAST = 0x10;       // the node it came from had one
constexpr uint32_t PED_FLAG_VERT_AFTER = 0x20;      // the node after next has one
constexpr uint32_t PED_FLAG_BAILING = 0x40;         // Ped_SetIsBailing
constexpr uint32_t PED_FLAG_GRIND_BAILING = 0x80;   // Ped_SetIsBailing Grind (bailing off a grind)
constexpr uint32_t PED_FLAG_CROUCH = 0x100;         // a jump at the next node: crouch when near (STATE_jumpcrouch)
constexpr uint32_t PED_FLAG_SPINNING = 0x200;       // a jump's spin (m_spinAngle) in progress
constexpr uint32_t PED_FLAG_VERT_ROTATING = 0x400;  // a jump's turn over a spine in progress
constexpr uint32_t PED_FLAG_JUMPING_TO_NODE = 0x800; // JumpToNextNode
constexpr uint32_t PED_FLAG_STOPPING = 0x1000;      // the stop state: ped_skater_start_moving runs when it moves
constexpr uint32_t PED_FLAG_VERT_LIP = 0x8000;      // the node it's heading for has a Vert_Lip action
constexpr uint32_t PED_FLAG_BOARD_HIDDEN = 0x20000;  // its board is hidden

// The PedLogic component (type 0x6EFDDFB8 "pedlogic", vtable 0x0064B7A8, 0x1CC bytes; made by 0x004F0B10): a ped's
// brain - walkers and the skating peds. It runs a logic state from a state table (Ped_StateLogic_Table, the object's
// UniqueStateTable tag, SkaterPed_StateTable...: each state a structure naming its script_ped_state, the script the
// object runs, and its ped_update_type, the move update run each frame), follows its path of waypoint nodes
// (choosing among their links by Class / Type, SkateAction and Priority), steers round other peds on the path and
// away from the path's edges, and for skaters does the jumps, grinds, lip tricks and bails the nodes' SkateActions
// ask for. Its script member functions are in the table at 0x00683730 (filled by 0x00642B90): Ped_SetLogicState ...
// Ped_GetVoicePrefix.
//
// Vtable: 0 the scalar deleting destructor, 1 Update, 2 InitFromStructure; 3 / 4 / 9 0x005480A0, 5 0x004E3F90, 6
// 0x0048DCD0, 7 0x0045D760, 8 / 10 0x005BAB90, 11 0x0042BB20 (shared), 12 CallMemberFunction 0x004ECFD0; then its
// own: 13 (+0x34) the generic move update 0x004F2A10, 14 (+0x38) the skater move update 0x004F2AD0, 15 (+0x3C) the
// no-move update 0x004ED4A0.
class CPedLogicComponent : public CBaseComponent {
public:
    static constexpr uint32_t kType = 0x6EFDDFB8;

    // 0x004F0B10 (registered with the composite object manager by 0x004E4860): a new one (0 if out of memory).
    static CBaseComponent* __cdecl Create();
    // 0x004EE4F0: the base's constructor, its vtable and type; no avoidance or bias, the position history at the
    // origin, ped_skater_stick_dist_above / below, the states "" (their checksums), no aux script, the next waypoint at
    // the origin. Returns this.
    CPedLogicComponent* Construct();
    // 0x004F2120: the aux script deleted, then the base's body.
    void DestroyBody();
    // 0x004F2540 (vtable slot 0)
    CPedLogicComponent* Destroy(unsigned int flags);
    // 0x004ED2C0 (vtable slot 2): female if SkeletonName is Ped_Female; PED_FLAG_BOARD_HIDEABLE if profile's board has
    // a desc_id; the start node (Name, asserted) and its position; up = the object's up row (all three); DistAbove /
    // DistBelow (the stick distances); the display matrix kept.
    void InitFromStructure(Script::CStruct* params);
    // 0x004F2190 (vtable slot 1): the frame time, then the move update ped_update_type names; the aux script run.
    void Update();

    // 0x004ED140: the logic state (a state table's member name; shot, ...).
    uint32_t GetLogicState() { return m_state; }

    // 0x004F0B70: the logic state `state` of `table` (0: the object's UniqueStateTable tag, else Ped_StateLogic_Table;
    // a state the table hasn't is looked for in Ped_StateLogic_Table) set: the old state's exit_state_action (a script
    // run, or wait_anim_finished: unless the new state is high_priority, the new one is delayed - DelayedState /
    // DelayedTable tags - and STATE_DELAY_waitanim runs), the board shown / hidden by hide_boardgeom, the object's
    // script switched to script_ped_state with `params`, the aux script killed, ped_initialize_movement run. False if
    // the object hasn't its ped_init tag or the state isn't found.
    bool SetState(uint32_t state, uint32_t table, Script::CStruct* params);
    // 0x004ED460: ped_initialize_movement run on the object; moving if the update type is the generic or the skater
    // move update.
    void InitializeMovement();
    // 0x004EDD00: the board (the model's geom board) hidden or shown, if PED_FLAG_BOARD_HIDEABLE (else false).
    bool HideBoard(int hide);
    // 0x004EDC60: the looping sound's terrain (+0x74, marked changed) and the skater sound's (+0x18) set to `state`.
    void SetSoundState(int state);
    // 0x004EDCB0: the looping sound's terrain (+0x60, marked changed) and the skater sound's (+0x1C, +0x1D) set.
    void SetSoundTerrain(uint8_t terrain);
    // 0x004F2240: the aux script (made if there's none) set to `script` with `params` and run once; deleted if it
    // ends at once. Nothing for no script.
    void ActivateAuxScript(uint32_t script, Script::CStruct* params);
    // 0x004EFB10: a new structure with `params`, then the object's tags, appended (the caller deletes it).
    Script::CStruct* StateParams(Script::CStruct* params);
    // 0x004ED4F0: the object's position pushed onto the position history (the oldest dropped).
    void PushPosition();
    // 0x004F1E10: a bail (STATE_bailgrind if grinding, else STATE_bail, of SkaterPed_StateTable; the stored max
    // velocity given back to the motion component if there is one), unless the motion component's flag 0x100 is set
    // (then false). Grinding, spinning and the spine turn end.
    bool Bail();

    // --- the move updates (vtable slots 13-15) ---
    // 0x004ED4A0 (slot 15, no_move_update): the display matrix kept if the motion component is rotating, else put back.
    void NoMoveUpdate();

    // --- script member functions (the table at 0x00683730) ---
    bool Ped_SetLogicState(Script::CStruct* params, Script::CScript* script);      // 0x004F2040
    bool Ped_GetLogicState(Script::CStruct* params, Script::CScript* script);      // 0x004EDD80
    bool Ped_StartMoving(Script::CStruct* params, Script::CScript* script);        // 0x004F24B0
    bool Ped_StopMoving(Script::CStruct* params, Script::CScript* script);         // 0x004ED150
    bool Ped_IsMoving(Script::CStruct* params, Script::CScript* script);           // 0x004ED160 [0x68D298B0]
    bool Ped_SetStickDistances(Script::CStruct* params, Script::CScript* script);  // 0x004ED170 [0x2CC086DD]
    bool Ped_SetIsGrinding(Script::CStruct* params, Script::CScript* script);      // 0x004ED1D0
    bool Ped_GetCurrentVelocity(Script::CStruct* params, Script::CScript* script); // 0x004EDDA0
    bool Ped_StoreMaxVelocity(Script::CStruct* params, Script::CScript* script);   // 0x004EDDD0
    bool Ped_GetOriginalMaxVelocity(Script::CStruct* params, Script::CScript* script); // 0x004EDE00
    bool Ped_ApplySpeedBoost(Script::CStruct* params, Script::CScript* script);    // 0x004EDE20
    bool Ped_RemoveSpeedBoost(Script::CStruct* params, Script::CScript* script);   // 0x004EDEA0
    bool Ped_SetIsBailing(Script::CStruct* params, Script::CScript* script);       // 0x004ED220
    bool Ped_NearVertNode(Script::CStruct* params, Script::CScript* script);       // 0x004ED2A0
    bool Ped_SetIsSkater(Script::CStruct* params, Script::CScript* script);        // 0x004EE100
    bool Ped_PlayJumpSound(Script::CStruct* params, Script::CScript* script);      // 0x004EE180
    bool Ped_PlayLandSound(Script::CStruct* params, Script::CScript* script);      // 0x004EE1B0
    bool Ped_GetCurrentNodeNames(Script::CStruct* params, Script::CScript* script); // 0x004EE1E0
    bool Ped_GetGender(Script::CStruct* params, Script::CScript* script);          // 0x004EE230
    bool Ped_ActivateAuxScript(Script::CStruct* params, Script::CScript* script);  // 0x004F24E0
    bool Ped_DeactivateAuxScript(Script::CStruct* params, Script::CScript* script); // 0x004F20F0
    bool Ped_HideBoard(Script::CStruct* params, Script::CScript* script);          // 0x004EE280
    bool Ped_GetVoicePrefix(Script::CStruct* params, Script::CScript* script);     // 0x004EE3A0
    bool Ped_Bail(Script::CStruct* params, Script::CScript* script);               // 0x004F20E0 [0xEE24A410]

    uint32_t m_kind;               // +0x18 0 a walker, 2 a skater (Ped_SetIsSkater)
    uint32_t m_female;             // +0x1C 1: SkeletonName is Ped_Female
    uint32_t m_flags;              // +0x20 PED_FLAG_*
    uint32_t m_state;              // +0x24 the logic state (LogicState)
    uint32_t m_stateTable;         // +0x28 its table
    uint32_t m_updateType;         // +0x2C ped_update_type (the move update run each frame)
    Script::CScript* mp_auxScript; // +0x30
    float m_avoid[4];              // +0x34 the steering this frame, per quarter turn (cos-weighted; -1..1)
    float m_bias[4];               // +0x44 the lasting steering bias, per quarter turn
    uint32_t m_node;               // +0x54 the node it came from (a name)
    uint32_t m_nextNode;           // +0x58 the node it's heading for
    uint32_t m_nodeAfter;          // +0x5C the node after that (chosen ahead)
    Mth::Vector m_nodePos;         // +0x60 m_node's position
    Mth::Vector m_nextNodePos;     // +0x70
    Mth::Vector m_nodeAfterPos;    // +0x80
    int m_turnFrame;               // +0x90 frames into the turn towards the node after
    int m_turnFrames;              // +0x94 the turn's length (0: not turning)
    CObjectGroup* mp_peds;         // +0x98 the path manager's extra group (the other peds to avoid)
    float m_frameTime;             // +0x9C this frame's (seconds)
    Mth::Vector m_up;              // +0xA0 the up it blends from
    Mth::Vector m_upTarget;        // +0xB0 the up it blends to
    Mth::Vector m_upShown;         // +0xC0 the up it has
    float m_upBlend;               // +0xD0 1 .. 0 along the blend
    float m_stickAbove;            // +0xD4 DistAbove (ped_skater_stick_dist_above)
    float m_stickBelow;            // +0xD8 DistBelow (ped_skater_stick_dist_below)
    float m_storedMaxVelocity;     // +0xDC Ped_StoreMaxVelocity
    float m_speedFraction;         // +0xE0 the motion component's speed over its max speed (the looping sound's)
    int m_spinAngle;               // +0xE4 the jump's spin (degrees, signed)
    float m_spin;                  // +0xE8 how far it has spun (degrees)
    float m_spinTime;              // +0xEC the jump's length (seconds)
    float m_spinElapsed;           // +0xF0
    Mth::Matrix m_jumpMatrix;      // +0xF4 the display matrix at the take-off
    float m_vertRotation;          // +0x134 how far it has turned over the spine (degrees)
    float m_vertRotationTarget;    // +0x138
    Mth::Matrix m_savedMatrix;     // +0x13C the display matrix kept
    Mth::Vector m_history[5];      // +0x17C its last positions (the newest last)
};
static_assert(offsetof(CPedLogicComponent, m_kind) == 0x18 && offsetof(CPedLogicComponent, m_flags) == 0x20 &&
                  offsetof(CPedLogicComponent, mp_auxScript) == 0x30 && offsetof(CPedLogicComponent, m_avoid) == 0x34 &&
                  offsetof(CPedLogicComponent, m_bias) == 0x44 && offsetof(CPedLogicComponent, m_node) == 0x54 &&
                  offsetof(CPedLogicComponent, m_nodePos) == 0x60 && offsetof(CPedLogicComponent, m_turnFrame) == 0x90 &&
                  offsetof(CPedLogicComponent, mp_peds) == 0x98 && offsetof(CPedLogicComponent, m_up) == 0xA0 &&
                  offsetof(CPedLogicComponent, m_upBlend) == 0xD0 && offsetof(CPedLogicComponent, m_spinAngle) == 0xE4 &&
                  offsetof(CPedLogicComponent, m_jumpMatrix) == 0xF4 &&
                  offsetof(CPedLogicComponent, m_vertRotation) == 0x134 &&
                  offsetof(CPedLogicComponent, m_savedMatrix) == 0x13C &&
                  offsetof(CPedLogicComponent, m_history) == 0x17C && sizeof(CPedLogicComponent) == 0x1CC,
              "layout must match the original");

// 0x004EE2C0: the current profile's voice: its voice= picks the entry of player_voices with that id (its stream_name;
// null if none), else Male1 / Female1 by is_male.
const char* ProfileVoice();

} // namespace Obj
