// The flexible particle component (Obj::CFlexibleParticleComponent) and the particle system's flag setter: see
// flexible_particle.h. From the disassembly of 0x0041D6E0-0x0041E25E.

#include "game/obj/flexible_particle.h"
#include "game/gfx/flexible_particle_manager.h"
#include "game/gfx/particle_system.h"

#include <cstdint>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/net/gamenet_manager.h"
#include "game/obj/suspend_component.h"
#include "game/script/cscript.h"
#include "game/script/names.h"
#include "game/script/struct.h"
#include "game/sys/timer.h"
#include "game/obj/collide_and_die.h"
#include "hook/game.h"
#include "hook/game_global.h"
#include "hook/hook.h"

namespace Nx {

// 0x0041D9C0
void CParticleSystem::SetFlag(int bit, bool on)
{
    if (on)
        m_flags |= 1u << bit;
    else
        m_flags &= ~(1u << bit);
}

} // namespace Nx

namespace Obj {

GAME_GLOBAL(float, g_particleUpdateTime, 0x0067D6D0, 1.0f);
GAME_GLOBAL(uint32_t[156], g_flexibleParticleMemberFunctions, 0x00683AC0,
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
            0, 0xC5BCEB9E, 0, 0, 0, 0);

namespace {

void** const kVtable = reinterpret_cast<void**>(0x00645FE4);

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
void* ParticleManager() { return GameVariable<void*>(0x006FA7C0); }
Nx::CParticleSystem* FindSystem(uint32_t name)
{
    return static_cast<Nx::CFlexibleParticleManager*>(ParticleManager())->FindSystem(name);
}
// 0x0049AC60: a new system (named `name`) from the parameters.
Nx::CParticleSystem* CreateSystem(uint32_t name, uint32_t type, int max, int maxStreams, uint32_t texture,
                                  uint32_t blendMode, int unknownA605B89F, int segments, float unknown4E0A65EE,
                                  int history, int perm, bool useGlobalColorModulation, const char* model)
{
    return static_cast<Nx::CFlexibleParticleManager*>(ParticleManager())
        ->CreateSystem(name, type, max, maxStreams, texture, blendMode, unknownA605B89F, segments, unknown4E0A65EE,
                       history, perm, useGlobalColorModulation, model);
}
void DestroySystemNow(uint32_t name)
{
    static_cast<Nx::CFlexibleParticleManager*>(ParticleManager())->DestroySystemNow(name);
}
void DestroySystemWhenEmpty(uint32_t name)
{
    static_cast<Nx::CFlexibleParticleManager*>(ParticleManager())->DestroySystemWhenEmpty(name);
}

// The system's own (no source yet).
bool SystemCallMemberFunction(Nx::CParticleSystem* system, uint32_t name, Script::CStruct* params,
                              Script::CScript* script)
{
    return system->CallMemberFunction(name, params, script);
}
void SetEmitScript(Nx::CParticleSystem* system, uint32_t script, Script::CStruct* params, CObject* object)
{
    system->SetEmitScript(script, params, object);
}
void SetUpdateScript(Nx::CParticleSystem* system, uint32_t script, Script::CStruct* params, CObject* object)
{
    system->SetUpdateScript(script, params, object);
}
void UpdateSystem(Nx::CParticleSystem* system, float time, int unknown)
{
    system->Update(time, unknown != 0);
}
void IdleSystem(Nx::CParticleSystem* system)
{
    system->RunEmitScript();
}
// 0x00497AB0: the matrix of the system's rotation, into `out` (returned).
Mth::Matrix* SystemRotation(Nx::CParticleSystem* system, Mth::Matrix* out)
{
    return system->GetRotationMatrix(out);
}

// out = a * b (the statically linked D3DXMatrixMultiply, 0x005BFA8C: a library function, not decompiled).
void MatrixMultiply(Mth::Matrix* out, const Mth::Matrix* a, const Mth::Matrix* b)
{
    GameFunction<void*(__stdcall*)(Mth::Matrix*, const Mth::Matrix*, const Mth::Matrix*)>(0x005BFA8C)(out, a, b);
}

// The suspend component (no source yet): whether its object should be updated (0x00433EA0), suspended (+0x30, the
// getter 0x00433E90), told to start (0x00433E70).
bool SuspendShouldUpdate(uint8_t* suspend)
{
    return reinterpret_cast<CSuspendComponent*>(suspend)->ShouldAnimate(nullptr);
}
void SuspendStart(uint8_t* suspend) { reinterpret_cast<CSuspendComponent*>(suspend)->DisableLod(); }

// The object dies (its vtable slot 6, MarkAsDead).
void Kill(CCompositeObject* object) { reinterpret_cast<void(__thiscall*)(CCompositeObject*)>(object->m_vtable[6])(object); }

// The original's _ftol (0x006236FC): truncated towards zero, the low 32 bits of the 64-bit result.
int Truncated(double value) { return static_cast<int>(static_cast<int64_t>(value)); }

} // namespace

// 0x0041E1E0
CBaseComponent* __cdecl CFlexibleParticleComponent::Create()
{
    void* memory = Mem::Calloc(sizeof(CFlexibleParticleComponent));
    return memory ? static_cast<CFlexibleParticleComponent*>(memory)->Construct() : nullptr;
}

// 0x0041D6E0
void __cdecl CFlexibleParticleComponent::RegisterMemberFunctions()
{
    Script::RegisterMemberFunctionTable(reinterpret_cast<void*>(CFlexibleParticleComponent::kType),
                                        g_flexibleParticleMemberFunctions, 0x18);
}

// 0x0041DA00
CFlexibleParticleComponent* CFlexibleParticleComponent::Construct()
{
    CBaseComponent::Construct();
    m_vtable = kVtable;
    mp_system.mp_prev = nullptr;
    mp_system.mp_next = nullptr;
    mp_system.mp_object = nullptr;
    m_type = CFlexibleParticleComponent::kType;
    mp_system.Release(); // (the original lets go of the null it was just given)
    mp_system.mp_object = nullptr;
    m_name = 0;
    m_unknown28 = 0;
    mp_suspend = nullptr;
    mp_brightnessModel = nullptr;
    m_rotate = false;
    m_time = 0.0f;
    return this;
}

// 0x0041DA80
void CFlexibleParticleComponent::DestroyBody()
{
    m_vtable = kVtable;
    if (Nx::CParticleSystem* system = mp_system.Get())
        reinterpret_cast<void*(__thiscall*)(Nx::CParticleSystem*, unsigned int)>(system->m_vtable[0])(system, 1);
    mp_system.Release();
    CBaseComponent::DestroyBody();
}

// 0x0041E240
CFlexibleParticleComponent* CFlexibleParticleComponent::Destroy(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x0041DB00
void CFlexibleParticleComponent::InitFromStructure(Script::CStruct* params)
{
    params->GetChecksum(kName, &m_name, 0);
    float maxFloat = 1.0f;
    params->GetFloat(kMax, &maxFloat, 0);
    int max = Truncated(maxFloat);
    int maxStreams = 2;
    params->GetInteger(kMaxStreams, &maxStreams, 0);
    uint32_t texture = 0;
    params->GetChecksum(kTexture, &texture, 0);
    uint32_t blendMode = 0x515E298E;
    params->GetChecksum(kBlendMode, &blendMode, 0);
    uint32_t type = 0xAAB555BB;
    params->GetChecksum(kTypeParam, &type, 0);
    params->GetChecksum(kUnknown184197D5, &m_unknown28, 0);
    int unknownA605B89F = 0;
    params->GetInteger(kUnknownA605B89F, &unknownA605B89F, 0);
    int segments = 8;
    params->GetInteger(kSegments, &segments, 0);
    float unknown4E0A65EE = 0.5f;
    params->GetFloat(kUnknown4E0A65EE, &unknown4E0A65EE, 0);
    int history = 1;
    params->GetInteger(kHistory, &history, 0);
    int perm = 0;
    params->GetInteger(kPerm, &perm, 0);
    int emitterIndependent = 0;
    params->GetInteger(kEmitterIndependent, &emitterIndependent, 0);
    float emitRate = 0.0f;
    params->GetFloat(kEmitRate, &emitRate, 0);
    const char* model = nullptr;
    params->GetString(kModel, &model, 0);
    bool useGlobalColorModulation = params->ContainsFlag(kUseGlobalColorModulation);
    // A permanent system of this name, if there's one; else a new one, named by the component's address.
    bool found = false;
    if (perm) {
        mp_system.Set(FindSystem(m_name));
        found = mp_system.Get() != nullptr;
    }
    if (!found) {
        m_name = reinterpret_cast<uint32_t>(this);
        mp_system.Set(CreateSystem(reinterpret_cast<uint32_t>(this), type, max, maxStreams, texture, blendMode,
                                   unknownA605B89F, segments, unknown4E0A65EE, history, perm, useGlobalColorModulation,
                                   model));
        if (!mp_system.Get())
            return;
    }
    uint32_t groupId = 0;
    if (params->GetChecksum(kGroupId, &groupId, 0))
        mp_system.Get()->m_groupId = groupId;
    mp_system.Get()->m_emitterIndependent = emitterIndependent > 0;
    mp_system.Get()->m_unknown78 = m_unknown28;
    mp_system.Get()->m_emitRate = emitRate;
    Script::CStruct* scriptParams = nullptr;
    params->GetStructure(kParams, &scriptParams, 0);
    uint32_t emitScript = 0;
    if (params->GetChecksum(kEmitScript, &emitScript, 0))
        SetEmitScript(mp_system.Get(), emitScript, scriptParams, m_object);
    uint32_t updateScript = 0;
    if (params->GetChecksum(kUpdateScript, &updateScript, 0))
        SetUpdateScript(mp_system.Get(), updateScript, scriptParams, m_object);
    if (params->ContainsFlag(kEnableRotate))
        m_rotate = true;
    bool updateBrightness = params->ContainsFlag(kUpdateBrightness);
    mp_system.Get()->m_updateBrightness = false;
    if (updateBrightness) {
        uint32_t objectId = m_unknown28;
        params->GetChecksum(kObjId, &objectId, 0);
        if (objectId)
            mp_brightnessModel = static_cast<CCompositeObject*>(FindObject(objectId))->GetComponent(kModel);
    }
    if (params->ContainsFlag(kUseRandomUVQuadrants))
        mp_system.Get()->m_flags |= 0x4000;
    if (params->ContainsFlag(kUnknown5446AF14))
        mp_system.Get()->m_flags |= 0x8000;
    if (params->ContainsFlag(kFillPath))
        mp_system.Get()->m_fillPath = true;
    if (params->ContainsFlag(kUnknownDE45A90D))
        mp_system.Get()->m_unknown133 = false;
    int priority = 0;
    params->GetInteger(kPriority, &priority, 0);
    mp_system.Get()->m_priority = static_cast<uint8_t>(priority);
    Script::VectorValue alignTo;
    if (params->GetVector(kAlignTo, &alignTo, 0)) {
        Nx::CParticleSystem* aligned = mp_system.Get();
        aligned->m_alignTo = {alignTo.x, alignTo.y, alignTo.z, alignTo.w};
        aligned->m_aligned = true;
    }
    ParticleUpdateTime() = static_cast<float>(Script::GetInteger(kForceParticleUpdateTime));
}

// 0x0041D740
void CFlexibleParticleComponent::Finalize()
{
    mp_suspend = reinterpret_cast<uint8_t*>(m_object->GetComponent(kSuspend));
    if (mp_suspend)
        SuspendStart(mp_suspend);
}

// 0x0041DF80
void CFlexibleParticleComponent::Update()
{
    if (!mp_system.Get()) {
        Kill(m_object);
        return;
    }
    m_time = static_cast<float>(Tmr::GetSlomoFrameSeconds() + m_time);
    bool update = true;
    if (mp_suspend && !SuspendShouldUpdate(mp_suspend))
        update = false;
    if (mp_suspend && mp_suspend[0x30] && !g_gameNetManager->InNetGame()) {
        // Suspended: only catching up, once the time reaches the particle manager's update time.
        if (m_time >= ParticleUpdateTime()) {
            Nx::CParticleSystem* system = mp_system.Get();
            system->m_flags |= 1;
            mp_system.Get()->m_unknown12F = true;
            if (mp_system.Get()->m_wantsTime) {
                UpdateSystem(mp_system.Get(), m_time, 0);
                m_time = 0.0f;
            }
            if (Nx::CParticleSystem* after = mp_system.Get()) {
                after->m_flags &= ~1u;
                mp_system.Get()->m_unknown12F = false;
            }
        }
    } else {
        mp_system.Get()->m_flags |= 1;
        mp_system.Get()->m_unknown12F = true;
        if (update) {
            if (m_rotate) {
                Mth::Matrix orientation = m_object->m_displayMatrix;
                orientation.row[3] = {0.0f, 0.0f, 0.0f, 1.0f};
                Mth::Matrix rotation;
                MatrixMultiply(&m_object->m_displayMatrix, SystemRotation(mp_system.Get(), &rotation), &orientation);
                mp_system.Get()->mp_matrix = &m_object->m_displayMatrix;
            }
            mp_system.Get()->m_pos = m_object->m_pos;
            Nx::CParticleSystem* system = mp_system.Get();
            if (system->m_wantsTime) {
                if (mp_brightnessModel)
                    system->m_unknownEC = *reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(mp_brightnessModel) +
                                                                       0x22C);
                UpdateSystem(mp_system.Get(), m_time, 0);
                m_time = 0.0f;
            }
        } else {
            IdleSystem(mp_system.Get());
        }
    }
    if (m_object->m_compositeFlags & COMPOSITE_FLAG_HIDDEN) {
        mp_system.Get()->m_unknown12F = false;
        mp_system.Get()->m_unknown130 = false;
    }
}

// 0x0041D760
void CFlexibleParticleComponent::Hide(bool hide)
{
    if (Nx::CParticleSystem* system = mp_system.Get())
        system->m_unknown12F = hide;
}

// 0x0041D780
int CFlexibleParticleComponent::CallMemberFunction(uint32_t name, Script::CStruct* params, Script::CScript* script)
{
    switch (name) {
    case kIsEmitting: {
        Nx::CParticleSystem* system = mp_system.Get(); // (no null check, as the original)
        return system->m_emitting || system->m_emitRate > 0.0f ? 1 : 0;
    }
    case kApplyEnvBrightness:
        if (Nx::CParticleSystem* system = mp_system.Get())
            return SystemCallMemberFunction(system, kApplyEnvBrightness, params, script) ? 1 : 0;
        return 0;
    default:
        return 2;
    }
}

// 0x0041D700
int CFlexibleParticleComponent::CallMemberFunctionEntry(Script::CMemberFunctionEntry* entry, Script::CStruct* params,
                                                        Script::CScript* script)
{
    if (entry->mCode) {
        using Member = int(__thiscall*)(void*, uint32_t, Script::CStruct*, Script::CScript*);
        return reinterpret_cast<Member>(entry->mCode)(reinterpret_cast<uint8_t*>(this) + entry->mThisAdjustment,
                                                      entry->mName, params, script);
    }
    return reinterpret_cast<int(__thiscall*)(CBaseComponent*, uint32_t, Script::CStruct*, Script::CScript*)>(
        m_vtable[12])(this, entry->mName, params, script);
}

// 0x0041D7F0
int CFlexibleParticleComponent::PassToSystem(uint32_t name, Script::CStruct* params, Script::CScript* script)
{
    Nx::CParticleSystem* system = mp_system.Get();
    if (!system)
        return 0;
    return SystemCallMemberFunction(system, name, params, script) ? 1 : 0;
}

// 0x0041D820
int CFlexibleParticleComponent::EmptySystem(uint32_t, Script::CStruct*, Script::CScript*)
{
    if (Nx::CParticleSystem* system = mp_system.Get())
        system->m_unknown8C = 0;
    return 1;
}

// 0x0041D840
int CFlexibleParticleComponent::SetScripts(uint32_t, Script::CStruct* params, Script::CScript*)
{
    Script::CStruct* scriptParams = nullptr;
    params->GetStructure(kParams, &scriptParams, 0);
    if (mp_system.Get()) {
        uint32_t emitScript = 0;
        if (params->GetChecksum(kEmitScript, &emitScript, 0))
            SetEmitScript(mp_system.Get(), emitScript, scriptParams, m_object);
        uint32_t updateScript = 0;
        if (params->GetChecksum(kUpdateScript, &updateScript, 0))
            SetUpdateScript(mp_system.Get(), updateScript, scriptParams, m_object);
    }
    return 1;
}

// 0x0041D8F0
int CFlexibleParticleComponent::ShouldEmit(uint32_t, Script::CStruct*, Script::CScript*)
{
    Nx::CParticleSystem* system = mp_system.Get();
    if (!system)
        return 0;
    return system->m_emitting || system->m_emitRate > 0.0f ? 1 : 0;
}

// 0x0041D920
int CFlexibleParticleComponent::EmitOn(uint32_t, Script::CStruct*, Script::CScript*)
{
    if (Nx::CParticleSystem* system = mp_system.Get())
        system->m_emitting = true;
    return 1;
}

// 0x0041D940
int CFlexibleParticleComponent::EmitOff(uint32_t, Script::CStruct*, Script::CScript*)
{
    if (Nx::CParticleSystem* system = mp_system.Get())
        system->m_emitting = false;
    return 1;
}

// 0x0041D960
int CFlexibleParticleComponent::DestroySystem(uint32_t, Script::CStruct* params, Script::CScript*)
{
    int ifEmpty = 0;
    params->GetInteger(kIfEmpty, &ifEmpty, 0);
    if (ifEmpty)
        DestroySystemWhenEmpty(m_name);
    else
        DestroySystemNow(m_name);
    return 1;
}

// 0x0041E190
int CFlexibleParticleComponent::DestroyParticles(uint32_t, Script::CStruct*, Script::CScript*)
{
    if (mp_system.Get()) {
        DestroySystemNow(m_name);
        mp_system.Release();
        mp_system.mp_object = nullptr;
    }
    Kill(m_object);
    return 1;
}

} // namespace Obj

REPLACE(0x0041E1E0, Obj::CFlexibleParticleComponent::Create);
REPLACE(0x0041D6E0, Obj::CFlexibleParticleComponent::RegisterMemberFunctions);
REPLACE(0x0041DA00, &Obj::CFlexibleParticleComponent::Construct);
REPLACE(0x0041DA80, &Obj::CFlexibleParticleComponent::DestroyBody);
REPLACE(0x0041E240, &Obj::CFlexibleParticleComponent::Destroy);
REPLACE(0x0041DB00, &Obj::CFlexibleParticleComponent::InitFromStructure);
REPLACE(0x0041D740, &Obj::CFlexibleParticleComponent::Finalize);
REPLACE(0x0041DF80, &Obj::CFlexibleParticleComponent::Update);
REPLACE(0x0041D760, &Obj::CFlexibleParticleComponent::Hide);
REPLACE(0x0041D780, &Obj::CFlexibleParticleComponent::CallMemberFunction);
REPLACE(0x0041D700, &Obj::CFlexibleParticleComponent::CallMemberFunctionEntry);
REPLACE(0x0041D7F0, &Obj::CFlexibleParticleComponent::PassToSystem);
REPLACE(0x0041D820, &Obj::CFlexibleParticleComponent::EmptySystem);
REPLACE(0x0041D840, &Obj::CFlexibleParticleComponent::SetScripts);
REPLACE(0x0041D8F0, &Obj::CFlexibleParticleComponent::ShouldEmit);
REPLACE(0x0041D920, &Obj::CFlexibleParticleComponent::EmitOn);
REPLACE(0x0041D940, &Obj::CFlexibleParticleComponent::EmitOff);
REPLACE(0x0041D960, &Obj::CFlexibleParticleComponent::DestroySystem);
REPLACE(0x0041E190, &Obj::CFlexibleParticleComponent::DestroyParticles);
REPLACE(0x0041D9C0, &Nx::CParticleSystem::SetFlag);
