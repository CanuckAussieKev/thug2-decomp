#pragma once

#include <cstddef>
#include <cstdint>

#include "game/gfx/particle_system.h"
#include "game/mth/matrix.h"
#include "game/mth/vector.h"
#include "game/obj/object.h"
#include "game/obj/smtptr.h"

namespace Script {
class CScript;
class CStruct;
struct CMemberFunctionEntry;
} // namespace Script

// Nx::CParticleSystem: game/gfx/particle_system.h.

namespace Obj {

// The flexible particle component (type 0xC5BCEB9E "flexibleparticle", vtable 0x00645FE4, 0x3C bytes; made by
// 0x0041E1E0): a particle system of the particle manager (0x006FA7C0) made from its InitFromStructure's parameters (or,
// with perm, the one of its name if there is one), moved with its object and given time each update; its member
// functions (setSpeedRange ... setpos, IsEmitting, ApplyEnvBrightness) are mostly passed on to the system.
//
// Vtable: 0 the scalar deleting destructor, 1 Update, 2 InitFromStructure, 3 Finalize, 8 Hide, 11 the member function
// table entry runner (shared with the animation blender), 12 CallMemberFunction; the rest the base's / shared (4 / 9
// 0x005480A0, 5 0x004E3F90, 6 0x0048DCD0, 7 / 10 0x005BAB90).
class CFlexibleParticleComponent : public CBaseComponent {
public:
    static constexpr uint32_t kType = 0xC5BCEB9E;

    // 0x0041E1E0 (registered with the composite object manager by 0x004E4860): a new one (0 if out of memory).
    static CBaseComponent* __cdecl Create();
    // 0x0041D6E0 (not a Ghidra function; registered with Create): its member functions' table (0x00683AC0; the
    // functions are put in by the static initialiser at 0x00642FD0).
    static void __cdecl RegisterMemberFunctions();
    // 0x0041DA00: the base's constructor, its vtable and type; no system, no name, nothing else. Returns this.
    CFlexibleParticleComponent* Construct();
    // 0x0041DA80: the system deleted (its vtable slot 0) and let go; then the base's body.
    void DestroyBody();
    // 0x0041E240 (vtable slot 0)
    CFlexibleParticleComponent* Destroy(unsigned int flags);
    // 0x0041DB00 (vtable slot 2): see the class; the parameters: name, max (a float, truncated; 1), MaxStreams (2),
    // texture, blendmode (0x515E298E), type (0xAAB555BB), 0x184197D5, 0xA605B89F (0), segments (8), 0x4E0A65EE (0.5),
    // history (1), perm (0), emitterIndependent, emit_rate, model (a string), useGlobalColorModulation, groupID, params
    // (a structure) with emitscript / UpdateScript, EnableRotate (follows the object's orientation), update_Brightness
    // with ObjID (the brightness of that object's model), UseRandomUVQuadrants, 0x5446AF14, FillPath, 0xDE45A90D,
    // priority, align_to. Without perm (or with no system of the name) a new system, named by the component's own
    // address. Also sets the particle manager's update time from the global Force_Particle_Update_Time.
    void InitFromStructure(Script::CStruct* params);
    // 0x0041D740 (vtable slot 3): the object's suspend component, which is told to start (0x00433E70).
    void Finalize();
    // 0x0041DF80 (vtable slot 1): no system: the object dies. Otherwise the time goes on; suspended (outside a network
    // game) the system only catches up once the time reaches Force_Particle_Update_Time; else, where the suspend
    // component says the object is to be updated, the system follows the object (its position; with EnableRotate its
    // rotation times the object's orientation becomes the display matrix it follows) and is given the time, elsewhere
    // 0x00496F60. A hidden object's system gets its +0x12F and +0x130 cleared.
    void Update();
    // 0x0041D760 (vtable slot 8): the system's +0x12F = `hide`.
    void Hide(bool hide);
    // 0x0041D780 (vtable slot 12): IsEmitting (0x2299A9F2; the system unchecked), ApplyEnvBrightness (0x96896315,
    // passed on); anything else 2.
    int CallMemberFunction(uint32_t name, Script::CStruct* params, Script::CScript* script);
    // 0x0041D700 (vtable slot 11; the animation blender's too): a member function table entry run with its name - its
    // member function pointer (on `this` moved by the entry's adjustment), or CallMemberFunction (vtable slot 12).
    int CallMemberFunctionEntry(Script::CMemberFunctionEntry* entry, Script::CStruct* params, Script::CScript* script);

    // --- the member functions scripts call (the table at 0x00683AC0; each is given its name) ---
    // 0x0041D7F0 (setSpeedRange, setEmitRange, setAngleSpread, 0xDB8621F7, setCircularEmit, 0xE3309AB5, setLODParams,
    // setForce, setParticleSize, SetParticleSizeScale, setLife, setEmitTarget, setEmitAngle, setColor, emit, EmitRate,
    // setpos): passed on to the system (0x00497B00); 0 without one.
    int PassToSystem(uint32_t name, Script::CStruct* params, Script::CScript* script);
    // 0x0041D820 (EmptySystem): the system's +0x8C cleared. 1.
    int EmptySystem(uint32_t name, Script::CStruct* params, Script::CScript* script);
    // 0x0041D840 (0x7F97BAD3, SetEmitScript): params (a structure) given to the system with emitscript (0x00497790)
    // and UpdateScript (0x00497900), each when given. 1.
    int SetScripts(uint32_t name, Script::CStruct* params, Script::CScript* script);
    // 0x0041D8F0 (ShouldEmit): whether the system is emitting or has an emit rate above 0 (0 without a system).
    int ShouldEmit(uint32_t name, Script::CStruct* params, Script::CScript* script);
    // 0x0041D920 / 0x0041D940 (EmitOn / EmitOff): the system emitting or not. 1.
    int EmitOn(uint32_t name, Script::CStruct* params, Script::CScript* script);
    int EmitOff(uint32_t name, Script::CStruct* params, Script::CScript* script);
    // 0x0041D960 (destroy): the system of its name destroyed by the particle manager - with ifEmpty (an integer) not 0,
    // once it's empty (0x0049ADB0), else now (0x0049AD90). 1.
    int DestroySystem(uint32_t name, Script::CStruct* params, Script::CScript* script);
    // 0x0041E190 (DestroyParticles): the system destroyed now and let go; the object dies. 1.
    int DestroyParticles(uint32_t name, Script::CStruct* params, Script::CScript* script);

    CSmtPtr<Nx::CParticleSystem> mp_system; // +0x18
    uint32_t m_name;                        // +0x24 name (or the component's address)
    uint32_t m_unknown28;                   // +0x28 0x184197D5 (given to the system's +0x78)
    float m_time;                           // +0x2C the time not yet given to the system
    uint8_t* mp_suspend;                    // +0x30 the suspend component
    CBaseComponent* mp_brightnessModel;     // +0x34 ObjID's model (update_Brightness)
    bool m_rotate;                          // +0x38 EnableRotate
    uint8_t pad39[3];
};
static_assert(offsetof(CFlexibleParticleComponent, mp_system) == 0x18 &&
                  offsetof(CFlexibleParticleComponent, m_name) == 0x24 &&
                  offsetof(CFlexibleParticleComponent, m_time) == 0x2C &&
                  offsetof(CFlexibleParticleComponent, mp_brightnessModel) == 0x34 &&
                  offsetof(CFlexibleParticleComponent, m_rotate) == 0x38 && sizeof(CFlexibleParticleComponent) == 0x3C,
              "layout must match the original");

} // namespace Obj
