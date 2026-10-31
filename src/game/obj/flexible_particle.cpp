// The flexible particle component (GameObjects::CFlexibleParticleComponent) and the particle system's flag setter: see
// flexible_particle.h. From the disassembly of 0x0041D6E0-0x0041E25E.

#include "game/obj/flexible_particle.h"
#include "game/gfx/d3dx.h"
#include "game/gfx/flexible_particle_manager.h"
#include "game/gfx/particle_system.h"

#include <cstdint>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/net/gamenet_manager.h"
#include "game/obj/suspend_component.h"
#include "game/script/cscript.h"
#include "game/script/names.h"
#include "game/script/param_struct.h"
#include "game/sys/stopwatch.h"
#include "game/obj/collide_and_die.h"
#include "game/core/code_address.h"
#include "game/gfx/engine.h"
#include "game/core/stubs.h"
#include "game/obj/camera_lookaround.h"

namespace Engine3D {

// 0x0041D9C0
void CEmitterInstance::SetFlag(int bit, bool on)
{
    if (on)
        m_flagBits |= 1u << bit;
    else
        m_flagBits &= ~(1u << bit);
}

} // namespace Nx

namespace GameObjects {

float g_particleUpdateTime{1.0f};
uint32_t g_flexibleParticleMemberFunctions[156]{
            0x63274395, 0xC5BCEB9E, 0, 0, 0, 0,
            0x8FFDEF38, 0xC5BCEB9E, 0, 0, 0, 0,
            0x7911855B, 0xC5BCEB9E, 0, 0, 0, 0,
            0xDB8621F7, 0xC5BCEB9E, 0, 0, 0, 0,
            0xE985A9A6, 0xC5BCEB9E, 0, 0, 0, 0,
            0xE3309AB5, 0xC5BCEB9E, 0, 0, 0, 0,
            0xFBB6720D, 0xC5BCEB9E, 0, 0, 0, 0,
            0xA075EE45, 0xC5BCEB9E, 0, 0, 0, 0,
            0x2A479569, 0xC5BCEB9E, 0, 0, 0, 0,
            0x2E66815E, 0xC5BCEB9E, 0, 0, 0, 0,
            0xD3F1D333, 0xC5BCEB9E, 0, 0, 0, 0,
            0xB373FB6E, 0xC5BCEB9E, 0, 0, 0, 0,
            0x1CFBF078, 0xC5BCEB9E, 0, 0, 0, 0,
            0x514B2584, 0xC5BCEB9E, 0, 0, 0, 0,
            0x711B1A6A, 0xC5BCEB9E, 0, 0, 0, 0,
            0x35B65BD4, 0xC5BCEB9E, 0, 0, 0, 0,
            0x099CD17B, 0xC5BCEB9E, 0, 0, 0, 0,
            0x4893F05D, 0xC5BCEB9E, 0, 0, 0, 0,
            0x482482C6, 0xC5BCEB9E, 0, 0, 0, 0,
            0x7F97BAD3, 0xC5BCEB9E, 0, 0, 0, 0,
            0xE99D53A8, 0xC5BCEB9E, 0, 0, 0, 0,
            0xED0FE310, 0xC5BCEB9E, 0, 0, 0, 0,
            0x2A1F307B, 0xC5BCEB9E, 0, 0, 0, 0,
            0xABF5F9E1, 0xC5BCEB9E, 0, 0, 0, 0,
            0x83B57984, 0xC5BCEB9E, 0, 0, 0, 0,
            0, 0xC5BCEB9E, 0, 0, 0, 0};

namespace {

extern void** const kVtable;

constexpr uint32_t kName = 0xA1DC81F9;                 // name
constexpr uint32_t kMax = 0x6289DD76;                  // max
constexpr uint32_t kMaxStreams = 0x9E52A20F;           // MaxStreams
constexpr uint32_t kTexture = 0x7D99F28D;              // texture
constexpr uint32_t kBlendMode = 0xDD6BB3D5;            // blendmode
constexpr uint32_t kTypeParam = 0x7321A8D6;            // type
constexpr uint32_t kUnknown184197D5 = 0x184197D5;
constexpr uint32_t kUnknownA605B89F = 0xA605B89F;
constexpr uint32_t kSegments = 0xD93124D6;             // segments
constexpr uint32_t kUnknown4E0A65EE = 0x4E0A65EE;
constexpr uint32_t kHistory = 0xD8458FB4;              // history
constexpr uint32_t kPerm = 0xD5928F25;                 // perm
constexpr uint32_t kEmitterIndependent = 0x03D4E483;   // emitterIndependent
constexpr uint32_t kEmitRate = 0x8E9A640E;             // emit_rate
constexpr uint32_t kModel = 0x286A8D26;                // model
constexpr uint32_t kUseGlobalColorModulation = 0xA0E585B0;
constexpr uint32_t kGroupId = 0x87FA53ED;              // groupID
constexpr uint32_t kParams = 0x7031F10C;               // params
constexpr uint32_t kEmitScript = 0xFC57577F;           // emitscript
constexpr uint32_t kUpdateScript = 0x092276DB;         // UpdateScript
constexpr uint32_t kEnableRotate = 0xA2487CC3;         // EnableRotate
constexpr uint32_t kUpdateBrightness = 0xBBFA4B06;     // update_Brightness
constexpr uint32_t kObjId = 0x0830ECAF;                // ObjID
constexpr uint32_t kUseRandomUVQuadrants = 0x47580C70; // UseRandomUVQuadrants
constexpr uint32_t kUnknown5446AF14 = 0x5446AF14;
constexpr uint32_t kFillPath = 0x9B9381E2;             // FillPath
constexpr uint32_t kUnknownDE45A90D = 0xDE45A90D;
constexpr uint32_t kPriority = 0x9D5923D8;             // priority
constexpr uint32_t kAlignTo = 0xE4247A6B;              // align_to
constexpr uint32_t kForceParticleUpdateTime = 0x0CF3CBA1; // Force_Particle_Update_Time (a script global)
constexpr uint32_t kIfEmpty = 0x0CF50F29;              // ifEmpty
constexpr uint32_t kIsEmitting = 0x2299A9F2;           // IsEmitting
constexpr uint32_t kApplyEnvBrightness = 0x96896315;   // ApplyEnvBrightness
constexpr uint32_t kSuspend = 0xCE0CA665;

// The particle manager's update time (0x0067D6D0: Force_Particle_Update_Time as a float).
float& ParticleUpdateTime() { return g_particleUpdateTime; }

// The particle manager (0x006FA7C0; no source yet).
void* ParticleHubHandle() { return Engine3D::g_flexibleParticleManager; }
Engine3D::CEmitterInstance* FindSystem(uint32_t name)
{
    return static_cast<Engine3D::CFlexibleParticleManager*>(ParticleHubHandle())->FindSystem(name);
}
// 0x0049AC60: a new system (named `name`) from the parameters.
Engine3D::CEmitterInstance* StartSubsystem(uint32_t name, uint32_t type, int max, int maxStreams, uint32_t texture,
                                  uint32_t blendMode, int unknownA605B89F, int segments, float unknown4E0A65EE,
                                  int history, int perm, bool useGlobalColorModulation, const char* model)
{
    return static_cast<Engine3D::CFlexibleParticleManager*>(ParticleHubHandle())
        ->StartSubsystem(name, type, max, maxStreams, texture, blendMode, unknownA605B89F, segments, unknown4E0A65EE,
                       history, perm, useGlobalColorModulation, model);
}
void DestroySystemNow(uint32_t name)
{
    static_cast<Engine3D::CFlexibleParticleManager*>(ParticleHubHandle())->DestroySystemNow(name);
}
void DestroySystemWhenEmpty(uint32_t name)
{
    static_cast<Engine3D::CFlexibleParticleManager*>(ParticleHubHandle())->DestroySystemWhenEmpty(name);
}

// The system's own (no source yet).
bool SystemCallMemberFunction(Engine3D::CEmitterInstance* system, uint32_t name, Scripts::CStruct* params,
                              Scripts::CScript* script)
{
    return system->InvokeMethodByName(name, params, script);
}
void SetEmitScript(Engine3D::CEmitterInstance* system, uint32_t script, Scripts::CStruct* params, CEntity* object)
{
    system->SetEmitScript(script, params, object);
}
void SetUpdateScript(Engine3D::CEmitterInstance* system, uint32_t script, Scripts::CStruct* params, CEntity* object)
{
    system->SetUpdateScript(script, params, object);
}
void UpdateSystem(Engine3D::CEmitterInstance* system, float time, int unknown)
{
    system->TickFrame(time, unknown != 0);
}
void IdleSystem(Engine3D::CEmitterInstance* system)
{
    system->RunEmitScript();
}
// 0x00497AB0: the matrix of the system's rotation, into `out` (returned).
Maths::Mat4* SystemRotation(Engine3D::CEmitterInstance* system, Maths::Mat4* out)
{
    return system->GetRotationMatrix(out);
}

// out = a * b (the statically linked D3DXMatrixMultiply, 0x005BFA8C: a library function, not decompiled).
void MatrixMultiply(Maths::Mat4* out, const Maths::Mat4* a, const Maths::Mat4* b)
{
    D3dx::MatrixMultiply(out, a, b);
}

// The suspend component (no source yet): whether its object should be updated (0x00433EA0), suspended (+0x30, the
// getter 0x00433E90), told to start (0x00433E70).
bool SuspendShouldUpdate(uint8_t* suspend)
{
    return reinterpret_cast<CDormancyModule*>(suspend)->WantsAnimating(nullptr);
}
void SuspendStart(uint8_t* suspend) { reinterpret_cast<CDormancyModule*>(suspend)->DisableLod(); }

// The object dies (its vtable slot 6, FlagAsDead).
void Kill(CGameEntity* object) { reinterpret_cast<void(__thiscall*)(CGameEntity*)>(object->m_vtable[6])(object); }

// The original's _ftol (0x006236FC): truncated towards zero, the low 32 bits of the 64-bit result.
int Truncated(double value) { return static_cast<int>(static_cast<int64_t>(value)); }

} // namespace

// 0x0041E1E0
CObjectModule* __cdecl CFlexibleParticleComponent::Fabricate()
{
    void* memory = MemSys::AllocZeroed(sizeof(CFlexibleParticleComponent));
    return memory ? static_cast<CFlexibleParticleComponent*>(memory)->MakeNew() : nullptr;
}

// 0x0041D6E0
void __cdecl CFlexibleParticleComponent::PublishMethodTable()
{
    Scripts::RegisterMemberFunctionTable(reinterpret_cast<void*>(CFlexibleParticleComponent::kType),
                                        g_flexibleParticleMemberFunctions, 0x18);
}

// 0x0041DA00
CFlexibleParticleComponent* CFlexibleParticleComponent::MakeNew()
{
    CObjectModule::MakeNew();
    m_vtable = kVtable;
    mp_system.m_preceding = nullptr;
    mp_system.m_following = nullptr;
    mp_system.m_ownerObject = nullptr;
    m_kindTag = CFlexibleParticleComponent::kType;
    mp_system.Release(); // (the original lets go of the null it was just given)
    mp_system.m_ownerObject = nullptr;
    m_label = 0;
    m_unknown28 = 0;
    mp_suspend = nullptr;
    mp_brightnessModel = nullptr;
    m_rotate = false;
    m_clock = 0.0f;
    return this;
}

// 0x0041DA80
void CFlexibleParticleComponent::DestroyBody()
{
    m_vtable = kVtable;
    if (Engine3D::CEmitterInstance* system = mp_system.Pull())
        reinterpret_cast<void*(__thiscall*)(Engine3D::CEmitterInstance*, unsigned int)>(system->m_vtable[0])(system, 1);
    mp_system.Release();
    CObjectModule::DestroyBody();
}

// 0x0041E240
CFlexibleParticleComponent* CFlexibleParticleComponent::Teardown(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        MemSys::Hub::Pull()->Dealloc(this);
    return this;
}

// 0x0041DB00
void CFlexibleParticleComponent::ConfigureFromData(Scripts::CStruct* params)
{
    params->ReadHashValue(kName, &m_label, 0);
    float maxFloat = 1.0f;
    params->FetchFloat(kMax, &maxFloat, 0);
    int max = Truncated(maxFloat);
    int maxStreams = 2;
    params->FetchIntValue(kMaxStreams, &maxStreams, 0);
    uint32_t texture = 0;
    params->ReadHashValue(kTexture, &texture, 0);
    uint32_t blendMode = 0x515E298E;
    params->ReadHashValue(kBlendMode, &blendMode, 0);
    uint32_t type = 0xAAB555BB;
    params->ReadHashValue(kTypeParam, &type, 0);
    params->ReadHashValue(kUnknown184197D5, &m_unknown28, 0);
    int unknownA605B89F = 0;
    params->FetchIntValue(kUnknownA605B89F, &unknownA605B89F, 0);
    int segments = 8;
    params->FetchIntValue(kSegments, &segments, 0);
    float unknown4E0A65EE = 0.5f;
    params->FetchFloat(kUnknown4E0A65EE, &unknown4E0A65EE, 0);
    int history = 1;
    params->FetchIntValue(kHistory, &history, 0);
    int perm = 0;
    params->FetchIntValue(kPerm, &perm, 0);
    int emitterIndependent = 0;
    params->FetchIntValue(kEmitterIndependent, &emitterIndependent, 0);
    float emitRate = 0.0f;
    params->FetchFloat(kEmitRate, &emitRate, 0);
    const char* model = nullptr;
    params->FetchText(kModel, &model, 0);
    bool useGlobalColorModulation = params->HasBareFlag(kUseGlobalColorModulation);
    // A permanent system of this name, if there's one; else a new one, named by the component's address.
    bool found = false;
    if (perm) {
        mp_system.AssignValue(FindSystem(m_label));
        found = mp_system.Pull() != nullptr;
    }
    if (!found) {
        m_label = reinterpret_cast<uint32_t>(this);
        mp_system.AssignValue(StartSubsystem(reinterpret_cast<uint32_t>(this), type, max, maxStreams, texture, blendMode,
                                   unknownA605B89F, segments, unknown4E0A65EE, history, perm, useGlobalColorModulation,
                                   model));
        if (!mp_system.Pull())
            return;
    }
    uint32_t groupId = 0;
    if (params->ReadHashValue(kGroupId, &groupId, 0))
        mp_system.Pull()->m_groupId = groupId;
    mp_system.Pull()->m_emitterIndependent = emitterIndependent > 0;
    mp_system.Pull()->m_unknown78 = m_unknown28;
    mp_system.Pull()->m_emitRate = emitRate;
    Scripts::CStruct* scriptParams = nullptr;
    params->ReadSubStruct(kParams, &scriptParams, 0);
    uint32_t emitScript = 0;
    if (params->ReadHashValue(kEmitScript, &emitScript, 0))
        SetEmitScript(mp_system.Pull(), emitScript, scriptParams, m_entity);
    uint32_t updateScript = 0;
    if (params->ReadHashValue(kUpdateScript, &updateScript, 0))
        SetUpdateScript(mp_system.Pull(), updateScript, scriptParams, m_entity);
    if (params->HasBareFlag(kEnableRotate))
        m_rotate = true;
    bool updateBrightness = params->HasBareFlag(kUpdateBrightness);
    mp_system.Pull()->m_updateBrightness = false;
    if (updateBrightness) {
        uint32_t objectId = m_unknown28;
        params->ReadHashValue(kObjId, &objectId, 0);
        if (objectId)
            mp_brightnessModel = static_cast<CGameEntity*>(FindObject(objectId))->FetchModule(kModel);
    }
    if (params->HasBareFlag(kUseRandomUVQuadrants))
        mp_system.Pull()->m_flagBits |= 0x4000;
    if (params->HasBareFlag(kUnknown5446AF14))
        mp_system.Pull()->m_flagBits |= 0x8000;
    if (params->HasBareFlag(kFillPath))
        mp_system.Pull()->m_fillPath = true;
    if (params->HasBareFlag(kUnknownDE45A90D))
        mp_system.Pull()->m_unknown133 = false;
    int priority = 0;
    params->FetchIntValue(kPriority, &priority, 0);
    mp_system.Pull()->m_rank = static_cast<uint8_t>(priority);
    Scripts::VectorValue alignTo;
    if (params->FetchVec(kAlignTo, &alignTo, 0)) {
        Engine3D::CEmitterInstance* aligned = mp_system.Pull();
        aligned->m_alignTo = {alignTo.x, alignTo.y, alignTo.z, alignTo.w};
        aligned->m_aligned = true;
    }
    ParticleUpdateTime() = static_cast<float>(Scripts::FetchIntValue(kForceParticleUpdateTime));
}

// 0x0041D740
void CFlexibleParticleComponent::Seal()
{
    mp_suspend = reinterpret_cast<uint8_t*>(m_entity->FetchModule(kSuspend));
    if (mp_suspend)
        SuspendStart(mp_suspend);
}

// 0x0041DF80
void CFlexibleParticleComponent::TickFrame()
{
    if (!mp_system.Pull()) {
        Kill(m_entity);
        return;
    }
    m_clock = static_cast<float>(FrameClock::GetSlomoFrameSeconds() + m_clock);
    bool update = true;
    if (mp_suspend && !SuspendShouldUpdate(mp_suspend))
        update = false;
    if (mp_suspend && mp_suspend[0x30] && !g_onlineHub->InNetGame()) {
        // Suspended: only catching up, once the time reaches the particle manager's update time.
        if (m_clock >= ParticleUpdateTime()) {
            Engine3D::CEmitterInstance* system = mp_system.Pull();
            system->m_flagBits |= 1;
            mp_system.Pull()->m_unknown12F = true;
            if (mp_system.Pull()->m_wantsTime) {
                UpdateSystem(mp_system.Pull(), m_clock, 0);
                m_clock = 0.0f;
            }
            if (Engine3D::CEmitterInstance* after = mp_system.Pull()) {
                after->m_flagBits &= ~1u;
                mp_system.Pull()->m_unknown12F = false;
            }
        }
    } else {
        mp_system.Pull()->m_flagBits |= 1;
        mp_system.Pull()->m_unknown12F = true;
        if (update) {
            if (m_rotate) {
                Maths::Mat4 orientation = m_entity->m_displayMatrix;
                orientation.row[3] = {0.0f, 0.0f, 0.0f, 1.0f};
                Maths::Mat4 rotation;
                MatrixMultiply(&m_entity->m_displayMatrix, SystemRotation(mp_system.Pull(), &rotation), &orientation);
                mp_system.Pull()->mp_matrix = &m_entity->m_displayMatrix;
            }
            mp_system.Pull()->m_atPoint = m_entity->m_atPoint;
            Engine3D::CEmitterInstance* system = mp_system.Pull();
            if (system->m_wantsTime) {
                if (mp_brightnessModel)
                    system->m_unknownEC = *reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(mp_brightnessModel) +
                                                                       0x22C);
                UpdateSystem(mp_system.Pull(), m_clock, 0);
                m_clock = 0.0f;
            }
        } else {
            IdleSystem(mp_system.Pull());
        }
    }
    if (m_entity->m_compositeFlags & COMPOSITE_FLAG_HIDDEN) {
        mp_system.Pull()->m_unknown12F = false;
        mp_system.Pull()->m_unknown130 = false;
    }
}

// 0x0041D760
void CFlexibleParticleComponent::Conceal(bool hide)
{
    if (Engine3D::CEmitterInstance* system = mp_system.Pull())
        system->m_unknown12F = hide;
}

// 0x0041D780
int CFlexibleParticleComponent::InvokeMethodByName(uint32_t name, Scripts::CStruct* params, Scripts::CScript* script)
{
    switch (name) {
    case kIsEmitting: {
        Engine3D::CEmitterInstance* system = mp_system.Pull(); // (no null check, as the original)
        return system->m_spewing || system->m_emitRate > 0.0f ? 1 : 0;
    }
    case kApplyEnvBrightness:
        if (Engine3D::CEmitterInstance* system = mp_system.Pull())
            return SystemCallMemberFunction(system, kApplyEnvBrightness, params, script) ? 1 : 0;
        return 0;
    default:
        return 2;
    }
}

// 0x0041D700
int CFlexibleParticleComponent::CallMemberFunctionEntry(Scripts::CMemberFunctionEntry* entry, Scripts::CStruct* params,
                                                        Scripts::CScript* script)
{
    if (entry->mCode) {
        using Member = int(__thiscall*)(void*, uint32_t, Scripts::CStruct*, Scripts::CScript*);
        return reinterpret_cast<Member>(entry->mCode)(reinterpret_cast<uint8_t*>(this) + entry->mThisAdjustment,
                                                      entry->m_callsign, params, script);
    }
    return reinterpret_cast<int(__thiscall*)(CObjectModule*, uint32_t, Scripts::CStruct*, Scripts::CScript*)>(
        m_vtable[12])(this, entry->m_callsign, params, script);
}

// 0x0041D7F0
int CFlexibleParticleComponent::PassToSystem(uint32_t name, Scripts::CStruct* params, Scripts::CScript* script)
{
    Engine3D::CEmitterInstance* system = mp_system.Pull();
    if (!system)
        return 0;
    return SystemCallMemberFunction(system, name, params, script) ? 1 : 0;
}

// 0x0041D820
int CFlexibleParticleComponent::EmptySystem(uint32_t, Scripts::CStruct*, Scripts::CScript*)
{
    if (Engine3D::CEmitterInstance* system = mp_system.Pull())
        system->m_unknown8C = 0;
    return 1;
}

// 0x0041D840
int CFlexibleParticleComponent::SetScripts(uint32_t, Scripts::CStruct* params, Scripts::CScript*)
{
    Scripts::CStruct* scriptParams = nullptr;
    params->ReadSubStruct(kParams, &scriptParams, 0);
    if (mp_system.Pull()) {
        uint32_t emitScript = 0;
        if (params->ReadHashValue(kEmitScript, &emitScript, 0))
            SetEmitScript(mp_system.Pull(), emitScript, scriptParams, m_entity);
        uint32_t updateScript = 0;
        if (params->ReadHashValue(kUpdateScript, &updateScript, 0))
            SetUpdateScript(mp_system.Pull(), updateScript, scriptParams, m_entity);
    }
    return 1;
}

// 0x0041D8F0
int CFlexibleParticleComponent::ShouldEmit(uint32_t, Scripts::CStruct*, Scripts::CScript*)
{
    Engine3D::CEmitterInstance* system = mp_system.Pull();
    if (!system)
        return 0;
    return system->m_spewing || system->m_emitRate > 0.0f ? 1 : 0;
}

// 0x0041D920
int CFlexibleParticleComponent::EmitOn(uint32_t, Scripts::CStruct*, Scripts::CScript*)
{
    if (Engine3D::CEmitterInstance* system = mp_system.Pull())
        system->m_spewing = true;
    return 1;
}

// 0x0041D940
int CFlexibleParticleComponent::EmitOff(uint32_t, Scripts::CStruct*, Scripts::CScript*)
{
    if (Engine3D::CEmitterInstance* system = mp_system.Pull())
        system->m_spewing = false;
    return 1;
}

// 0x0041D960
int CFlexibleParticleComponent::DestroySystem(uint32_t, Scripts::CStruct* params, Scripts::CScript*)
{
    int ifEmpty = 0;
    params->FetchIntValue(kIfEmpty, &ifEmpty, 0);
    if (ifEmpty)
        DestroySystemWhenEmpty(m_label);
    else
        DestroySystemNow(m_label);
    return 1;
}

// 0x0041E190
int CFlexibleParticleComponent::DestroyParticles(uint32_t, Scripts::CStruct*, Scripts::CScript*)
{
    if (mp_system.Pull()) {
        DestroySystemNow(m_label);
        mp_system.Release();
        mp_system.m_ownerObject = nullptr;
    }
    Kill(m_entity);
    return 1;
}

} // namespace Obj

namespace GameObjects {
namespace {

static VtableSlot kVtable_slots[] = {&GameObjects::CFlexibleParticleComponent::Teardown,
         &GameObjects::CFlexibleParticleComponent::TickFrame,
         &GameObjects::CFlexibleParticleComponent::ConfigureFromData,
         &GameObjects::CFlexibleParticleComponent::Seal,
         Stubs::DoNothing,
         GameObjects::ReapplyFromData,
         Stubs::IgnoreArguments2,
         Stubs::IgnoreArgument,
         &GameObjects::CFlexibleParticleComponent::Conceal,
         Stubs::DoNothing,
         Stubs::IgnoreArgument,
         &GameObjects::CFlexibleParticleComponent::CallMemberFunctionEntry,
         &GameObjects::CFlexibleParticleComponent::InvokeMethodByName};
void** const kVtable = reinterpret_cast<void**>(kVtable_slots);

} // namespace
} // namespace Obj
