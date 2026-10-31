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

namespace render {

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

using namespace particle_parts;

namespace {

constexpr unsigned int kPosition = 12; // three floats

// The scalar deleting destructors: the body, freed with flags & 1. Returns the system.
template <typename T>
T* destroy_and_free(T* system, unsigned int flags)
{
    system->destroy_body();
    if (flags & 1)
        memory::dealloc(system);
    return system;
}

} // namespace

// --- ribbon ---

// 0x0049B0D0
RibbonParticles* RibbonParticles::make_new(uint32_t name, int particle_cap, uint32_t, uint32_t, int, int, float,
                                           int history)
{
    EmitterInstance::make_new();
    set_vtables(this, kCRibbonParticlesVtable, kCRibbonParticlesNodeVtable);
    steps_ = history + 1;
    if (construct_parts(this, name, particle_cap, static_cast<unsigned int>(particle_cap * steps_) * kPosition, true,
                        2)) {
        colour_mid_ = -1.0f;
        flag_bits_ |= 4u;
    }
    return this;
}

// 0x0049B070
void RibbonParticles::destroy_body()
{
    set_vtables(this, kCRibbonParticlesVtable, kCRibbonParticlesNodeVtable);
    EmitterInstance::destroy_body();
}

// 0x0049B0A0
RibbonParticles* RibbonParticles::teardown(unsigned int flags) { return destroy_and_free(this, flags); }

// --- ribbontrail ---

// 0x0049B380
RibbonTrailParticles* RibbonTrailParticles::make_new(uint32_t name, int particle_cap, uint32_t, uint32_t, int, int,
                                                     float, int history)
{
    EmitterInstance::make_new();
    set_vtables(this, kCRibbonTrailParticlesVtable, kCRibbonTrailParticlesNodeVtable);
    steps_ = history + 1;
    // (the colours' count read again from steps_ for each array and each step of the loop)
    if (construct_parts(this, name, particle_cap, static_cast<unsigned int>(particle_cap * steps_) * kPosition, true,
                        steps_)) {
        colour_mid_ = -1.0f;
        flag_bits_ |= 4u;
    }
    return this;
}

// 0x0049B320
void RibbonTrailParticles::destroy_body()
{
    set_vtables(this, kCRibbonTrailParticlesVtable, kCRibbonTrailParticlesNodeVtable);
    EmitterInstance::destroy_body();
}

// 0x0049B350
RibbonTrailParticles* RibbonTrailParticles::teardown(unsigned int flags) { return destroy_and_free(this, flags); }

// --- shaded ---

// 0x0049B660
ShadedParticles* ShadedParticles::make_new(uint32_t name, int particle_cap, uint32_t, uint32_t, int, int, float, int)
{
    EmitterInstance::make_new();
    set_vtables(this, kCShadedParticlesVtable, kCShadedParticlesNodeVtable);
    if (construct_parts(this, name, particle_cap, static_cast<unsigned int>(particle_cap) * kPosition, false, 4)) {
        colour_mid_ = -1.0f;
        flag_bits_ |= 4u;
    }
    return this;
}

// 0x0049B600
void ShadedParticles::destroy_body()
{
    set_vtables(this, kCShadedParticlesVtable, kCShadedParticlesNodeVtable);
    EmitterInstance::destroy_body();
}

// 0x0049B630
ShadedParticles* ShadedParticles::teardown(unsigned int flags) { return destroy_and_free(this, flags); }

// --- smooth ---

// 0x0049B910
SmoothParticles* SmoothParticles::make_new(uint32_t name, int particle_cap, uint32_t, uint32_t, int, int, float, int)
{
    EmitterInstance::make_new();
    set_vtables(this, kCSmoothParticlesVtable, kCSmoothParticlesNodeVtable);
    if (construct_parts(this, name, particle_cap, static_cast<unsigned int>(particle_cap) * kPosition, false, 2)) {
        colour_mid_ = -1.0f;
        flag_bits_ |= 4u;
    }
    return this;
}

// 0x0049B8A0
void SmoothParticles::destroy_body()
{
    set_vtables(this, kCSmoothParticlesVtable, kCSmoothParticlesNodeVtable);
    EmitterInstance::destroy_body();
}

// 0x0049B8E0
SmoothParticles* SmoothParticles::teardown(unsigned int flags) { return destroy_and_free(this, flags); }

// --- smoothribbon ---

// 0x0049BBD0
SmoothRibbonParticles* SmoothRibbonParticles::make_new(uint32_t name, int particle_cap, uint32_t, uint32_t, int, int,
                                                       float, int history)
{
    EmitterInstance::make_new();
    set_vtables(this, kCSmoothRibbonParticlesVtable, kCSmoothRibbonParticlesNodeVtable);
    if (construct_parts(this, name, particle_cap, static_cast<unsigned int>(particle_cap * (history + 1)) * kPosition,
                        true, 4)) {
        steps_ = history + 1;
        colour_mid_ = -1.0f;
        flag_bits_ |= 4u;
    }
    return this;
}

// 0x0049BB50
void SmoothRibbonParticles::destroy_body()
{
    set_vtables(this, kCSmoothRibbonParticlesVtable, kCSmoothRibbonParticlesNodeVtable);
    EmitterInstance::destroy_body();
}

// 0x0049BBA0
SmoothRibbonParticles* SmoothRibbonParticles::teardown(unsigned int flags) { return destroy_and_free(this, flags); }

// --- smoothstar ---

// 0x0049BE80
SmoothStarParticles* SmoothStarParticles::make_new(uint32_t name, int particle_cap, uint32_t, uint32_t, int,
                                                   int segments, float size, int)
{
    EmitterInstance::make_new();
    set_vtables(this, kCSmoothStarParticlesVtable, kCSmoothStarParticlesNodeVtable);
    if (construct_parts(this, name, particle_cap, static_cast<unsigned int>(particle_cap) * kPosition, false, 3)) {
        segments_ = segments;
        colour_mid_ = -1.0f;
        extent_ = size;
        flag_bits_ |= 4u;
    }
    return this;
}

// 0x0049BE20
void SmoothStarParticles::destroy_body()
{
    set_vtables(this, kCSmoothStarParticlesVtable, kCSmoothStarParticlesNodeVtable);
    EmitterInstance::destroy_body();
}

// 0x0049BE50
SmoothStarParticles* SmoothStarParticles::teardown(unsigned int flags) { return destroy_and_free(this, flags); }

// --- star ---

// 0x0049C140
StarParticles* StarParticles::make_new(uint32_t name, int particle_cap, uint32_t, uint32_t, int, int segments,
                                       float size, int)
{
    EmitterInstance::make_new();
    set_vtables(this, kCStarParticlesVtable, kCStarParticlesNodeVtable);
    if (construct_parts(this, name, particle_cap, static_cast<unsigned int>(particle_cap) * kPosition, false, 3)) {
        segments_ = segments;
        colour_mid_ = -1.0f;
        extent_ = size;
        flag_bits_ |= 4u;
    }
    return this;
}

// 0x0049C0D0
void StarParticles::destroy_body()
{
    set_vtables(this, kCStarParticlesVtable, kCStarParticlesNodeVtable);
    EmitterInstance::destroy_body();
}

// 0x0049C110
StarParticles* StarParticles::teardown(unsigned int flags) { return destroy_and_free(this, flags); }

// --- the counts ---

// 0x0049B8C0 / 0x0049BB70 / 0x0049C0F0 / 0x0049BB80
int ParticleCounts::two_colours() { return 2; }
int ParticleCounts::four_colours() { return 4; }
int ParticleCounts::three_colours() { return 3; }
int ParticleCounts::field134() { return *reinterpret_cast<const int*>(reinterpret_cast<const uint8_t*>(this) + 0x134); }

} // namespace render

namespace {

// The destructors reached through the node's vtable (`this` the node at +0x0C): each a jump to the type's.
struct TypeNodeAdapters {
    template <typename T>
    T* get_system()
    {
        return reinterpret_cast<T*>(reinterpret_cast<uint8_t*>(this) - offsetof(render::EmitterInstance, list_link_));
    }
    render::RibbonParticles* destroy_ribbon(unsigned int flags)
    {
        return get_system<render::RibbonParticles>()->teardown(flags);
    }
    render::RibbonTrailParticles* destroy_ribbon_trail(unsigned int flags)
    {
        return get_system<render::RibbonTrailParticles>()->teardown(flags);
    }
    render::ShadedParticles* destroy_shaded(unsigned int flags)
    {
        return get_system<render::ShadedParticles>()->teardown(flags);
    }
    render::SmoothParticles* destroy_smooth(unsigned int flags)
    {
        return get_system<render::SmoothParticles>()->teardown(flags);
    }
    render::SmoothRibbonParticles* destroy_smooth_ribbon(unsigned int flags)
    {
        return get_system<render::SmoothRibbonParticles>()->teardown(flags);
    }
    render::SmoothStarParticles* destroy_smooth_star(unsigned int flags)
    {
        return get_system<render::SmoothStarParticles>()->teardown(flags);
    }
    render::StarParticles* destroy_star(unsigned int flags)
    {
        return get_system<render::StarParticles>()->teardown(flags);
    }
};

} // namespace

namespace render {
namespace {
static VtableSlot g_c_ribbon_particles_node_vtable_slots[] = {&TypeNodeAdapters::destroy_ribbon};
void** const kCRibbonParticlesNodeVtable = reinterpret_cast<void**>(g_c_ribbon_particles_node_vtable_slots);
static VtableSlot g_c_ribbon_particles_vtable_slots[] = {
    &render::RibbonParticles::teardown, &render::WeatherSystem::render, &render::ParticleCounts::field134,
    &render::ParticleCounts::two_colours};
void** const kCRibbonParticlesVtable = reinterpret_cast<void**>(g_c_ribbon_particles_vtable_slots);
static VtableSlot g_c_ribbon_trail_particles_node_vtable_slots[] = {&TypeNodeAdapters::destroy_ribbon_trail};
void** const kCRibbonTrailParticlesNodeVtable = reinterpret_cast<void**>(g_c_ribbon_trail_particles_node_vtable_slots);
static VtableSlot g_c_ribbon_trail_particles_vtable_slots[] = {
    &render::RibbonTrailParticles::teardown, &render::WeatherSystem::render, &render::ParticleCounts::field134,
    &render::ParticleCounts::field134};
void** const kCRibbonTrailParticlesVtable = reinterpret_cast<void**>(g_c_ribbon_trail_particles_vtable_slots);
static VtableSlot g_c_shaded_particles_node_vtable_slots[] = {&TypeNodeAdapters::destroy_shaded};
void** const kCShadedParticlesNodeVtable = reinterpret_cast<void**>(g_c_shaded_particles_node_vtable_slots);
static VtableSlot g_c_shaded_particles_vtable_slots[] = {&render::ShadedParticles::teardown,
                                                         &render::WeatherSystem::render, online::handle_nothing,
                                                         &render::ParticleCounts::four_colours};
void** const kCShadedParticlesVtable = reinterpret_cast<void**>(g_c_shaded_particles_vtable_slots);
static VtableSlot g_c_smooth_particles_node_vtable_slots[] = {&TypeNodeAdapters::destroy_smooth};
void** const kCSmoothParticlesNodeVtable = reinterpret_cast<void**>(g_c_smooth_particles_node_vtable_slots);
static VtableSlot g_c_smooth_particles_vtable_slots[] = {&render::SmoothParticles::teardown,
                                                         &render::WeatherSystem::render, online::handle_nothing,
                                                         &render::ParticleCounts::two_colours};
void** const kCSmoothParticlesVtable = reinterpret_cast<void**>(g_c_smooth_particles_vtable_slots);
static VtableSlot g_c_smooth_ribbon_particles_node_vtable_slots[] = {&TypeNodeAdapters::destroy_smooth_ribbon};
void** const kCSmoothRibbonParticlesNodeVtable =
    reinterpret_cast<void**>(g_c_smooth_ribbon_particles_node_vtable_slots);
static VtableSlot g_c_smooth_ribbon_particles_vtable_slots[] = {
    &render::SmoothRibbonParticles::teardown, &render::WeatherSystem::render, &render::ParticleCounts::field134,
    &render::ParticleCounts::four_colours};
void** const kCSmoothRibbonParticlesVtable = reinterpret_cast<void**>(g_c_smooth_ribbon_particles_vtable_slots);
static VtableSlot g_c_smooth_star_particles_node_vtable_slots[] = {&TypeNodeAdapters::destroy_smooth_star};
void** const kCSmoothStarParticlesNodeVtable = reinterpret_cast<void**>(g_c_smooth_star_particles_node_vtable_slots);
static VtableSlot g_c_smooth_star_particles_vtable_slots[] = {&render::SmoothStarParticles::teardown,
                                                              &render::WeatherSystem::render, online::handle_nothing,
                                                              &render::ParticleCounts::three_colours};
void** const kCSmoothStarParticlesVtable = reinterpret_cast<void**>(g_c_smooth_star_particles_vtable_slots);
static VtableSlot g_c_star_particles_node_vtable_slots[] = {&TypeNodeAdapters::destroy_star};
void** const kCStarParticlesNodeVtable = reinterpret_cast<void**>(g_c_star_particles_node_vtable_slots);
static VtableSlot g_c_star_particles_vtable_slots[] = {&render::StarParticles::teardown, &render::WeatherSystem::render,
                                                       online::handle_nothing, &render::ParticleCounts::three_colours};
void** const kCStarParticlesVtable = reinterpret_cast<void**>(g_c_star_particles_vtable_slots);
} // namespace
} // namespace render
