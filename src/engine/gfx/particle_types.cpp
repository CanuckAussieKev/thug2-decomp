// The flexible particle types' engine classes: ribbon, ribbontrail, shaded, smooth, smoothribbon, smoothstar, star
// (see particle_types.h). From the disassembly of 0x0049B070-0x0049C384.

#include <cstddef>
#include <cstdint>

#include "engine/gfx/particle_types.h"

#include "engine/core/mem.h"
#include "engine/gfx/particle_system_internal.h"
#include "engine/core/code_address.h"
#include "engine/net/gamenet_handlers.h"
#include "engine/gfx/weather.h"

namespace Engine3D {

namespace {
extern void** const kCRibbonParticlesNodeVtable;
extern void** const kCRibbonParticlesVtable;
extern void** const kCRibbonTrailParticlesNodeVtable;
extern void** const kCRibbonTrailParticlesVtable;
extern void** const kCShadedParticlesNodeVtable;
extern void** const kCShadedParticlesVtable;
extern void** const kCSmoothParticlesNodeVtable;
extern void** const kCSmoothParticlesVtable;
extern void** const kCSmoothRibbonParticlesNodeVtable;
extern void** const kCSmoothRibbonParticlesVtable;
extern void** const kCSmoothStarParticlesNodeVtable;
extern void** const kCSmoothStarParticlesVtable;
extern void** const kCStarParticlesNodeVtable;
extern void** const kCStarParticlesVtable;
} // namespace

using namespace ParticleParts;

namespace {

constexpr unsigned int kPosition = 12; // three floats

// The scalar deleting destructors: the body, freed with flags & 1. Returns the system.
template <typename T>
T* DestroyAndFree(T* system, unsigned int flags)
{
    system->DestroyBody();
    if (flags & 1)
        MemSys::Dealloc(system);
    return system;
}

} // namespace

// --- ribbon ---

// 0x0049B0D0
CRibbonParticles* CRibbonParticles::MakeNew(uint32_t name, int particleCap, uint32_t, uint32_t, int, int, float,
                                              int history)
{
    CEmitterInstance::MakeNew();
    SetVtables(this, kCRibbonParticlesVtable, kCRibbonParticlesNodeVtable);
    m_steps = history + 1;
    if (ConstructParts(this, name, particleCap, static_cast<unsigned int>(particleCap * m_steps) * kPosition, true,
                       2)) {
        m_colourMid = -1.0f;
        m_flagBits |= 4u;
    }
    return this;
}

// 0x0049B070
void CRibbonParticles::DestroyBody()
{
    SetVtables(this, kCRibbonParticlesVtable, kCRibbonParticlesNodeVtable);
    CEmitterInstance::DestroyBody();
}

// 0x0049B0A0
CRibbonParticles* CRibbonParticles::Teardown(unsigned int flags) { return DestroyAndFree(this, flags); }

// --- ribbontrail ---

// 0x0049B380
CRibbonTrailParticles* CRibbonTrailParticles::MakeNew(uint32_t name, int particleCap, uint32_t, uint32_t, int, int,
                                                        float, int history)
{
    CEmitterInstance::MakeNew();
    SetVtables(this, kCRibbonTrailParticlesVtable, kCRibbonTrailParticlesNodeVtable);
    m_steps = history + 1;
    // (the colours' count read again from m_steps for each array and each step of the loop)
    if (ConstructParts(this, name, particleCap, static_cast<unsigned int>(particleCap * m_steps) * kPosition, true,
                       m_steps)) {
        m_colourMid = -1.0f;
        m_flagBits |= 4u;
    }
    return this;
}

// 0x0049B320
void CRibbonTrailParticles::DestroyBody()
{
    SetVtables(this, kCRibbonTrailParticlesVtable, kCRibbonTrailParticlesNodeVtable);
    CEmitterInstance::DestroyBody();
}

// 0x0049B350
CRibbonTrailParticles* CRibbonTrailParticles::Teardown(unsigned int flags) { return DestroyAndFree(this, flags); }

// --- shaded ---

// 0x0049B660
CShadedParticles* CShadedParticles::MakeNew(uint32_t name, int particleCap, uint32_t, uint32_t, int, int, float,
                                              int)
{
    CEmitterInstance::MakeNew();
    SetVtables(this, kCShadedParticlesVtable, kCShadedParticlesNodeVtable);
    if (ConstructParts(this, name, particleCap, static_cast<unsigned int>(particleCap) * kPosition, false, 4)) {
        m_colourMid = -1.0f;
        m_flagBits |= 4u;
    }
    return this;
}

// 0x0049B600
void CShadedParticles::DestroyBody()
{
    SetVtables(this, kCShadedParticlesVtable, kCShadedParticlesNodeVtable);
    CEmitterInstance::DestroyBody();
}

// 0x0049B630
CShadedParticles* CShadedParticles::Teardown(unsigned int flags) { return DestroyAndFree(this, flags); }

// --- smooth ---

// 0x0049B910
CSmoothParticles* CSmoothParticles::MakeNew(uint32_t name, int particleCap, uint32_t, uint32_t, int, int, float,
                                              int)
{
    CEmitterInstance::MakeNew();
    SetVtables(this, kCSmoothParticlesVtable, kCSmoothParticlesNodeVtable);
    if (ConstructParts(this, name, particleCap, static_cast<unsigned int>(particleCap) * kPosition, false, 2)) {
        m_colourMid = -1.0f;
        m_flagBits |= 4u;
    }
    return this;
}

// 0x0049B8A0
void CSmoothParticles::DestroyBody()
{
    SetVtables(this, kCSmoothParticlesVtable, kCSmoothParticlesNodeVtable);
    CEmitterInstance::DestroyBody();
}

// 0x0049B8E0
CSmoothParticles* CSmoothParticles::Teardown(unsigned int flags) { return DestroyAndFree(this, flags); }

// --- smoothribbon ---

// 0x0049BBD0
CSmoothRibbonParticles* CSmoothRibbonParticles::MakeNew(uint32_t name, int particleCap, uint32_t, uint32_t, int,
                                                          int, float, int history)
{
    CEmitterInstance::MakeNew();
    SetVtables(this, kCSmoothRibbonParticlesVtable, kCSmoothRibbonParticlesNodeVtable);
    if (ConstructParts(this, name, particleCap, static_cast<unsigned int>(particleCap * (history + 1)) * kPosition,
                       true, 4)) {
        m_steps = history + 1;
        m_colourMid = -1.0f;
        m_flagBits |= 4u;
    }
    return this;
}

// 0x0049BB50
void CSmoothRibbonParticles::DestroyBody()
{
    SetVtables(this, kCSmoothRibbonParticlesVtable, kCSmoothRibbonParticlesNodeVtable);
    CEmitterInstance::DestroyBody();
}

// 0x0049BBA0
CSmoothRibbonParticles* CSmoothRibbonParticles::Teardown(unsigned int flags) { return DestroyAndFree(this, flags); }

// --- smoothstar ---

// 0x0049BE80
CSmoothStarParticles* CSmoothStarParticles::MakeNew(uint32_t name, int particleCap, uint32_t, uint32_t, int,
                                                      int segments, float size, int)
{
    CEmitterInstance::MakeNew();
    SetVtables(this, kCSmoothStarParticlesVtable, kCSmoothStarParticlesNodeVtable);
    if (ConstructParts(this, name, particleCap, static_cast<unsigned int>(particleCap) * kPosition, false, 3)) {
        m_segments = segments;
        m_colourMid = -1.0f;
        m_extent = size;
        m_flagBits |= 4u;
    }
    return this;
}

// 0x0049BE20
void CSmoothStarParticles::DestroyBody()
{
    SetVtables(this, kCSmoothStarParticlesVtable, kCSmoothStarParticlesNodeVtable);
    CEmitterInstance::DestroyBody();
}

// 0x0049BE50
CSmoothStarParticles* CSmoothStarParticles::Teardown(unsigned int flags) { return DestroyAndFree(this, flags); }

// --- star ---

// 0x0049C140
CStarParticles* CStarParticles::MakeNew(uint32_t name, int particleCap, uint32_t, uint32_t, int, int segments,
                                          float size, int)
{
    CEmitterInstance::MakeNew();
    SetVtables(this, kCStarParticlesVtable, kCStarParticlesNodeVtable);
    if (ConstructParts(this, name, particleCap, static_cast<unsigned int>(particleCap) * kPosition, false, 3)) {
        m_segments = segments;
        m_colourMid = -1.0f;
        m_extent = size;
        m_flagBits |= 4u;
    }
    return this;
}

// 0x0049C0D0
void CStarParticles::DestroyBody()
{
    SetVtables(this, kCStarParticlesVtable, kCStarParticlesNodeVtable);
    CEmitterInstance::DestroyBody();
}

// 0x0049C110
CStarParticles* CStarParticles::Teardown(unsigned int flags) { return DestroyAndFree(this, flags); }

// --- the counts ---

// 0x0049B8C0 / 0x0049BB70 / 0x0049C0F0 / 0x0049BB80
int CParticleCounts::TwoColours() { return 2; }
int CParticleCounts::FourColours() { return 4; }
int CParticleCounts::ThreeColours() { return 3; }
int CParticleCounts::Field134() { return *reinterpret_cast<const int*>(reinterpret_cast<const uint8_t*>(this) + 0x134); }

} // namespace Nx

namespace {

// The destructors reached through the node's vtable (`this` the node at +0x0C): each a jump to the type's.
struct TypeNodeAdapters {
    template <typename T>
    T* System()
    {
        return reinterpret_cast<T*>(reinterpret_cast<uint8_t*>(this) - offsetof(Engine3D::CEmitterInstance, m_listLink));
    }
    Engine3D::CRibbonParticles* DestroyRibbon(unsigned int flags) { return System<Engine3D::CRibbonParticles>()->Teardown(flags); }
    Engine3D::CRibbonTrailParticles* DestroyRibbonTrail(unsigned int flags)
    {
        return System<Engine3D::CRibbonTrailParticles>()->Teardown(flags);
    }
    Engine3D::CShadedParticles* DestroyShaded(unsigned int flags) { return System<Engine3D::CShadedParticles>()->Teardown(flags); }
    Engine3D::CSmoothParticles* DestroySmooth(unsigned int flags) { return System<Engine3D::CSmoothParticles>()->Teardown(flags); }
    Engine3D::CSmoothRibbonParticles* DestroySmoothRibbon(unsigned int flags)
    {
        return System<Engine3D::CSmoothRibbonParticles>()->Teardown(flags);
    }
    Engine3D::CSmoothStarParticles* DestroySmoothStar(unsigned int flags)
    {
        return System<Engine3D::CSmoothStarParticles>()->Teardown(flags);
    }
    Engine3D::CStarParticles* DestroyStar(unsigned int flags) { return System<Engine3D::CStarParticles>()->Teardown(flags); }
};

} // namespace

namespace Engine3D {
namespace {
static VtableSlot kCRibbonParticlesNodeVtable_slots[] = {&TypeNodeAdapters::DestroyRibbon};
void** const kCRibbonParticlesNodeVtable = reinterpret_cast<void**>(kCRibbonParticlesNodeVtable_slots);
static VtableSlot kCRibbonParticlesVtable_slots[] = {&Engine3D::CRibbonParticles::Teardown, &Engine3D::CWeatherSystem::Render,
                &Engine3D::CParticleCounts::Field134, &Engine3D::CParticleCounts::TwoColours};
void** const kCRibbonParticlesVtable = reinterpret_cast<void**>(kCRibbonParticlesVtable_slots);
static VtableSlot kCRibbonTrailParticlesNodeVtable_slots[] = {&TypeNodeAdapters::DestroyRibbonTrail};
void** const kCRibbonTrailParticlesNodeVtable = reinterpret_cast<void**>(kCRibbonTrailParticlesNodeVtable_slots);
static VtableSlot kCRibbonTrailParticlesVtable_slots[] = {&Engine3D::CRibbonTrailParticles::Teardown, &Engine3D::CWeatherSystem::Render,
                &Engine3D::CParticleCounts::Field134, &Engine3D::CParticleCounts::Field134};
void** const kCRibbonTrailParticlesVtable = reinterpret_cast<void**>(kCRibbonTrailParticlesVtable_slots);
static VtableSlot kCShadedParticlesNodeVtable_slots[] = {&TypeNodeAdapters::DestroyShaded};
void** const kCShadedParticlesNodeVtable = reinterpret_cast<void**>(kCShadedParticlesNodeVtable_slots);
static VtableSlot kCShadedParticlesVtable_slots[] = {&Engine3D::CShadedParticles::Teardown, &Engine3D::CWeatherSystem::Render,
                OnlinePlay::HandleNothing, &Engine3D::CParticleCounts::FourColours};
void** const kCShadedParticlesVtable = reinterpret_cast<void**>(kCShadedParticlesVtable_slots);
static VtableSlot kCSmoothParticlesNodeVtable_slots[] = {&TypeNodeAdapters::DestroySmooth};
void** const kCSmoothParticlesNodeVtable = reinterpret_cast<void**>(kCSmoothParticlesNodeVtable_slots);
static VtableSlot kCSmoothParticlesVtable_slots[] = {&Engine3D::CSmoothParticles::Teardown, &Engine3D::CWeatherSystem::Render,
                OnlinePlay::HandleNothing, &Engine3D::CParticleCounts::TwoColours};
void** const kCSmoothParticlesVtable = reinterpret_cast<void**>(kCSmoothParticlesVtable_slots);
static VtableSlot kCSmoothRibbonParticlesNodeVtable_slots[] = {&TypeNodeAdapters::DestroySmoothRibbon};
void** const kCSmoothRibbonParticlesNodeVtable = reinterpret_cast<void**>(kCSmoothRibbonParticlesNodeVtable_slots);
static VtableSlot kCSmoothRibbonParticlesVtable_slots[] = {&Engine3D::CSmoothRibbonParticles::Teardown, &Engine3D::CWeatherSystem::Render,
                &Engine3D::CParticleCounts::Field134, &Engine3D::CParticleCounts::FourColours};
void** const kCSmoothRibbonParticlesVtable = reinterpret_cast<void**>(kCSmoothRibbonParticlesVtable_slots);
static VtableSlot kCSmoothStarParticlesNodeVtable_slots[] = {&TypeNodeAdapters::DestroySmoothStar};
void** const kCSmoothStarParticlesNodeVtable = reinterpret_cast<void**>(kCSmoothStarParticlesNodeVtable_slots);
static VtableSlot kCSmoothStarParticlesVtable_slots[] = {&Engine3D::CSmoothStarParticles::Teardown, &Engine3D::CWeatherSystem::Render,
                OnlinePlay::HandleNothing, &Engine3D::CParticleCounts::ThreeColours};
void** const kCSmoothStarParticlesVtable = reinterpret_cast<void**>(kCSmoothStarParticlesVtable_slots);
static VtableSlot kCStarParticlesNodeVtable_slots[] = {&TypeNodeAdapters::DestroyStar};
void** const kCStarParticlesNodeVtable = reinterpret_cast<void**>(kCStarParticlesNodeVtable_slots);
static VtableSlot kCStarParticlesVtable_slots[] = {&Engine3D::CStarParticles::Teardown, &Engine3D::CWeatherSystem::Render,
                OnlinePlay::HandleNothing, &Engine3D::CParticleCounts::ThreeColours};
void** const kCStarParticlesVtable = reinterpret_cast<void**>(kCStarParticlesVtable_slots);
} // namespace
} // namespace Nx
