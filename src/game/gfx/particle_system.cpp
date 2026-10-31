// The engine's flexible particle systems (Engine3D::CEmitterInstance and its types' engine classes): see particle_system.h.
// From the disassembly of 0x00496890-0x0049A0BF (0x004968A0 isn't a Ghidra function). Emit, the member functions and
// Update are in particle_system_update.cpp.

#include "game/gfx/particle_system.h"
#include "game/gfx/particle_system_internal.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>

#include "game/core/dbg.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/gfx/view_cam.h"
#include "game/gfx/engine.h"
#include "game/gfx/flexible_particle_manager.h"
#include "game/gfx/viewport.h"
#include "game/script/cscript.h"
#include "game/script/param_struct.h"
#include "game/core/code_address.h"
#include "game/core/game_vtables.h"
#include "game/gfx/particle_types.h"
#include "game/net/gamenet_handlers.h"
#include "game/gfx/weather.h"
#include "game/skate/skate_module.h"

namespace Engine3D {

namespace {
extern void** const kCNewFlatParticlesNodeVtable;
extern void** const kCNewFlatParticlesVtable;
extern void** const kCFlatRibbonParticlesNodeVtable;
extern void** const kCFlatRibbonParticlesVtable;
extern void** const kCGlowParticlesNodeVtable;
extern void** const kCGlowParticlesVtable;
extern void** const kCGlowRibbonTrailParticlesNodeVtable;
extern void** const kCGlowRibbonTrailParticlesVtable;
extern void** const kCLineParticlesNodeVtable;
extern void** const kCLineParticlesVtable;
} // namespace

uint8_t g_allocAssertFlag;

namespace {

extern void** const kSystemVtable;
extern void** const kSystemNodeVtable;
void** const& kNodeVtable = Vt::kParticleNode; // (ListKit::Elem<CEmitterInstance>'s own)
constexpr float kNearlyUp = 0.99f;                                // 0x00645450
constexpr float kFramesPerSecond = 60.0f;                         // 0x00645EA0

void* ParticleHubHandle() { return g_flexibleParticleManager; }
int ViewportCount() { return GameModules::g_viewportCount; }
Gfx2D::ViewCam* ViewportCamera(int viewport)
{
    return FetchLiveCamera(viewport);
}
// _ftol: truncated to a 64-bit integer, its low half.
int Truncate(double value) { return static_cast<int>(static_cast<int64_t>(value)); }

// The allocations of the types' constructors: the manager's, its assert flag the global byte 0x00658555.
void* Carve(unsigned int size)
{
    return MemSys::Hub::Pull()->Carve(size, g_allocAssertFlag, nullptr);
}
void* AllocateZeroed(unsigned int size)
{
    return MemSys::Hub::Pull()->AllocateZeroed(size, g_allocAssertFlag, nullptr);
}

void DeleteScript(Scripts::CScript* script)
{
    script->Teardown();
    Scripts::CScript::operator delete(script);
}
Scripts::CScript* NewScript()
{
    void* memory = Scripts::CScript::operator new(sizeof(Scripts::CScript));
    return memory ? static_cast<Scripts::CScript*>(memory)->MakeNew() : nullptr;
}
Scripts::CStruct* SpawnParamTable()
{
    Scripts::CStruct* structure = Scripts::SpawnParamBag();
    return structure ? structure->PrepareAll() : nullptr;
}

} // namespace

// (shared with the other types' constructors: particle_system_internal.h)
namespace ParticleParts {

// The particles: `count` after their count (the compiler's array new; their constructor and destructor are empty).
sFlexibleParticle* NewParticles(int count)
{
    auto* memory = static_cast<int*>(Carve(static_cast<unsigned int>(count) * sizeof(sFlexibleParticle) + 4));
    if (!memory)
        return nullptr;
    *memory = count; // (then the empty constructor run over each: 0x0062388B with 0x005902E0)
    return reinterpret_cast<sFlexibleParticle*>(memory + 1);
}
void DeleteParticles(sFlexibleParticle* particles)
{
    // (the empty destructor run over each first: 0x0062382B with 0x005480A0)
    MemSys::Dealloc(reinterpret_cast<int*>(particles) - 1);
}

// The three colour arrays, `count` RGBA colours each (the constructor iterator 0x00401020 runs an empty one over
// them), each allocated only if the one before was; all grey once the three are there. Whether they are.
bool NewColours(CEmitterInstance* system, int count)
{
    uint8_t** colours[3] = {&system->mp_startColour, &system->mp_midColour, &system->mp_endColour};
    for (uint8_t** colour : colours) {
        *colour = static_cast<uint8_t*>(Carve(static_cast<unsigned int>(count) * 4));
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
bool ConstructParts(CEmitterInstance* system, uint32_t name, int particleCap, unsigned int positionsSize,
                    bool positionsZeroed, int colours)
{
    system->m_label = name;
    system->m_maxParticles = particleCap;
    system->m_unknown8C = 0;
    system->m_particleList = NewParticles(particleCap);
    if (!system->m_particleList)
        return false;
    system->m_positionArray = static_cast<float*>(positionsZeroed ? AllocateZeroed(positionsSize) : Carve(positionsSize));
    if (!system->m_positionArray)
        return false;
    return NewColours(system, colours);
}

void SetVtables(CEmitterInstance* system, void** primary, void** node)
{
    system->m_vtable = primary;
    system->m_listLink.vtable = node;
}

} // namespace ParticleParts

using namespace ParticleParts;

// --- life ---

// 0x00496F10
CEmitterInstance* CEmitterInstance::MakeNew()
{
    GameObjects::CSharedBase::MakeNew();
    m_listLink.priority = 0;
    m_listLink.vtable = kNodeVtable;
    m_listLink.data = this;
    m_listLink.next = &m_listLink;
    m_listLink.prev = &m_listLink;
    m_vtable = kSystemVtable;
    m_listLink.vtable = kSystemNodeVtable;
    m_flagBits = 0;
    Reset();
    return this;
}

// 0x00496D70
void CEmitterInstance::Reset()
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
    m_atPoint = Maths::Vec4{2048.0f, 144.0f, -928.0f, 0.0f};
    m_lastPos = Maths::Vec4{0.0f, 0.0f, 0.0f, 0.0f};
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
    m_orientationTurn[0] = 0.0f;
    m_orientationTurn[1] = 0.0f;
    m_orientationTurn[2] = 0.0f;
    m_rank = 0;
    for (float& size : m_extent)
        size = 2.0f;
    m_ageLeft[0] = 2.0f;
    m_ageLeft[1] = 2.0f;
    m_speed[1] = 2.0f;
    m_flagBits &= ~1u;
    for (float& scale : m_colourScale)
        scale = 1.0f;
    m_moving = false;
    m_endColourSet = false;
    m_flagBits |= 1u;
    m_unknown12F = true;
    m_flagBits &= ~0x10u;
    m_flagBits &= ~1u;
    m_randomDirection = false;
    m_flagBits |= 0x100u;
    m_circularEmit = true;
    m_aligned = false;
    m_fadingOut = false;
    m_fillPath = false;
    m_emitterIndependent = false;
    m_updateBrightness = false;
    m_unknown130 = true;
    m_unknown133 = true;
    m_unknown78 = 0;
}

// 0x00498690
void CEmitterInstance::DestroyBody()
{
    m_vtable = kSystemVtable;
    m_listLink.vtable = kSystemNodeVtable;
    if (mp_emitScript)
        DeleteScript(mp_emitScript);
    if (mp_updateScript)
        DeleteScript(mp_updateScript);
    if (Scripts::CStruct* params = mp_scriptParams) {
        params->Clear();
        Scripts::DeleteStruct(params);
    }
    if (mp_startColour)
        MemSys::Dealloc(mp_startColour);
    if (mp_midColour)
        MemSys::Dealloc(mp_midColour);
    if (mp_endColour)
        MemSys::Dealloc(mp_endColour);
    if (m_positionArray)
        MemSys::Dealloc(m_positionArray);
    if (m_particleList)
        DeleteParticles(m_particleList);
    // the particle manager lets go of it
    static_cast<CFlexibleParticleManager*>(ParticleHubHandle())->RemoveSystem(this);
    m_listLink.UnlinkAs(kNodeVtable);
    GameObjects::CSharedBase::Destruct();
}

// 0x00498F80
CEmitterInstance* CEmitterInstance::Teardown(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        MemSys::Dealloc(this);
    return this;
}

// --- settings ---

// 0x004968B0
void CEmitterInstance::SetEmitRange(float width, float height, float depth)
{
    m_emitRange[0] = width;
    m_emitRange[1] = height;
    m_emitRange[2] = depth;
}

// 0x004968E0
void CEmitterInstance::SetForce(float x, float y, float z)
{
    m_force[0] = x;
    m_force[1] = y;
    m_force[2] = z;
}

// 0x00496910
void CEmitterInstance::SetEmitTarget(float x, float y, float z)
{
    m_emitTarget[0] = x;
    m_emitTarget[1] = y;
    m_emitTarget[2] = z;
}

// 0x00496940
void CEmitterInstance::ChangeSpin(float x, float y, float z)
{
    m_orientationTurn[0] = x;
    m_orientationTurn[1] = y;
    m_orientationTurn[2] = z;
}

// 0x00496970
void CEmitterInstance::FetchPlace(int particle, int step, float* x, float* y, float* z)
{
    const float* position = m_positionArray + 3 * (m_maxParticles * step + particle);
    *x = position[0];
    *y = position[1];
    *z = position[2];
}

// 0x004969B0
void CEmitterInstance::ChangePlace(int particle, int step, float x, float y, float z)
{
    float* position = m_positionArray + 3 * (m_maxParticles * step + particle);
    position[0] = x;
    position[1] = y;
    position[2] = z;
}

// 0x004969F0
void CEmitterInstance::GetEmitMatrix(Maths::Mat4* out)
{
    if (mp_matrix) {
        *out = *mp_matrix;
        return;
    }
    out->row[0] = Maths::Vec4{1.0f, 0.0f, 0.0f, 0.0f};
    out->row[1] = Maths::Vec4{0.0f, 1.0f, 0.0f, 0.0f};
    out->row[2] = Maths::Vec4{0.0f, 0.0f, 1.0f, 0.0f};
    out->row[3] = Maths::Vec4{0.0f, 0.0f, 0.0f, 1.0f};
    Maths::Vec4& at = out->row[2];
    at = Maths::Vec4{m_emitTarget[0], m_emitTarget[1], m_emitTarget[2], 1.0f};
    at.MakeUnit();
    // across: up x the target (along x when the target is nearly up); then the second row across both (x87, 53 bits)
    double up = 1.0, along = 0.0;
    if (std::fabs(at.y) > kNearlyUp) {
        up = 0.0;
        along = 1.0;
    }
    Maths::Vec4& across = out->row[0];
    across.w = 0.0f;
    float z = static_cast<float>(along * at.y - up * at.x);
    across.x = static_cast<float>(up * at.z - static_cast<double>(at.y) * 0.0);
    across.y = static_cast<float>(static_cast<double>(at.x) * 0.0 - along * at.z);
    across.z = z;
    Maths::Vec4& second = out->row[1];
    second.w = 0.0f;
    float secondZ = static_cast<float>(static_cast<double>(at.y) * across.x - static_cast<double>(at.x) * across.y);
    second.x = static_cast<float>(static_cast<double>(across.y) * at.z - static_cast<double>(across.z) * at.y);
    second.y = static_cast<float>(static_cast<double>(across.z) * at.x - static_cast<double>(across.x) * at.z);
    second.z = secondZ;
}

// 0x00496B40
int CEmitterInstance::LodParticleCount(int count)
{
    if (m_lodNear >= m_lodFar || ViewportCount() > 1) // (NaN distances go on)
        return count;
    Gfx2D::ViewCam* camera = ViewportCamera(0);
    if (!camera)
        return count;
    const Maths::Vec4* eye = camera->FetchLocation();
    double dx = static_cast<double>(m_atPoint.x) - eye->x;
    double dy = static_cast<double>(m_atPoint.y) - eye->y;
    double dz = static_cast<double>(m_atPoint.z) - eye->z;
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
void CEmitterInstance::UpdateBounds()
{
    float force = std::fabs(m_force[2]);
    if (std::fabs(m_force[1]) > force)
        force = std::fabs(m_force[1]);
    if (std::fabs(m_force[0]) > force)
        force = std::fabs(m_force[0]);
    double travel = static_cast<double>(force) * m_ageLeft[1] + m_speed[1];
    m_bounds = m_atPoint;
    float range = m_emitRange[2];
    if (m_emitRange[1] > range)
        range = m_emitRange[1];
    if (m_emitRange[0] > range)
        range = m_emitRange[0];
    m_bounds.w = static_cast<float>(travel * m_ageLeft[1] * kFramesPerSecond + range);
}

// 0x00496CE0
void CEmitterInstance::SetColourScale(const float* rgba)
{
    if (m_flagBits & 0x10000) {
        for (int i = 0; i < 4; ++i)
            m_colourScale[i] = rgba[i];
    }
}

// 0x00496F60
void CEmitterInstance::RunEmitScript()
{
    if (mp_emitScript && mp_emitScript->TickFrame() == Scripts::SCRIPT_OUTCOME_FINISHED) {
        if (Scripts::CScript* script = mp_emitScript)
            DeleteScript(script);
        mp_emitScript = nullptr;
    }
}

// 0x00496FA0
void CEmitterInstance::RenderIfVisible(bool force)
{
    if ((m_unknown12F && m_unknown130) || force) {
        reinterpret_cast<void(__thiscall*)(CEmitterInstance*)>(m_vtable[1])(this);
        m_flagBits &= ~1u;
        m_unknown12F = false;
    }
}

namespace {

// 0x00497790 / 0x00497900: a script slot set (see SetEmitScript).
void BindScript(CEmitterInstance* system, Scripts::CScript*& slot, uint32_t script, Scripts::CStruct* params,
               void* object)
{
    if (!script) {
        if (Scripts::CScript* old = slot)
            DeleteScript(old);
        slot = nullptr;
        return;
    }
    if (!slot)
        slot = NewScript();
    if (!system->mp_scriptParams)
        system->mp_scriptParams = SpawnParamTable();
    system->mp_scriptParams->AppendStructureIfAny(params);
    slot->BindScript(script, system->mp_scriptParams, static_cast<GameObjects::CEntity*>(object));
}

} // namespace

// 0x00497790
void CEmitterInstance::SetEmitScript(uint32_t script, Scripts::CStruct* params, void* object)
{
    BindScript(this, mp_emitScript, script, params, object);
}

// 0x00497900
void CEmitterInstance::SetUpdateScript(uint32_t script, Scripts::CStruct* params, void* object)
{
    BindScript(this, mp_updateScript, script, params, object);
}

// 0x00497A70
void CEmitterInstance::SetWantsTime(uint8_t on)
{
    if (on)
        m_flagBits |= 0x10000u;
    else
        m_flagBits &= ~0x10000u;
    reinterpret_cast<uint8_t&>(m_wantsTime) = on;
}

// 0x00497AB0
Maths::Mat4* CEmitterInstance::GetRotationMatrix(Maths::Mat4* out)
{
    Maths::Vec4 angles{m_orientationTurn[0], m_orientationTurn[1], m_orientationTurn[2], 1.0f};
    Maths::Mat4 rotation;
    rotation.InitFromAngles(angles);
    *out = rotation;
    return out;
}

// 0x00496890
int __cdecl CEmitterInstance::GetNumColoursStub()
{
    DebugOut::Printf("STUB: plat_get_num_particle_colors\n");
    return 0;
}

// 0x004968A0
int __cdecl CEmitterInstance::GetNumVertexListsStub()
{
    DebugOut::Printf("STUB: plat_get_num_vertex_lists\n");
    return 0;
}

// --- the types ---

// 0x00499010
CNewFlatParticles* CNewFlatParticles::MakeNew(uint32_t name, int particleCap, uint32_t, uint32_t, int, int, float,
                                                int)
{
    CEmitterInstance::MakeNew();
    SetVtables(this, kCNewFlatParticlesVtable, kCNewFlatParticlesNodeVtable);
    if (ConstructParts(this, name, particleCap, static_cast<unsigned int>(particleCap) * 12, false, 1)) {
        m_colourMid = -1.0f;
        m_flagBits |= 4u;
    }
    return this;
}

// 0x00498FB0
void CNewFlatParticles::DestroyBody()
{
    SetVtables(this, kCNewFlatParticlesVtable, kCNewFlatParticlesNodeVtable);
    CEmitterInstance::DestroyBody();
}

// 0x00498FE0
CNewFlatParticles* CNewFlatParticles::Teardown(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        MemSys::Dealloc(this);
    return this;
}

// 0x004992C0
CFlatRibbonParticles* CFlatRibbonParticles::MakeNew(uint32_t name, int particleCap, uint32_t, uint32_t, int, int,
                                                      float, int history)
{
    CEmitterInstance::MakeNew();
    SetVtables(this, kCFlatRibbonParticlesVtable, kCFlatRibbonParticlesNodeVtable);
    m_segments = history + 1;
    // (the colours' count read again as m_segments + 3 for each array and each step of the loop)
    if (ConstructParts(this, name, particleCap, static_cast<unsigned int>(particleCap * m_segments * 12), true,
                       m_segments + 3)) {
        m_colourMid = -1.0f;
        m_flagBits |= 4u;
    }
    return this;
}

// 0x00499200
void CFlatRibbonParticles::DestroyBody()
{
    SetVtables(this, kCFlatRibbonParticlesVtable, kCFlatRibbonParticlesNodeVtable);
    if (m_particleList) {
        DeleteParticles(m_particleList);
        m_particleList = nullptr;
    }
    CEmitterInstance::DestroyBody();
}

// 0x00499290
CFlatRibbonParticles* CFlatRibbonParticles::Teardown(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        MemSys::Dealloc(this);
    return this;
}

// 0x004991E0
int CFlatRibbonParticles::GetNumColours() { return m_segments + 3; }

// 0x004995B0
CGlowParticles* CGlowParticles::MakeNew(uint32_t name, int particleCap, uint32_t, uint32_t, int, int segments,
                                          float glowWidth, int)
{
    CEmitterInstance::MakeNew();
    SetVtables(this, kCGlowParticlesVtable, kCGlowParticlesNodeVtable);
    if (ConstructParts(this, name, particleCap, static_cast<unsigned int>(particleCap) * 12, false, 3)) {
        m_segments = segments;
        m_colourMid = -1.0f;
        m_glowWidth = glowWidth;
        m_flagBits |= 4u;
    }
    return this;
}

// 0x00499550
void CGlowParticles::DestroyBody()
{
    SetVtables(this, kCGlowParticlesVtable, kCGlowParticlesNodeVtable);
    CEmitterInstance::DestroyBody();
}

// 0x00499580
CGlowParticles* CGlowParticles::Teardown(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        MemSys::Dealloc(this);
    return this;
}

// 0x00499880
CGlowRibbonTrailParticles* CGlowRibbonTrailParticles::MakeNew(uint32_t name, int particleCap, uint32_t, uint32_t,
                                                                int, int segments, float width, int history)
{
    CEmitterInstance::MakeNew();
    SetVtables(this, kCGlowRibbonTrailParticlesVtable, kCGlowRibbonTrailParticlesNodeVtable);
    m_historyLength = history + 1;
    if (ConstructParts(this, name, particleCap, static_cast<unsigned int>((history + 1) * particleCap * 12), true,
                       m_historyLength + 3)) {
        m_segments = segments;
        m_colourMid = -1.0f;
        m_span = width;
        m_flagBits |= 4u;
    }
    return this;
}

// 0x00499800
void CGlowRibbonTrailParticles::DestroyBody()
{
    SetVtables(this, kCGlowRibbonTrailParticlesVtable, kCGlowRibbonTrailParticlesNodeVtable);
    CEmitterInstance::DestroyBody();
}

// 0x00499850
CGlowRibbonTrailParticles* CGlowRibbonTrailParticles::Teardown(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        MemSys::Dealloc(this);
    return this;
}

// 0x00499820 / 0x00499830
int CGlowRibbonTrailParticles::GetNumColours() { return m_historyLength + 3; }
int CGlowRibbonTrailParticles::GetNumVertexLists() { return m_historyLength; }

// 0x00499B80
CLineParticles* CLineParticles::MakeNew(uint32_t name, int particleCap, uint32_t, uint32_t, int, int, float,
                                          int history)
{
    CEmitterInstance::MakeNew();
    SetVtables(this, kCLineParticlesVtable, kCLineParticlesNodeVtable);
    m_steps = history + 1;
    if (ConstructParts(this, name, particleCap, static_cast<unsigned int>(particleCap * (history + 1) * 12), true,
                       m_steps)) {
        m_colourMid = -1.0f;
        m_flagBits |= 4u;
    }
    return this;
}

// 0x00499B20
void CLineParticles::DestroyBody()
{
    SetVtables(this, kCLineParticlesVtable, kCLineParticlesNodeVtable);
    CEmitterInstance::DestroyBody();
}

// 0x00499B50
CLineParticles* CLineParticles::Teardown(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        MemSys::Dealloc(this);
    return this;
}

// 0x00499E10
void CModelParticles::DestroyBody()
{
    SetVtables(this, Vt::kModelParticles, Vt::kModelParticlesNode);
    if (mp_unknown13C) {
        MemSys::Dealloc(mp_unknown13C);
        mp_unknown13C = nullptr;
    }
    if (m_modelList) {
        for (unsigned int i = 0; i < m_modelCount; ++i)
            Engine::DeleteGeomObject(m_modelList[i]); // (the model deleted)
        MemSys::Dealloc(m_modelList);
        m_modelList = nullptr;
    }
    CEmitterInstance::DestroyBody();
}

// 0x0049A090
CModelParticles* CModelParticles::Teardown(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        MemSys::Dealloc(this);
    return this;
}

} // namespace Nx

namespace {

// The destructors reached through the node's vtable (`this` the node at +0x0C): each a jump to the type's.
struct NodeAdapters {
    // 0x00496D10 / 0x00496D30: ListKit::Elem<CEmitterInstance>'s own destructor body (its vtable, out of its list) and
    // scalar deleting destructor (freed with flags & 1; returns this).
    void NodeDestroyBody() { reinterpret_cast<MemSys::ListNode*>(this)->UnlinkAs(Vt::kParticleNode); }
    MemSys::ListNode* NodeDestroy(unsigned int flags)
    {
        NodeDestroyBody();
        if (flags & 1)
            MemSys::Dealloc(this);
        return reinterpret_cast<MemSys::ListNode*>(this);
    }
    template <typename T>
    T* System()
    {
        return reinterpret_cast<T*>(reinterpret_cast<uint8_t*>(this) - offsetof(Engine3D::CEmitterInstance, m_listLink));
    }
    Engine3D::CEmitterInstance* DestroySystem(unsigned int flags) { return System<Engine3D::CEmitterInstance>()->Teardown(flags); }
    Engine3D::CNewFlatParticles* DestroyNewFlat(unsigned int flags) { return System<Engine3D::CNewFlatParticles>()->Teardown(flags); }
    Engine3D::CFlatRibbonParticles* DestroyFlatRibbon(unsigned int flags)
    {
        return System<Engine3D::CFlatRibbonParticles>()->Teardown(flags);
    }
    Engine3D::CGlowParticles* DestroyGlow(unsigned int flags) { return System<Engine3D::CGlowParticles>()->Teardown(flags); }
    Engine3D::CGlowRibbonTrailParticles* DestroyGlowRibbonTrail(unsigned int flags)
    {
        return System<Engine3D::CGlowRibbonTrailParticles>()->Teardown(flags);
    }
    Engine3D::CLineParticles* DestroyLine(unsigned int flags) { return System<Engine3D::CLineParticles>()->Teardown(flags); }
    Engine3D::CModelParticles* DestroyModel(unsigned int flags) { return System<Engine3D::CModelParticles>()->Teardown(flags); }
};

} // namespace

namespace Engine3D {
namespace {
static VtableSlot kSystemVtable_slots[] = {&Engine3D::CEmitterInstance::Teardown, &Engine3D::CWeatherSystem::Render,
                Engine3D::CEmitterInstance::GetNumVertexListsStub, Engine3D::CEmitterInstance::GetNumColoursStub};
void** const kSystemVtable = reinterpret_cast<void**>(kSystemVtable_slots);
static VtableSlot kSystemNodeVtable_slots[] = {&NodeAdapters::DestroySystem};
void** const kSystemNodeVtable = reinterpret_cast<void**>(kSystemNodeVtable_slots);
static VtableSlot kCNewFlatParticlesNodeVtable_slots[] = {&NodeAdapters::DestroyNewFlat};
void** const kCNewFlatParticlesNodeVtable = reinterpret_cast<void**>(kCNewFlatParticlesNodeVtable_slots);
static VtableSlot kCNewFlatParticlesVtable_slots[] = {&Engine3D::CNewFlatParticles::Teardown, &Engine3D::CWeatherSystem::Render,
                OnlinePlay::HandleNothing, OnlinePlay::HandleNothing};
void** const kCNewFlatParticlesVtable = reinterpret_cast<void**>(kCNewFlatParticlesVtable_slots);
static VtableSlot kCFlatRibbonParticlesNodeVtable_slots[] = {&NodeAdapters::DestroyFlatRibbon};
void** const kCFlatRibbonParticlesNodeVtable = reinterpret_cast<void**>(kCFlatRibbonParticlesNodeVtable_slots);
static VtableSlot kCFlatRibbonParticlesVtable_slots[] = {&Engine3D::CFlatRibbonParticles::Teardown, &Engine3D::CWeatherSystem::Render,
                &Engine3D::CParticleCounts::Field134, &Engine3D::CFlatRibbonParticles::GetNumColours};
void** const kCFlatRibbonParticlesVtable = reinterpret_cast<void**>(kCFlatRibbonParticlesVtable_slots);
static VtableSlot kCGlowParticlesNodeVtable_slots[] = {&NodeAdapters::DestroyGlow};
void** const kCGlowParticlesNodeVtable = reinterpret_cast<void**>(kCGlowParticlesNodeVtable_slots);
static VtableSlot kCGlowParticlesVtable_slots[] = {&Engine3D::CGlowParticles::Teardown, &Engine3D::CWeatherSystem::Render,
                OnlinePlay::HandleNothing, &Engine3D::CParticleCounts::ThreeColours};
void** const kCGlowParticlesVtable = reinterpret_cast<void**>(kCGlowParticlesVtable_slots);
static VtableSlot kCGlowRibbonTrailParticlesNodeVtable_slots[] = {&NodeAdapters::DestroyGlowRibbonTrail};
void** const kCGlowRibbonTrailParticlesNodeVtable = reinterpret_cast<void**>(kCGlowRibbonTrailParticlesNodeVtable_slots);
static VtableSlot kCGlowRibbonTrailParticlesVtable_slots[] = {&Engine3D::CGlowRibbonTrailParticles::Teardown,
                &Engine3D::CWeatherSystem::Render, &Engine3D::CGlowRibbonTrailParticles::GetNumVertexLists,
                &Engine3D::CGlowRibbonTrailParticles::GetNumColours};
void** const kCGlowRibbonTrailParticlesVtable = reinterpret_cast<void**>(kCGlowRibbonTrailParticlesVtable_slots);
static VtableSlot kCLineParticlesNodeVtable_slots[] = {&NodeAdapters::DestroyLine};
void** const kCLineParticlesNodeVtable = reinterpret_cast<void**>(kCLineParticlesNodeVtable_slots);
static VtableSlot kCLineParticlesVtable_slots[] = {&Engine3D::CLineParticles::Teardown, &Engine3D::CWeatherSystem::Render,
                &Engine3D::CParticleCounts::Field134, &Engine3D::CParticleCounts::TwoColours};
void** const kCLineParticlesVtable = reinterpret_cast<void**>(kCLineParticlesVtable_slots);
} // namespace
} // namespace Nx

namespace Vt {
static VtableSlot kParticleNode_slots[] = {&NodeAdapters::NodeDestroy};
void** const kParticleNode = reinterpret_cast<void**>(kParticleNode_slots);
static VtableSlot kModelParticlesNode_slots[] = {&NodeAdapters::DestroyModel};
void** const kModelParticlesNode = reinterpret_cast<void**>(kModelParticlesNode_slots);
static VtableSlot kModelParticles_slots[] = {&Engine3D::CModelParticles::Teardown, &Engine3D::CModelParticles::Render,
                OnlinePlay::HandleNothing, OnlinePlay::HandleNothing};
void** const kModelParticles = reinterpret_cast<void**>(kModelParticles_slots);
} // namespace Vt
