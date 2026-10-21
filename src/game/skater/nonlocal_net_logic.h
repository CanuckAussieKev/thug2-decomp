#pragma once

#include <cstddef>
#include <cstdint>

#include "game/mth/vector.h"
#include "game/obj/object.h"

// The skater's non-local network logic component (SkaterNonLocalNetLogic, type 0x850EEF66, vtable 0x0064BFAC, 0x2C0
// bytes): another player's skater on this machine, moved from the states its client sends (the state history
// component keeps the last 20): played back a little behind, interpolated between states, and - when the states run
// late - extrapolated along the ground or a rail with the skater's own collision checks; its lighting, shadow, sound,
// animation events, flip / board rotation and state flags kept in step. From the disassembly of 0x005144B0-0x0051769F.
//
// Vtable: 0 Destroy, 1 Update, 3 Finalize, 10 the shared 0x005144C0 (ignores its argument), 12 the shared
// CallMemberFunction 0x0051ADC0.
namespace Obj {

class CSkaterNonLocalNetLogicComponent : public CBaseComponent {
public:
    static constexpr int kHistory = 20;

    // 0x005147F0 (registered by RegisterComponentTypes): a new one (0 if out of memory).
    static CBaseComponent* __cdecl Create();
    // 0x005144E0: the base, its vtable and type, no components, the two positions (0, 0, 0, 1), no updates. Returns
    // this.
    CSkaterNonLocalNetLogicComponent* Construct();
    // 0x005144B0: the destructor body (the base's).
    void DestroyBody();
    // 0x00514550 (vtable slot 0)
    CSkaterNonLocalNetLogicComponent* Destroy(unsigned int flags);
    // 0x00514580 (vtable +0x0C): the state history, state, end run, animation blender, flip-and-rotate, model and
    // shadow components found.
    void Finalize();
    // 0x00517270 (vtable +0x04): the frame: the light and shadow from the ground (UpdateGroundLighting), the state
    // played back (Interpolate); when this machine's player plays (not observing) in a game that extrapolates
    // (0x00534340, or firefight) and the history is full, extrapolated (on a rail ExtrapolateOnRail, else Extrapolate
    // and SnapToGround); the history's events (PlayHistoryEvents); the out-of-a-lip flag; driving in or out told to
    // the scripts (0x7853F8C4 / 0x0F02E44F with skaterId / control_type); the looping sound kept to the state.
    void Update();

    // 0x005144D0: the playback restarted (the synced time and the last event time cleared).
    void ResetPlayback();
    // 0x00514610: a feeler from just above the skater down 4800: on a hit (unless on a rail, and only on triangle
    // collision) the model's light set from the hit's vertex values; the shadow placed at the hit point and normal.
    void UpdateGroundLighting();
    // 0x00514850: kept to the ground: a feeler along the up row from Physics_Ground_Snap_Up above to 200 below; a
    // hit on skatable ground close enough (within the frame's movement's slope, at least Physics_Ground_Snap_Down)
    // and not too steep (Ground_stick_angle_forward) makes it the ground (the up row and the position); a hit not
    // skatable that the skater is below is left by one unit along its normal.
    void SnapToGround();
    // 0x00514D10: the velocity and the matrix turned (about y) towards `dir` by Wall_Bounce_Angle_Multiplier times
    // the angle between it and the right row (folded to at most pi / 2): that turn into `turn`; the angle returned.
    double TurnTowards(Mth::Vector dir, float* turn);
    // 0x00514E20: a wall met while extrapolating (`normal`, `point`): turned along it (slowed past
    // Wall_Bounce_Dont_Slow_Angle), put back at the point (Skater_First_Forward_Collision_Height up the up row, 6
    // out along the normal); if a feeler ahead (Skater_First_Forward_Collision_Length) hits again within 240, turned
    // round by what's left of a half turn and halved.
    void BounceOffWall(const Mth::Vector* normal, const Mth::Vector* point);

    uint32_t m_syncFrame;           // +0x18 the client app's frame when playback started (0: not yet)
    int m_syncTime;                 // +0x1C the history's time then, less the app's frames since
    uint32_t m_lastEventTime;       // +0x20 the history's events played up to here
    int m_entry;                    // +0x24 the history entry being played
    Mth::Vector m_playPos;          // +0x28 the position playback put the skater at
    Mth::Vector m_lastPlayPos;      // +0x38 the one before
    Mth::Vector m_lastPos;          // +0x48 the object's position before this frame
    Mth::Vector m_groundNormal;     // +0x58 the last ground snapped to
    float m_speeds[kHistory];       // +0x68 per update: the playback's speed
    float m_lags[kHistory];         // +0xB8 per update: how far behind the connection is
    float m_alignments[kHistory];   // +0x108 per update: how the move lines up with the local skater's
    Mth::Vector m_moves[kHistory];  // +0x158 per update: the playback's move
    int m_updates;                  // +0x298
    float m_frameSeconds;           // +0x29C this frame's length
    uint8_t m_wasDriving;           // +0x2A0
    uint8_t pad2A1[3];
    uint8_t* m_history;             // +0x2A4 SkaterStateHistory (0x18223FD6)
    CBaseComponent* m_animBlender;  // +0x2A8 (0x7149B753)
    uint8_t* m_model;               // +0x2AC Model (0x286A8D26)
    uint8_t* m_state;               // +0x2B0 SkaterState (0x43686585)
    uint8_t* m_endRun;              // +0x2B4 SkaterEndRun (0xB1D5A3E1)
    CBaseComponent* m_flipAndRotate;// +0x2B8 SkaterFlipAndRotate (0x488AF07A)
    CBaseComponent* m_shadow;       // +0x2BC Shadow (0x8A897DD2)
};
static_assert(offsetof(CSkaterNonLocalNetLogicComponent, m_syncFrame) == 0x18 &&
                  offsetof(CSkaterNonLocalNetLogicComponent, m_playPos) == 0x28 &&
                  offsetof(CSkaterNonLocalNetLogicComponent, m_lastPos) == 0x48 &&
                  offsetof(CSkaterNonLocalNetLogicComponent, m_groundNormal) == 0x58 &&
                  offsetof(CSkaterNonLocalNetLogicComponent, m_speeds) == 0x68 &&
                  offsetof(CSkaterNonLocalNetLogicComponent, m_lags) == 0xB8 &&
                  offsetof(CSkaterNonLocalNetLogicComponent, m_alignments) == 0x108 &&
                  offsetof(CSkaterNonLocalNetLogicComponent, m_moves) == 0x158 &&
                  offsetof(CSkaterNonLocalNetLogicComponent, m_updates) == 0x298 &&
                  offsetof(CSkaterNonLocalNetLogicComponent, m_history) == 0x2A4 &&
                  offsetof(CSkaterNonLocalNetLogicComponent, m_shadow) == 0x2BC &&
                  sizeof(CSkaterNonLocalNetLogicComponent) == 0x2C0,
              "layout must match the original (0x005147F0 allocates 0x2C0 bytes)");

} // namespace Obj
