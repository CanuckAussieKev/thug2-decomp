// The engine's particle system's own members (on the PC particle system's first 0x190 bytes): see pc_particles.h.

#include <bit>
#include <cstdint>

#include "game/core/manager.h"
#include "game/gfx/pc_particles.h"
#include "game/script/cscript.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Nx {

namespace {

constexpr uint32_t kEngineVtable = 0x00649380;

uint8_t* Bytes(CPcParticleSystem* system) { return reinterpret_cast<uint8_t*>(system); }
bool& Hidden(CPcParticleSystem* system) { return reinterpret_cast<bool&>(Bytes(system)[0x159]); }
bool HeldBack(CPcParticleSystem* system) { return Bytes(system)[0x15A] != 0; }

// A virtual slot of the system.
template <typename... A>
void Virtual(CPcParticleSystem* system, int slot, A... args)
{
    reinterpret_cast<void(__thiscall*)(CPcParticleSystem*, A...)>(system->vtable[slot])(system, args...);
}

// A float passed through the x87 (FLD / FSTP): a signalling NaN comes out quiet.
float ThroughX87(float value)
{
    uint32_t bits = std::bit_cast<uint32_t>(value);
    if ((bits & 0x7F800000u) == 0x7F800000u && (bits & 0x007FFFFFu))
        bits |= 0x00400000u;
    return std::bit_cast<float>(bits);
}

} // namespace

// 0x00494DF0
void CPcParticleSystem::EngineDestroyBody() { vtable = reinterpret_cast<void**>(kEngineVtable); }

// 0x00495230
CPcParticleSystem* CPcParticleSystem::EngineDestroy(unsigned int flags)
{
    vtable = reinterpret_cast<void**>(kEngineVtable);
    if (flags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x00494E00
void CPcParticleSystem::EngineRender(bool advance)
{
    if (!Hidden(this) && !HeldBack(this))
        Virtual(this, 4, advance);
}

// 0x00494E20
void CPcParticleSystem::EngineUpdate()
{
    if (!Hidden(this) && !HeldBack(this))
        Virtual(this, 5);
}

// 0x00494E40
void CPcParticleSystem::EngineRelease()
{
    uint32_t& scripts = *reinterpret_cast<uint32_t*>(Bytes(this) + 0x4C);
    if (scripts) {
        Script::KillSpawnedScriptsWithId(scripts);
        scripts = 0;
    }
    Virtual(this, 2);
}

// 0x00494E70
void CPcParticleSystem::Hide(bool hide)
{
    if (Hidden(this) == hide)
        return;
    Hidden(this) = hide;
    Virtual(this, 3, hide);
}

// 0x00494E90
void CPcParticleSystem::SetRate(float value) { rate = value; }

// 0x00494EA0
void CPcParticleSystem::SetLifetime(float value) { lifetime = value; }

// 0x00494EB0
void CPcParticleSystem::SetSize(int which, float size) { sizes[which] = ThroughX87(size); }

// 0x00494EC0
void CPcParticleSystem::SetColour(int which, const uint32_t* colour)
{
    *reinterpret_cast<uint32_t*>(colours[which]) = *colour;
    Virtual(this, 6);
}

// 0x00494EE0
void CPcParticleSystem::SetValue38(float value)
{
    *reinterpret_cast<float*>(Bytes(this) + 0x38) = value;
    Virtual(this, 6);
}

// 0x00494EF0
void CPcParticleSystem::SetColourMidPercent(float percent)
{
    colourMidPercent = percent;
    Virtual(this, 6);
}

} // namespace Nx

REPLACE(0x00494DF0, &Nx::CPcParticleSystem::EngineDestroyBody);
REPLACE(0x00495230, &Nx::CPcParticleSystem::EngineDestroy);
REPLACE(0x00494E00, &Nx::CPcParticleSystem::EngineRender);
REPLACE(0x00494E20, &Nx::CPcParticleSystem::EngineUpdate);
REPLACE(0x00494E40, &Nx::CPcParticleSystem::EngineRelease);
REPLACE(0x00494E70, &Nx::CPcParticleSystem::Hide);
REPLACE(0x00494E90, &Nx::CPcParticleSystem::SetRate);
REPLACE(0x00494EA0, &Nx::CPcParticleSystem::SetLifetime);
REPLACE(0x00494EB0, &Nx::CPcParticleSystem::SetSize);
REPLACE(0x00494EC0, &Nx::CPcParticleSystem::SetColour);
REPLACE(0x00494EE0, &Nx::CPcParticleSystem::SetValue38);
REPLACE(0x00494EF0, &Nx::CPcParticleSystem::SetColourMidPercent);
