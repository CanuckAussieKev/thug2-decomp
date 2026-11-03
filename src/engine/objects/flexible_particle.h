#pragma once

#include <cstddef>
#include <cstdint>

#include "engine/gfx/particle_system.h"
#include "engine/maths/mat4.h"
#include "engine/maths/vec4.h"
#include "engine/objects/entity.h"
#include "engine/objects/smtptr.h"
#include <gsl/pointers>
#include <array>

namespace script {
class Script;
class Struct;
struct MemberFunctionEntry;
} // namespace script

// render::EmitterInstance: engine/gfx/particle_system.h.

namespace objects {

class DormancyModule;

// The flexible particle component (type 0xC5BCEB9E "flexibleparticle", vtable 0x00645FE4, 0x3C bytes; made by
// 0x0041E1E0): a particle system of the particle manager (0x006FA7C0) made from its configure_from_data's parameters (or,
// with perm, the one of its name if there is one), moved with its object and given time each update; its member
// functions (setSpeedRange ... setpos, IsEmitting, ApplyEnvBrightness) are mostly passed on to the system.
//
// Vtable: 0 the scalar deleting destructor, 1 Update, 2 configure_from_data, 3 Finalize, 8 Hide, 11 the member function
// table entry runner (shared with the animation blender), 12 invoke_method_by_name; the rest the base's / shared (4 / 9
// 0x005480A0, 5 0x004E3F90, 6 0x0048DCD0, 7 / 10 0x005BAB90).
class FlexibleParticleComponent final : public ObjectModule {
public:
    FlexibleParticleComponent(const FlexibleParticleComponent&) = delete;
    FlexibleParticleComponent& operator=(const FlexibleParticleComponent&) = delete;
    static constexpr uint32_t kType = 0xC5BCEB9E;

    // 0x0041E1E0 (registered with the composite object manager by 0x004E4860): a new one (0 if out of memory).
    static gsl::owner<ObjectModule*> fabricate();
    // 0x0041D6E0 (not a Ghidra function; registered with Create): its member functions' table (0x00683AC0; the
    // functions are put in by the static initialiser at 0x00642FD0).
    static void publish_method_table();
    // 0x0041DA00: its type; no system, no name, nothing else.
    FlexibleParticleComponent();
    // 0x0041DA80: the system deleted (its vtable slot 0) and let go; then the base's body.
    ~FlexibleParticleComponent() override;
    void hold_off(bool /*suspend*/) override {} // (slot 7: never suspended)
    // 0x0041DB00 (vtable slot 2): see the class; the parameters: name, max (a float, truncated; 1), MaxStreams (2),
    // texture, blendmode (0x515E298E), type (0xAAB555BB), 0x184197D5, 0xA605B89F (0), segments (8), 0x4E0A65EE (0.5),
    // history (1), perm (0), emitterIndependent, emit_rate, model (a string), useGlobalColorModulation, groupID, params
    // (a structure) with emitscript / UpdateScript, EnableRotate (follows the object's orientation), update_Brightness
    // with ObjID (the brightness of that object's model), UseRandomUVQuadrants, 0x5446AF14, FillPath, 0xDE45A90D,
    // priority, align_to. Without perm (or with no system of the name) a new system, named by the component's own
    // address. Also sets the particle manager's update time from the global Force_Particle_Update_Time.
    void configure_from_data(script::Struct* params) override;
    // 0x0041D740 (vtable slot 3): the object's suspend component, which is told to start (0x00433E70).
    void seal() override;
    // 0x0041DF80 (vtable slot 1): no system: the object dies. Otherwise the time goes on; suspended (outside a network
    // game) the system only catches up once the time reaches Force_Particle_Update_Time; else, where the suspend
    // component says the object is to be updated, the system follows the object (its position; with EnableRotate its
    // rotation times the object's orientation becomes the display matrix it follows) and is given the time, elsewhere
    // 0x00496F60. A hidden object's system gets its +0x12F and +0x130 cleared.
    void tick_frame() override;
    // 0x0041D760 (vtable slot 8): the system's +0x12F = `hide`.
    void conceal(bool hide) override;
    // 0x0041D780 (vtable slot 12): IsEmitting (0x2299A9F2; the system unchecked), ApplyEnvBrightness (0x96896315,
    // passed on); anything else 2.
    int invoke_method_by_name(uint32_t name, script::Struct* params, script::Script* script) override;
    // 0x0041D700 (vtable slot 11; the animation blender's too): ObjectModule::dispatch_named_script_method.
    int invoke_method(const script::MemberFunctionEntry* entry, script::Struct* params, script::Script* script) override
    {
        return dispatch_named_script_method(entry, params, script);
    }

    // --- the member functions scripts call (the table at 0x00683AC0; each is given its name) ---
    // 0x0041D7F0 (setSpeedRange, setEmitRange, setAngleSpread, 0xDB8621F7, setCircularEmit, 0xE3309AB5, setLODParams,
    // setForce, setParticleSize, SetParticleSizeScale, setLife, setEmitTarget, setEmitAngle, setColor, emit, EmitRate,
    // setpos): passed on to the system (0x00497B00); 0 without one.
    int pass_to_system(uint32_t name, script::Struct* params, script::Script* script);
    // 0x0041D820 (empty_system): the system's +0x8C cleared. 1.
    int empty_system(uint32_t name, script::Struct* params, script::Script* script);
    // 0x0041D840 (0x7F97BAD3, SetEmitScript): params (a structure) given to the system with emitscript (0x00497790)
    // and UpdateScript (0x00497900), each when given. 1.
    int set_scripts(uint32_t name, script::Struct* params, script::Script* script);
    // 0x0041D8F0 (ShouldEmit): whether the system is emitting or has an emit rate above 0 (0 without a system).
    int should_emit(uint32_t name, script::Struct* params, script::Script* script);
    // 0x0041D920 / 0x0041D940 (EmitOn / EmitOff): the system emitting or not. 1.
    int emit_on(uint32_t name, script::Struct* params, script::Script* script);
    int emit_off(uint32_t name, script::Struct* params, script::Script* script);
    // 0x0041D960 (destroy): the system of its name destroyed by the particle manager - with ifEmpty (an integer) not 0,
    // once it's empty (0x0049ADB0), else now (0x0049AD90). 1.
    int destroy_system(uint32_t name, script::Struct* params, script::Script* script) const;
    // 0x0041E190 (destroy_particles): the system destroyed now and let go; the object dies. 1.
    int destroy_particles(uint32_t name, script::Struct* params, script::Script* script);

private:
    SharedRef<render::EmitterInstance> system_; // +0x18
    uint32_t label_{0};                         // +0x24 name (or a running number: next_unnamed_label)
    uint32_t unknown28_{0};                     // +0x28 0x184197D5 (given to the system's +0x78)
    float clock_{0.0f};                         // +0x2C the time not yet given to the system
    DormancyModule* suspend_{nullptr};          // +0x30 the suspend component
    ObjectModule* brightness_model_{nullptr};   // +0x34 ObjID's model (update_Brightness)
    bool rotate_{false};                        // +0x38 EnableRotate
};

} // namespace objects

namespace objects {
// The script member table's records (their code is filled in by engine/script/member_function_code.cpp).
constexpr std::size_t kFlexibleParticleMemberFunctionCount = 26;
std::array<script::MemberFunctionEntry, kFlexibleParticleMemberFunctionCount>& flexible_particle_member_functions();
} // namespace objects
