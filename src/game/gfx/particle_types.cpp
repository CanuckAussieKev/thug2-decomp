// The flexible particle types' engine classes: ribbon, ribbontrail, shaded, smooth, smoothribbon, smoothstar, star
// (see particle_types.h). From the disassembly of 0x0049B070-0x0049C384.

#include "game/gfx/particle_types.h"

#include "game/core/mem.h"
#include "game/gfx/particle_system_internal.h"
#include "hook/hook.h"
#include "hook/game_code.h"
#include "game/net/gamenet_handlers.h"
#include "game/gfx/weather.h"

namespace Nx {

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
        Mem::Free(system);
    return system;
}

} // namespace

// --- ribbon ---

// 0x0049B0D0
CRibbonParticles* CRibbonParticles::Construct(uint32_t name, int maxParticles, uint32_t, uint32_t, int, int, float,
                                              int history)
{
    CParticleSystem::Construct();
    SetVtables(this, kCRibbonParticlesVtable, kCRibbonParticlesNodeVtable);
    m_steps = history + 1;
    if (ConstructParts(this, name, maxParticles, static_cast<unsigned int>(maxParticles * m_steps) * kPosition, true,
                       2)) {
        m_colourMid = -1.0f;
        m_flags |= 4u;
    }
    return this;
}

// 0x0049B070
void CRibbonParticles::DestroyBody()
{
    SetVtables(this, kCRibbonParticlesVtable, kCRibbonParticlesNodeVtable);
    CParticleSystem::DestroyBody();
}

// 0x0049B0A0
CRibbonParticles* CRibbonParticles::Destroy(unsigned int flags) { return DestroyAndFree(this, flags); }

// --- ribbontrail ---

// 0x0049B380
CRibbonTrailParticles* CRibbonTrailParticles::Construct(uint32_t name, int maxParticles, uint32_t, uint32_t, int, int,
                                                        float, int history)
{
    CParticleSystem::Construct();
    SetVtables(this, kCRibbonTrailParticlesVtable, kCRibbonTrailParticlesNodeVtable);
    m_steps = history + 1;
    // (the colours' count read again from m_steps for each array and each step of the loop)
    if (ConstructParts(this, name, maxParticles, static_cast<unsigned int>(maxParticles * m_steps) * kPosition, true,
                       m_steps)) {
        m_colourMid = -1.0f;
        m_flags |= 4u;
    }
    return this;
}

// 0x0049B320
void CRibbonTrailParticles::DestroyBody()
{
    SetVtables(this, kCRibbonTrailParticlesVtable, kCRibbonTrailParticlesNodeVtable);
    CParticleSystem::DestroyBody();
}

// 0x0049B350
CRibbonTrailParticles* CRibbonTrailParticles::Destroy(unsigned int flags) { return DestroyAndFree(this, flags); }

// --- shaded ---

// 0x0049B660
CShadedParticles* CShadedParticles::Construct(uint32_t name, int maxParticles, uint32_t, uint32_t, int, int, float,
                                              int)
{
    CParticleSystem::Construct();
    SetVtables(this, kCShadedParticlesVtable, kCShadedParticlesNodeVtable);
    if (ConstructParts(this, name, maxParticles, static_cast<unsigned int>(maxParticles) * kPosition, false, 4)) {
        m_colourMid = -1.0f;
        m_flags |= 4u;
    }
    return this;
}

// 0x0049B600
void CShadedParticles::DestroyBody()
{
    SetVtables(this, kCShadedParticlesVtable, kCShadedParticlesNodeVtable);
    CParticleSystem::DestroyBody();
}

// 0x0049B630
CShadedParticles* CShadedParticles::Destroy(unsigned int flags) { return DestroyAndFree(this, flags); }

// --- smooth ---

// 0x0049B910
CSmoothParticles* CSmoothParticles::Construct(uint32_t name, int maxParticles, uint32_t, uint32_t, int, int, float,
                                              int)
{
    CParticleSystem::Construct();
    SetVtables(this, kCSmoothParticlesVtable, kCSmoothParticlesNodeVtable);
    if (ConstructParts(this, name, maxParticles, static_cast<unsigned int>(maxParticles) * kPosition, false, 2)) {
        m_colourMid = -1.0f;
        m_flags |= 4u;
    }
    return this;
}

// 0x0049B8A0
void CSmoothParticles::DestroyBody()
{
    SetVtables(this, kCSmoothParticlesVtable, kCSmoothParticlesNodeVtable);
    CParticleSystem::DestroyBody();
}

// 0x0049B8E0
CSmoothParticles* CSmoothParticles::Destroy(unsigned int flags) { return DestroyAndFree(this, flags); }

// --- smoothribbon ---

// 0x0049BBD0
CSmoothRibbonParticles* CSmoothRibbonParticles::Construct(uint32_t name, int maxParticles, uint32_t, uint32_t, int,
                                                          int, float, int history)
{
    CParticleSystem::Construct();
    SetVtables(this, kCSmoothRibbonParticlesVtable, kCSmoothRibbonParticlesNodeVtable);
    if (ConstructParts(this, name, maxParticles, static_cast<unsigned int>(maxParticles * (history + 1)) * kPosition,
                       true, 4)) {
        m_steps = history + 1;
        m_colourMid = -1.0f;
        m_flags |= 4u;
    }
    return this;
}

// 0x0049BB50
void CSmoothRibbonParticles::DestroyBody()
{
    SetVtables(this, kCSmoothRibbonParticlesVtable, kCSmoothRibbonParticlesNodeVtable);
    CParticleSystem::DestroyBody();
}

// 0x0049BBA0
CSmoothRibbonParticles* CSmoothRibbonParticles::Destroy(unsigned int flags) { return DestroyAndFree(this, flags); }

// --- smoothstar ---

// 0x0049BE80
CSmoothStarParticles* CSmoothStarParticles::Construct(uint32_t name, int maxParticles, uint32_t, uint32_t, int,
                                                      int segments, float size, int)
{
    CParticleSystem::Construct();
    SetVtables(this, kCSmoothStarParticlesVtable, kCSmoothStarParticlesNodeVtable);
    if (ConstructParts(this, name, maxParticles, static_cast<unsigned int>(maxParticles) * kPosition, false, 3)) {
        m_segments = segments;
        m_colourMid = -1.0f;
        m_size = size;
        m_flags |= 4u;
    }
    return this;
}

// 0x0049BE20
void CSmoothStarParticles::DestroyBody()
{
    SetVtables(this, kCSmoothStarParticlesVtable, kCSmoothStarParticlesNodeVtable);
    CParticleSystem::DestroyBody();
}

// 0x0049BE50
CSmoothStarParticles* CSmoothStarParticles::Destroy(unsigned int flags) { return DestroyAndFree(this, flags); }

// --- star ---

// 0x0049C140
CStarParticles* CStarParticles::Construct(uint32_t name, int maxParticles, uint32_t, uint32_t, int, int segments,
                                          float size, int)
{
    CParticleSystem::Construct();
    SetVtables(this, kCStarParticlesVtable, kCStarParticlesNodeVtable);
    if (ConstructParts(this, name, maxParticles, static_cast<unsigned int>(maxParticles) * kPosition, false, 3)) {
        m_segments = segments;
        m_colourMid = -1.0f;
        m_size = size;
        m_flags |= 4u;
    }
    return this;
}

// 0x0049C0D0
void CStarParticles::DestroyBody()
{
    SetVtables(this, kCStarParticlesVtable, kCStarParticlesNodeVtable);
    CParticleSystem::DestroyBody();
}

// 0x0049C110
CStarParticles* CStarParticles::Destroy(unsigned int flags) { return DestroyAndFree(this, flags); }

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
        return reinterpret_cast<T*>(reinterpret_cast<uint8_t*>(this) - offsetof(Nx::CParticleSystem, m_node));
    }
    Nx::CRibbonParticles* DestroyRibbon(unsigned int flags) { return System<Nx::CRibbonParticles>()->Destroy(flags); }
    Nx::CRibbonTrailParticles* DestroyRibbonTrail(unsigned int flags)
    {
        return System<Nx::CRibbonTrailParticles>()->Destroy(flags);
    }
    Nx::CShadedParticles* DestroyShaded(unsigned int flags) { return System<Nx::CShadedParticles>()->Destroy(flags); }
    Nx::CSmoothParticles* DestroySmooth(unsigned int flags) { return System<Nx::CSmoothParticles>()->Destroy(flags); }
    Nx::CSmoothRibbonParticles* DestroySmoothRibbon(unsigned int flags)
    {
        return System<Nx::CSmoothRibbonParticles>()->Destroy(flags);
    }
    Nx::CSmoothStarParticles* DestroySmoothStar(unsigned int flags)
    {
        return System<Nx::CSmoothStarParticles>()->Destroy(flags);
    }
    Nx::CStarParticles* DestroyStar(unsigned int flags) { return System<Nx::CStarParticles>()->Destroy(flags); }
};

} // namespace


namespace Nx {
namespace {
GAME_VTABLE(kCRibbonParticlesNodeVtable, 0x006494D8, &TypeNodeAdapters::DestroyRibbon);
GAME_VTABLE(kCRibbonParticlesVtable, 0x006494DC, &Nx::CRibbonParticles::Destroy, &Nx::CWeather::Render,
            &Nx::CParticleCounts::Field134, &Nx::CParticleCounts::TwoColours);
GAME_VTABLE(kCRibbonTrailParticlesNodeVtable, 0x006494EC, &TypeNodeAdapters::DestroyRibbonTrail);
GAME_VTABLE(kCRibbonTrailParticlesVtable, 0x006494F0, &Nx::CRibbonTrailParticles::Destroy, &Nx::CWeather::Render,
            &Nx::CParticleCounts::Field134, &Nx::CParticleCounts::Field134);
GAME_VTABLE(kCShadedParticlesNodeVtable, 0x00649500, &TypeNodeAdapters::DestroyShaded);
GAME_VTABLE(kCShadedParticlesVtable, 0x00649504, &Nx::CShadedParticles::Destroy, &Nx::CWeather::Render,
            GameNet::HandleNothing, &Nx::CParticleCounts::FourColours);
GAME_VTABLE(kCSmoothParticlesNodeVtable, 0x00649514, &TypeNodeAdapters::DestroySmooth);
GAME_VTABLE(kCSmoothParticlesVtable, 0x00649518, &Nx::CSmoothParticles::Destroy, &Nx::CWeather::Render,
            GameNet::HandleNothing, &Nx::CParticleCounts::TwoColours);
GAME_VTABLE(kCSmoothRibbonParticlesNodeVtable, 0x00649528, &TypeNodeAdapters::DestroySmoothRibbon);
GAME_VTABLE(kCSmoothRibbonParticlesVtable, 0x0064952C, &Nx::CSmoothRibbonParticles::Destroy, &Nx::CWeather::Render,
            &Nx::CParticleCounts::Field134, &Nx::CParticleCounts::FourColours);
GAME_VTABLE(kCSmoothStarParticlesNodeVtable, 0x0064953C, &TypeNodeAdapters::DestroySmoothStar);
GAME_VTABLE(kCSmoothStarParticlesVtable, 0x00649540, &Nx::CSmoothStarParticles::Destroy, &Nx::CWeather::Render,
            GameNet::HandleNothing, &Nx::CParticleCounts::ThreeColours);
GAME_VTABLE(kCStarParticlesNodeVtable, 0x00649550, &TypeNodeAdapters::DestroyStar);
GAME_VTABLE(kCStarParticlesVtable, 0x00649554, &Nx::CStarParticles::Destroy, &Nx::CWeather::Render,
            GameNet::HandleNothing, &Nx::CParticleCounts::ThreeColours);
} // namespace
} // namespace Nx

REPLACE(0x0049B0D0, &Nx::CRibbonParticles::Construct);
REPLACE(0x0049B070, &Nx::CRibbonParticles::DestroyBody);
REPLACE(0x0049B0A0, &Nx::CRibbonParticles::Destroy);
REPLACE(0x0049B090, &TypeNodeAdapters::DestroyRibbon);
REPLACE(0x0049B380, &Nx::CRibbonTrailParticles::Construct);
REPLACE(0x0049B320, &Nx::CRibbonTrailParticles::DestroyBody);
REPLACE(0x0049B350, &Nx::CRibbonTrailParticles::Destroy);
REPLACE(0x0049B340, &TypeNodeAdapters::DestroyRibbonTrail);
REPLACE(0x0049B660, &Nx::CShadedParticles::Construct);
REPLACE(0x0049B600, &Nx::CShadedParticles::DestroyBody);
REPLACE(0x0049B630, &Nx::CShadedParticles::Destroy);
REPLACE(0x0049B620, &TypeNodeAdapters::DestroyShaded);
REPLACE(0x0049B910, &Nx::CSmoothParticles::Construct);
REPLACE(0x0049B8A0, &Nx::CSmoothParticles::DestroyBody);
REPLACE(0x0049B8E0, &Nx::CSmoothParticles::Destroy);
REPLACE(0x0049B8D0, &TypeNodeAdapters::DestroySmooth);
REPLACE(0x0049BBD0, &Nx::CSmoothRibbonParticles::Construct);
REPLACE(0x0049BB50, &Nx::CSmoothRibbonParticles::DestroyBody);
REPLACE(0x0049BBA0, &Nx::CSmoothRibbonParticles::Destroy);
REPLACE(0x0049BB90, &TypeNodeAdapters::DestroySmoothRibbon);
REPLACE(0x0049BE80, &Nx::CSmoothStarParticles::Construct);
REPLACE(0x0049BE20, &Nx::CSmoothStarParticles::DestroyBody);
REPLACE(0x0049BE50, &Nx::CSmoothStarParticles::Destroy);
REPLACE(0x0049BE40, &TypeNodeAdapters::DestroySmoothStar);
REPLACE(0x0049C140, &Nx::CStarParticles::Construct);
REPLACE(0x0049C0D0, &Nx::CStarParticles::DestroyBody);
REPLACE(0x0049C110, &Nx::CStarParticles::Destroy);
REPLACE(0x0049C100, &TypeNodeAdapters::DestroyStar);
REPLACE(0x0049B8C0, &Nx::CParticleCounts::TwoColours);
REPLACE(0x0049BB70, &Nx::CParticleCounts::FourColours);
REPLACE(0x0049C0F0, &Nx::CParticleCounts::ThreeColours);
REPLACE(0x0049BB80, &Nx::CParticleCounts::Field134);
