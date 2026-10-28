// The engine's flexible particle systems (Nx::CParticleSystem and its types' engine classes): see particle_system.h.
// From the disassembly of 0x00496890-0x0049A0BF (0x004968A0 isn't a Ghidra function). Emit, the member functions and
// Update are in particle_system_update.cpp.

#include "game/gfx/particle_system.h"
#include "game/gfx/particle_system_internal.h"

#include <cmath>
#include <cstring>

#include "game/core/dbg.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/gfx/camera.h"
#include "game/script/cscript.h"
#include "game/script/struct.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Nx {

namespace {

void** const kSystemVtable = reinterpret_cast<void**>(0x00649428);
void** const kSystemNodeVtable = reinterpret_cast<void**>(0x00649424);
void** const kNodeVtable = reinterpret_cast<void**>(0x00649420); // (Lst::Node<CParticleSystem>'s own)
constexpr float kNearlyUp = 0.99f;                                // 0x00645450
constexpr float kFramesPerSecond = 60.0f;                         // 0x00645EA0

void* ParticleManager() { return GameVariable<void*>(0x006FA7C0); }
int ViewportCount() { return GameVariable<int>(0x00701378); }
Gfx::Camera* ViewportCamera(int viewport)
{
    return static_cast<Gfx::Camera*>(GameFunction<void*(__cdecl*)(int)>(0x004A09C0)(viewport));
}
// _ftol: truncated to a 64-bit integer, its low half.
int Truncate(double value) { return static_cast<int>(static_cast<int64_t>(value)); }

// The allocations of the types' constructors: the manager's, its assert flag the global byte 0x00658555.
void* Allocate(unsigned int size)
{
    return Mem::Manager::Get()->Allocate(size, GameVariable<uint8_t>(0x00658555), nullptr);
}
void* AllocateZeroed(unsigned int size)
{
    return Mem::Manager::Get()->AllocateZeroed(size, GameVariable<uint8_t>(0x00658555), nullptr);
}

void DeleteScript(Script::CScript* script)
{
    script->Destroy();
    Script::CScript::operator delete(script);
}
Script::CScript* NewScript()
{
    void* memory = Script::CScript::operator new(sizeof(Script::CScript));
    return memory ? static_cast<Script::CScript*>(memory)->Construct() : nullptr;
}
Script::CStruct* NewStructure()
{
    Script::CStruct* structure = Script::NewStruct();
    return structure ? structure->Init() : nullptr;
}

} // namespace

// (shared with the other types' constructors: particle_system_internal.h)
namespace ParticleParts {

// The particles: `count` after their count (the compiler's array new; their constructor and destructor are empty).
sFlexibleParticle* NewParticles(int count)
{
    auto* memory = static_cast<int*>(Allocate(static_cast<unsigned int>(count) * sizeof(sFlexibleParticle) + 4));
    if (!memory)
        return nullptr;
    *memory = count; // (then the empty constructor run over each: 0x0062388B with 0x005902E0)
    return reinterpret_cast<sFlexibleParticle*>(memory + 1);
}
void DeleteParticles(sFlexibleParticle* particles)
{
    // (the empty destructor run over each first: 0x0062382B with 0x005480A0)
    Mem::Free(reinterpret_cast<int*>(particles) - 1);
}

// The three colour arrays, `count` RGBA colours each (the constructor iterator 0x00401020 runs an empty one over
// them), each allocated only if the one before was; all grey once the three are there. Whether they are.
bool NewColours(CParticleSystem* system, int count)
{
    uint8_t** colours[3] = {&system->mp_startColour, &system->mp_midColour, &system->mp_endColour};
    for (uint8_t** colour : colours) {
        *colour = static_cast<uint8_t*>(Allocate(static_cast<unsigned int>(count) * 4));
        if (!*colour)
            return false;
    }
    for (int i = 0; i < count; ++i)
        for (uint8_t** colour : colours)
            for (int k = 0; k < 4; ++k)
                (*colour)[i * 4 + k] = 0x80;
    return true;
}

// A type's constructor after the base's: the name and the maximum, no particles yet; the particles, then the
// positions (`positionsZeroed`: zeroed memory), then the colours. Whether everything was allocated.
bool ConstructParts(CParticleSystem* system, uint32_t name, int maxParticles, unsigned int positionsSize,
                    bool positionsZeroed, int colours)
{
    system->m_name = name;
    system->m_maxParticles = maxParticles;
    system->m_unknown8C = 0;
    system->mp_particles = NewParticles(maxParticles);
    if (!system->mp_particles)
        return false;
    system->mp_positions = static_cast<float*>(positionsZeroed ? AllocateZeroed(positionsSize) : Allocate(positionsSize));
    if (!system->mp_positions)
        return false;
    return NewColours(system, colours);
}

void SetVtables(CParticleSystem* system, uint32_t primary, uint32_t node)
{
    system->m_vtable = reinterpret_cast<void**>(primary);
    system->m_node.vtable = reinterpret_cast<void**>(node);
}

} // namespace ParticleParts

using namespace ParticleParts;

// --- life ---

// 0x00496F10
CParticleSystem* CParticleSystem::Construct()
{
    Obj::CRefCounted::Construct();
    m_node.priority = 0;
    m_node.vtable = kNodeVtable;
    m_node.data = this;
    m_node.next = &m_node;
    m_node.prev = &m_node;
    m_vtable = kSystemVtable;
    m_node.vtable = kSystemNodeVtable;
    m_flags = 0;
    Reset();
    return this;
}

// 0x00496D70
void CParticleSystem::Reset()
{
    mp_emitScript = nullptr;
    mp_updateScript = nullptr;
    mp_scriptParams = nullptr;
    mp_matrix = nullptr;
    m_emitRate = 0.0f;
    m_emitOwed = 0.0f;
    m_lodMinParticles = 0;
    m_lodNear = 0.0f;
    m_lodFar = 0.0f;
    m_pos = Mth::Vector{2048.0f, 144.0f, -928.0f, 0.0f};
    m_lastPos = Mth::Vector{0.0f, 0.0f, 0.0f, 0.0f};
    m_sizeScale = 1.0f;
    m_emitRange[0] = 1.0f;
    m_emitRange[1] = 1.0f;
    m_emitTarget[1] = 1.0f;
    m_speed[0] = 1.0f;
    m_unknownEC = 0x3F800000; // (1.0f)
    m_emitRange[2] = 0.0f;
    m_angleSpread = 0.5f;
    m_force[0] = 0.0f;
    m_force[1] = -0.02f;
    m_force[2] = 0.0f;
    m_emitTarget[0] = 0.0f;
    m_emitTarget[2] = 0.0f;
    m_rotation[0] = 0.0f;
    m_rotation[1] = 0.0f;
    m_rotation[2] = 0.0f;
    m_priority = 0;
    for (float& size : m_size)
        size = 2.0f;
    m_life[0] = 2.0f;
    m_life[1] = 2.0f;
    m_speed[1] = 2.0f;
    m_flags &= ~1u;
    for (float& scale : m_colourScale)
        scale = 1.0f;
    m_moving = false;
    m_endColourSet = false;
    m_flags |= 1u;
    m_unknown12F = true;
    m_flags &= ~0x10u;
    m_flags &= ~1u;
    m_randomDirection = false;
    m_flags |= 0x100u;
    m_circularEmit = true;
    m_aligned = false;
    m_dying = false;
    m_fillPath = false;
    m_emitterIndependent = false;
    m_updateBrightness = false;
    m_unknown130 = true;
    m_unknown133 = true;
    m_unknown78 = 0;
}

// 0x00498690
void CParticleSystem::DestroyBody()
{
    m_vtable = kSystemVtable;
    m_node.vtable = kSystemNodeVtable;
    if (mp_emitScript)
        DeleteScript(mp_emitScript);
    if (mp_updateScript)
        DeleteScript(mp_updateScript);
    if (Script::CStruct* params = mp_scriptParams) {
        params->Clear();
        Script::DeleteStruct(params);
    }
    if (mp_startColour)
        Mem::Free(mp_startColour);
    if (mp_midColour)
        Mem::Free(mp_midColour);
    if (mp_endColour)
        Mem::Free(mp_endColour);
    if (mp_positions)
        Mem::Free(mp_positions);
    if (mp_particles)
        DeleteParticles(mp_particles);
    // the particle manager lets go of it
    GameFunction<void(__thiscall*)(void*, CParticleSystem*)>(0x0049AD60)(ParticleManager(), this);
    m_node.UnlinkAs(kNodeVtable);
    Obj::CRefCounted::Destruct();
}

// 0x00498F80
CParticleSystem* CParticleSystem::Destroy(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        Mem::Free(this);
    return this;
}

// --- settings ---

// 0x004968B0
void CParticleSystem::SetEmitRange(float width, float height, float depth)
{
    m_emitRange[0] = width;
    m_emitRange[1] = height;
    m_emitRange[2] = depth;
}

// 0x004968E0
void CParticleSystem::SetForce(float x, float y, float z)
{
    m_force[0] = x;
    m_force[1] = y;
    m_force[2] = z;
}

// 0x00496910
void CParticleSystem::SetEmitTarget(float x, float y, float z)
{
    m_emitTarget[0] = x;
    m_emitTarget[1] = y;
    m_emitTarget[2] = z;
}

// 0x00496940
void CParticleSystem::SetRotation(float x, float y, float z)
{
    m_rotation[0] = x;
    m_rotation[1] = y;
    m_rotation[2] = z;
}

// 0x00496970
void CParticleSystem::GetPosition(int particle, int step, float* x, float* y, float* z)
{
    const float* position = mp_positions + 3 * (m_maxParticles * step + particle);
    *x = position[0];
    *y = position[1];
    *z = position[2];
}

// 0x004969B0
void CParticleSystem::SetPosition(int particle, int step, float x, float y, float z)
{
    float* position = mp_positions + 3 * (m_maxParticles * step + particle);
    position[0] = x;
    position[1] = y;
    position[2] = z;
}

// 0x004969F0
void CParticleSystem::GetEmitMatrix(Mth::Matrix* out)
{
    if (mp_matrix) {
        *out = *mp_matrix;
        return;
    }
    out->row[0] = Mth::Vector{1.0f, 0.0f, 0.0f, 0.0f};
    out->row[1] = Mth::Vector{0.0f, 1.0f, 0.0f, 0.0f};
    out->row[2] = Mth::Vector{0.0f, 0.0f, 1.0f, 0.0f};
    out->row[3] = Mth::Vector{0.0f, 0.0f, 0.0f, 1.0f};
    Mth::Vector& at = out->row[2];
    at = Mth::Vector{m_emitTarget[0], m_emitTarget[1], m_emitTarget[2], 1.0f};
    at.Normalize();
    // across: up x the target (along x when the target is nearly up); then the second row across both (x87, 53 bits)
    double up = 1.0, along = 0.0;
    if (std::fabs(at.y) > kNearlyUp) {
        up = 0.0;
        along = 1.0;
    }
    Mth::Vector& across = out->row[0];
    across.w = 0.0f;
    float z = static_cast<float>(along * at.y - up * at.x);
    across.x = static_cast<float>(up * at.z - static_cast<double>(at.y) * 0.0);
    across.y = static_cast<float>(static_cast<double>(at.x) * 0.0 - along * at.z);
    across.z = z;
    Mth::Vector& second = out->row[1];
    second.w = 0.0f;
    float secondZ = static_cast<float>(static_cast<double>(at.y) * across.x - static_cast<double>(at.x) * across.y);
    second.x = static_cast<float>(static_cast<double>(across.y) * at.z - static_cast<double>(across.z) * at.y);
    second.y = static_cast<float>(static_cast<double>(across.z) * at.x - static_cast<double>(across.x) * at.z);
    second.z = secondZ;
}

// 0x00496B40
int CParticleSystem::LodParticleCount(int count)
{
    if (m_lodNear >= m_lodFar || ViewportCount() > 1) // (NaN distances go on)
        return count;
    Gfx::Camera* camera = ViewportCamera(0);
    if (!camera)
        return count;
    const Mth::Vector* eye = camera->GetPos();
    double dx = static_cast<double>(m_pos.x) - eye->x;
    double dy = static_cast<double>(m_pos.y) - eye->y;
    double dz = static_cast<double>(m_pos.z) - eye->z;
    float distance = static_cast<float>(dz * dz + dy * dy + dx * dx);
    if (distance < m_lodNear)
        return count;
    if (distance > m_lodFar)
        return m_lodMinParticles;
    double t = (static_cast<double>(m_lodFar) - distance) / (static_cast<double>(m_lodFar) - m_lodNear);
    int lod = Truncate(t * (m_maxParticles - m_lodMinParticles) + m_lodMinParticles);
    return count < lod ? count : lod;
}

// 0x00496C30
void CParticleSystem::UpdateBounds()
{
    float force = std::fabs(m_force[2]);
    if (std::fabs(m_force[1]) > force)
        force = std::fabs(m_force[1]);
    if (std::fabs(m_force[0]) > force)
        force = std::fabs(m_force[0]);
    double travel = static_cast<double>(force) * m_life[1] + m_speed[1];
    m_bounds = m_pos;
    float range = m_emitRange[2];
    if (m_emitRange[1] > range)
        range = m_emitRange[1];
    if (m_emitRange[0] > range)
        range = m_emitRange[0];
    m_bounds.w = static_cast<float>(travel * m_life[1] * kFramesPerSecond + range);
}

// 0x00496CE0
void CParticleSystem::SetColourScale(const float* rgba)
{
    if (m_flags & 0x10000) {
        for (int i = 0; i < 4; ++i)
            m_colourScale[i] = rgba[i];
    }
}

// 0x00496F60
void CParticleSystem::RunEmitScript()
{
    if (mp_emitScript && mp_emitScript->Update() == Script::ESCRIPTRETURNVAL_FINISHED) {
        if (Script::CScript* script = mp_emitScript)
            DeleteScript(script);
        mp_emitScript = nullptr;
    }
}

// 0x00496FA0
void CParticleSystem::RenderIfVisible(bool force)
{
    if ((m_unknown12F && m_unknown130) || force) {
        reinterpret_cast<void(__thiscall*)(CParticleSystem*)>(m_vtable[1])(this);
        m_flags &= ~1u;
        m_unknown12F = false;
    }
}

namespace {

// 0x00497790 / 0x00497900: a script slot set (see SetEmitScript).
void SetScript(CParticleSystem* system, Script::CScript*& slot, uint32_t script, Script::CStruct* params,
               void* object)
{
    if (!script) {
        if (Script::CScript* old = slot)
            DeleteScript(old);
        slot = nullptr;
        return;
    }
    if (!slot)
        slot = NewScript();
    if (!system->mp_scriptParams)
        system->mp_scriptParams = NewStructure();
    system->mp_scriptParams->AppendStructureIfAny(params);
    slot->SetScript(script, system->mp_scriptParams, static_cast<Obj::CObject*>(object));
}

} // namespace

// 0x00497790
void CParticleSystem::SetEmitScript(uint32_t script, Script::CStruct* params, void* object)
{
    SetScript(this, mp_emitScript, script, params, object);
}

// 0x00497900
void CParticleSystem::SetUpdateScript(uint32_t script, Script::CStruct* params, void* object)
{
    SetScript(this, mp_updateScript, script, params, object);
}

// 0x00497A70
void CParticleSystem::SetWantsTime(uint8_t on)
{
    if (on)
        m_flags |= 0x10000u;
    else
        m_flags &= ~0x10000u;
    reinterpret_cast<uint8_t&>(m_wantsTime) = on;
}

// 0x00497AB0
Mth::Matrix* CParticleSystem::GetRotationMatrix(Mth::Matrix* out)
{
    Mth::Vector angles{m_rotation[0], m_rotation[1], m_rotation[2], 1.0f};
    Mth::Matrix rotation;
    rotation.InitFromAngles(angles);
    *out = rotation;
    return out;
}

// 0x00496890
int __cdecl CParticleSystem::GetNumColoursStub()
{
    Dbg::Printf("STUB: plat_get_num_particle_colors\n");
    return 0;
}

// 0x004968A0
int __cdecl CParticleSystem::GetNumVertexListsStub()
{
    Dbg::Printf("STUB: plat_get_num_vertex_lists\n");
    return 0;
}

// --- the types ---

// 0x00499010
CNewFlatParticles* CNewFlatParticles::Construct(uint32_t name, int maxParticles, uint32_t, uint32_t, int, int, float,
                                                int)
{
    CParticleSystem::Construct();
    SetVtables(this, 0x0064943C, 0x00649438);
    if (ConstructParts(this, name, maxParticles, static_cast<unsigned int>(maxParticles) * 12, false, 1)) {
        m_colourMid = -1.0f;
        m_flags |= 4u;
    }
    return this;
}

// 0x00498FB0
void CNewFlatParticles::DestroyBody()
{
    SetVtables(this, 0x0064943C, 0x00649438);
    CParticleSystem::DestroyBody();
}

// 0x00498FE0
CNewFlatParticles* CNewFlatParticles::Destroy(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        Mem::Free(this);
    return this;
}

// 0x004992C0
CFlatRibbonParticles* CFlatRibbonParticles::Construct(uint32_t name, int maxParticles, uint32_t, uint32_t, int, int,
                                                      float, int history)
{
    CParticleSystem::Construct();
    SetVtables(this, 0x00649450, 0x0064944C);
    m_segments = history + 1;
    // (the colours' count read again as m_segments + 3 for each array and each step of the loop)
    if (ConstructParts(this, name, maxParticles, static_cast<unsigned int>(maxParticles * m_segments * 12), true,
                       m_segments + 3)) {
        m_colourMid = -1.0f;
        m_flags |= 4u;
    }
    return this;
}

// 0x00499200
void CFlatRibbonParticles::DestroyBody()
{
    SetVtables(this, 0x00649450, 0x0064944C);
    if (mp_particles) {
        DeleteParticles(mp_particles);
        mp_particles = nullptr;
    }
    CParticleSystem::DestroyBody();
}

// 0x00499290
CFlatRibbonParticles* CFlatRibbonParticles::Destroy(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        Mem::Free(this);
    return this;
}

// 0x004991E0
int CFlatRibbonParticles::GetNumColours() { return m_segments + 3; }

// 0x004995B0
CGlowParticles* CGlowParticles::Construct(uint32_t name, int maxParticles, uint32_t, uint32_t, int, int segments,
                                          float glowWidth, int)
{
    CParticleSystem::Construct();
    SetVtables(this, 0x00649464, 0x00649460);
    if (ConstructParts(this, name, maxParticles, static_cast<unsigned int>(maxParticles) * 12, false, 3)) {
        m_segments = segments;
        m_colourMid = -1.0f;
        m_glowWidth = glowWidth;
        m_flags |= 4u;
    }
    return this;
}

// 0x00499550
void CGlowParticles::DestroyBody()
{
    SetVtables(this, 0x00649464, 0x00649460);
    CParticleSystem::DestroyBody();
}

// 0x00499580
CGlowParticles* CGlowParticles::Destroy(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        Mem::Free(this);
    return this;
}

// 0x00499880
CGlowRibbonTrailParticles* CGlowRibbonTrailParticles::Construct(uint32_t name, int maxParticles, uint32_t, uint32_t,
                                                                int, int segments, float width, int history)
{
    CParticleSystem::Construct();
    SetVtables(this, 0x00649478, 0x00649474);
    m_historyLength = history + 1;
    if (ConstructParts(this, name, maxParticles, static_cast<unsigned int>((history + 1) * maxParticles * 12), true,
                       m_historyLength + 3)) {
        m_segments = segments;
        m_colourMid = -1.0f;
        m_width = width;
        m_flags |= 4u;
    }
    return this;
}

// 0x00499800
void CGlowRibbonTrailParticles::DestroyBody()
{
    SetVtables(this, 0x00649478, 0x00649474);
    CParticleSystem::DestroyBody();
}

// 0x00499850
CGlowRibbonTrailParticles* CGlowRibbonTrailParticles::Destroy(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        Mem::Free(this);
    return this;
}

// 0x00499820 / 0x00499830
int CGlowRibbonTrailParticles::GetNumColours() { return m_historyLength + 3; }
int CGlowRibbonTrailParticles::GetNumVertexLists() { return m_historyLength; }

// 0x00499B80
CLineParticles* CLineParticles::Construct(uint32_t name, int maxParticles, uint32_t, uint32_t, int, int, float,
                                          int history)
{
    CParticleSystem::Construct();
    SetVtables(this, 0x0064948C, 0x00649488);
    m_steps = history + 1;
    if (ConstructParts(this, name, maxParticles, static_cast<unsigned int>(maxParticles * (history + 1) * 12), true,
                       m_steps)) {
        m_colourMid = -1.0f;
        m_flags |= 4u;
    }
    return this;
}

// 0x00499B20
void CLineParticles::DestroyBody()
{
    SetVtables(this, 0x0064948C, 0x00649488);
    CParticleSystem::DestroyBody();
}

// 0x00499B50
CLineParticles* CLineParticles::Destroy(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        Mem::Free(this);
    return this;
}

// 0x00499E10
void CModelParticles::DestroyBody()
{
    SetVtables(this, 0x006494A0, 0x0064949C);
    if (mp_unknown13C) {
        Mem::Free(mp_unknown13C);
        mp_unknown13C = nullptr;
    }
    if (mp_models) {
        for (unsigned int i = 0; i < m_modelCount; ++i)
            GameFunction<void(__cdecl*)(void*)>(0x0048C3C0)(mp_models[i]); // (the model deleted)
        Mem::Free(mp_models);
        mp_models = nullptr;
    }
    CParticleSystem::DestroyBody();
}

// 0x0049A090
CModelParticles* CModelParticles::Destroy(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        Mem::Free(this);
    return this;
}

} // namespace Nx

namespace {

// The destructors reached through the node's vtable (`this` the node at +0x0C): each a jump to the type's.
struct NodeAdapters {
    // 0x00496D10 / 0x00496D30: Lst::Node<CParticleSystem>'s own destructor body (its vtable, out of its list) and
    // scalar deleting destructor (freed with flags & 1; returns this).
    void NodeDestroyBody() { reinterpret_cast<Mem::ListNode*>(this)->UnlinkAs(reinterpret_cast<void**>(0x00649420)); }
    Mem::ListNode* NodeDestroy(unsigned int flags)
    {
        NodeDestroyBody();
        if (flags & 1)
            Mem::Free(this);
        return reinterpret_cast<Mem::ListNode*>(this);
    }
    template <typename T>
    T* System()
    {
        return reinterpret_cast<T*>(reinterpret_cast<uint8_t*>(this) - offsetof(Nx::CParticleSystem, m_node));
    }
    Nx::CParticleSystem* DestroySystem(unsigned int flags) { return System<Nx::CParticleSystem>()->Destroy(flags); }
    Nx::CNewFlatParticles* DestroyNewFlat(unsigned int flags) { return System<Nx::CNewFlatParticles>()->Destroy(flags); }
    Nx::CFlatRibbonParticles* DestroyFlatRibbon(unsigned int flags)
    {
        return System<Nx::CFlatRibbonParticles>()->Destroy(flags);
    }
    Nx::CGlowParticles* DestroyGlow(unsigned int flags) { return System<Nx::CGlowParticles>()->Destroy(flags); }
    Nx::CGlowRibbonTrailParticles* DestroyGlowRibbonTrail(unsigned int flags)
    {
        return System<Nx::CGlowRibbonTrailParticles>()->Destroy(flags);
    }
    Nx::CLineParticles* DestroyLine(unsigned int flags) { return System<Nx::CLineParticles>()->Destroy(flags); }
    Nx::CModelParticles* DestroyModel(unsigned int flags) { return System<Nx::CModelParticles>()->Destroy(flags); }
};

} // namespace

REPLACE(0x00496F10, &Nx::CParticleSystem::Construct);
REPLACE(0x00496D70, &Nx::CParticleSystem::Reset);
REPLACE(0x00498690, &Nx::CParticleSystem::DestroyBody);
REPLACE(0x00498F80, &Nx::CParticleSystem::Destroy);
REPLACE(0x00496F50, &NodeAdapters::DestroySystem);
REPLACE(0x00496D10, &NodeAdapters::NodeDestroyBody);
REPLACE(0x00496D30, &NodeAdapters::NodeDestroy);
REPLACE(0x004968B0, &Nx::CParticleSystem::SetEmitRange);
REPLACE(0x004968E0, &Nx::CParticleSystem::SetForce);
REPLACE(0x00496910, &Nx::CParticleSystem::SetEmitTarget);
REPLACE(0x00496940, &Nx::CParticleSystem::SetRotation);
REPLACE(0x00496970, &Nx::CParticleSystem::GetPosition);
REPLACE(0x004969B0, &Nx::CParticleSystem::SetPosition);
REPLACE(0x004969F0, &Nx::CParticleSystem::GetEmitMatrix);
REPLACE(0x00496B40, &Nx::CParticleSystem::LodParticleCount);
REPLACE(0x00496C30, &Nx::CParticleSystem::UpdateBounds);
REPLACE(0x00496CE0, &Nx::CParticleSystem::SetColourScale);
REPLACE(0x00496F60, &Nx::CParticleSystem::RunEmitScript);
REPLACE(0x00496FA0, &Nx::CParticleSystem::RenderIfVisible);
REPLACE(0x00497790, &Nx::CParticleSystem::SetEmitScript);
REPLACE(0x00497900, &Nx::CParticleSystem::SetUpdateScript);
REPLACE(0x00497A70, &Nx::CParticleSystem::SetWantsTime);
REPLACE(0x00497AB0, &Nx::CParticleSystem::GetRotationMatrix);
REPLACE(0x00496890, Nx::CParticleSystem::GetNumColoursStub);
REPLACE(0x004968A0, Nx::CParticleSystem::GetNumVertexListsStub);
REPLACE(0x00499010, &Nx::CNewFlatParticles::Construct);
REPLACE(0x00498FB0, &Nx::CNewFlatParticles::DestroyBody);
REPLACE(0x00498FE0, &Nx::CNewFlatParticles::Destroy);
REPLACE(0x00498FD0, &NodeAdapters::DestroyNewFlat);
REPLACE(0x004992C0, &Nx::CFlatRibbonParticles::Construct);
REPLACE(0x00499200, &Nx::CFlatRibbonParticles::DestroyBody);
REPLACE(0x00499290, &Nx::CFlatRibbonParticles::Destroy);
REPLACE(0x004991F0, &NodeAdapters::DestroyFlatRibbon);
REPLACE(0x004991E0, &Nx::CFlatRibbonParticles::GetNumColours);
REPLACE(0x004995B0, &Nx::CGlowParticles::Construct);
REPLACE(0x00499550, &Nx::CGlowParticles::DestroyBody);
REPLACE(0x00499580, &Nx::CGlowParticles::Destroy);
REPLACE(0x00499570, &NodeAdapters::DestroyGlow);
REPLACE(0x00499880, &Nx::CGlowRibbonTrailParticles::Construct);
REPLACE(0x00499800, &Nx::CGlowRibbonTrailParticles::DestroyBody);
REPLACE(0x00499850, &Nx::CGlowRibbonTrailParticles::Destroy);
REPLACE(0x00499840, &NodeAdapters::DestroyGlowRibbonTrail);
REPLACE(0x00499820, &Nx::CGlowRibbonTrailParticles::GetNumColours);
REPLACE(0x00499830, &Nx::CGlowRibbonTrailParticles::GetNumVertexLists);
REPLACE(0x00499B80, &Nx::CLineParticles::Construct);
REPLACE(0x00499B20, &Nx::CLineParticles::DestroyBody);
REPLACE(0x00499B50, &Nx::CLineParticles::Destroy);
REPLACE(0x00499B40, &NodeAdapters::DestroyLine);
REPLACE(0x00499E10, &Nx::CModelParticles::DestroyBody);
REPLACE(0x0049A090, &Nx::CModelParticles::Destroy);
REPLACE(0x00499E00, &NodeAdapters::DestroyModel);
