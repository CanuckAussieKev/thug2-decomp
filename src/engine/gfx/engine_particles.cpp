// The engine's particle system's own members (on the PC particle system's first 0x190 bytes): see pc_particles.h.

#include <bit>
#include <cstdint>

#include "engine/core/manager.h"
#include "engine/gfx/pc_particles.h"
#include "engine/script/cscript.h"
#include "engine/core/game_vtables.h"

namespace render {

namespace {

void** const& g_engine_vtable = vt::kEngineParticles;

uint8_t* get_bytes(PcParticleSystem* system) { return reinterpret_cast<uint8_t*>(system); }
bool& get_hidden(PcParticleSystem* system) { return reinterpret_cast<bool&>(get_bytes(system)[0x159]); }
bool held_back(PcParticleSystem* system) { return get_bytes(system)[0x15A] != 0; }

// A virtual slot of the system.
template <typename... A>
void Virtual(PcParticleSystem* system, int slot, A... args)
{
    reinterpret_cast<void(__thiscall*)(PcParticleSystem*, A...)>(system->vtable[slot])(system, args...);
}

// A float passed through the x87 (FLD / FSTP): a signalling NaN comes out quiet.
float through_x87(float value)
{
    uint32_t bits = std::bit_cast<uint32_t>(value);
    if ((bits & 0x7F800000u) == 0x7F800000u && (bits & 0x007FFFFFu))
        bits |= 0x00400000u;
    return std::bit_cast<float>(bits);
}

} // namespace

// 0x00494DF0
void PcParticleSystem::engine_destroy_body() { vtable = reinterpret_cast<void**>(g_engine_vtable); }

// 0x00495230
PcParticleSystem* PcParticleSystem::engine_destroy(unsigned int flags)
{
    vtable = reinterpret_cast<void**>(g_engine_vtable);
    if (flags & 1)
        memory::Hub::get_pull()->dealloc(this);
    return this;
}

// 0x00494E00
void PcParticleSystem::engine_render(bool advance)
{
    if (!get_hidden(this) && !held_back(this))
        Virtual(this, 4, advance);
}

// 0x00494E20
void PcParticleSystem::engine_update()
{
    if (!get_hidden(this) && !held_back(this))
        Virtual(this, 5);
}

// 0x00494E40
void PcParticleSystem::engine_release()
{
    uint32_t& scripts = *reinterpret_cast<uint32_t*>(get_bytes(this) + 0x4C);
    if (scripts) {
        script::stop_scripts_by_tag(scripts);
        scripts = 0;
    }
    Virtual(this, 2);
}

// 0x00494E70
void PcParticleSystem::conceal(bool hide)
{
    if (get_hidden(this) == hide)
        return;
    get_hidden(this) = hide;
    Virtual(this, 3, hide);
}

// 0x00494E90
void PcParticleSystem::set_rate(float value) { rate = value; }

// 0x00494EA0
void PcParticleSystem::set_lifespan(float value) { lifetime = value; }

// 0x00494EB0
void PcParticleSystem::change_extent(int which, float size) { sizes[which] = through_x87(size); }

// 0x00494EC0
void PcParticleSystem::change_colour(int which, const uint32_t* colour)
{
    *reinterpret_cast<uint32_t*>(colours[which]) = *colour;
    Virtual(this, 6);
}

// 0x00494EE0
void PcParticleSystem::set_value38(float value)
{
    *reinterpret_cast<float*>(get_bytes(this) + 0x38) = value;
    Virtual(this, 6);
}

// 0x00494EF0
void PcParticleSystem::set_colour_mid_percent(float percent)
{
    colour_mid_percent_ = percent;
    Virtual(this, 6);
}

} // namespace render
