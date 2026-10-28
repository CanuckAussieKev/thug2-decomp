// The engine's effects: see engine_effects.h. The maths follows the original's x87 code (53-bit precision: double
// where it keeps values in registers, float where it stores them).

#include "game/gfx/engine_effects.h"

#include <bit>
#include <cstring>
#include <new>

#include "game/core/crc.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/gfx/flash.h"
#include "game/gfx/pc_fog.h"
#include "game/gfx/shatter.h"
#include "game/gfx/spawned_polys.h"
#include "game/gfx/texture_splats.h"
#include "game/lst/hashtable.h"
#include "game/mth/geometry.h"
#include "game/mth/matrix.h"
#include "game/nx/coll_grid.h"
#include "game/nx/coll_obj.h"
#include "game/script/struct.h"
#include "game/sys/timer.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Nx {

namespace {

constexpr uint32_t kShatterVtable = 0x00649308;

// The tables.
Lst::HashTable*& Flashes() { return GameVariable<Lst::HashTable*>(0x007004A8); }
Lst::HashTable*& SpawnedPolys() { return GameVariable<Lst::HashTable*>(0x007004AC); }
Lst::HashTable*& SplatSets() { return GameVariable<Lst::HashTable*>(0x007004B0); }
Lst::HashTable*& RecentSplats() { return GameVariable<Lst::HashTable*>(0x007004B4); }
Lst::HashTable*& Shatters() { return GameVariable<Lst::HashTable*>(0x007004B8); }

// A table's first value and the next (null at the end) - the original's inlined iteration (IterateNext).
void* NextValue(Lst::HashTable* table)
{
    if (!table->IterateNext() || !table->m_iterItem)
        return nullptr;
    return table->m_iterItem->value;
}
void* FirstValue(Lst::HashTable* table)
{
    table->m_iterBucket = -1;
    table->m_iterItem = nullptr;
    return NextValue(table);
}
// A value taken out of its table (keyed by its address).
void Remove(Lst::HashTable* table, void* value)
{
    uint32_t key = reinterpret_cast<uint32_t>(value);
    table->FlushItem(key);
}

bool Paused() { return GameVariable<bool>(0x006FA7D4); }

// The game's C runtime's rand() and its scales (0x0064930C: 2 / 32767, 0x00646510: 1 / 32767).
int Rand() { return GameFunction<int(__cdecl*)()>(0x00623BA3)(); }
constexpr double kPerRandTwice = std::bit_cast<float>(0x38800100u);
constexpr double kPerRand = std::bit_cast<float>(0x38000100u);

// The frame's time in milliseconds (truncated, the original's _ftol).
int FrameMilliseconds() { return static_cast<int>(static_cast<int64_t>(Tmr::GetSlomoFrameSeconds() * 1000.0f)); }

// The platform's fog exponent: compiled out on PC (the empty 0x005480A0).
void PlatSetFogExponent(float /*exponent*/) {}
// Two compiled-out calls (the empty 0x005480A0) after the tables are made.
void CompiledOut() {}

// A new table of 16 buckets on the default heap (none when out of memory).
Lst::HashTable* NewTable()
{
    void* memory = Mem::New(sizeof(Lst::HashTable));
    return memory ? new (memory) Lst::HashTable(4) : nullptr;
}

} // namespace

// 0x00490D80
void sVertexStack::Pop(uint8_t* out)
{
    used -= vertexSize;
    std::memcpy(out, data + used, vertexSize);
}

// 0x00490E40
sSplatParams* sSplatParams::Construct()
{
    start[0] = start[1] = start[2] = 0.0f;
    start[3] = 1.0f;
    end[0] = end[1] = end[2] = 0.0f;
    end[3] = 1.0f;
    size = 0.0f;
    depth = 0.0f;
    angle = 0.0f;
    coneAngle = 0.0f;
    lifetime = 0.0f;
    texture = nullptr;
    unknown38 = 0x54628ED7;
    up = nullptr;
    unknown40 = 0;
    return this;
}

// --- the engine's shatter ---

// 0x004912C0
CShatter* CShatter::Construct(int count)
{
    vtable = reinterpret_cast<void**>(kShatterVtable);
    centres = static_cast<Mth::Vector*>(Mem::New(count << 4));
    velocities = static_cast<Mth::Vector*>(Mem::New(count << 4));
    rotations = static_cast<Mth::Matrix*>(Mem::New(count << 6));
    triangleCount = static_cast<uint16_t>(count);
    unknown28 = GameVariable<uint32_t>(0x007004C0);
    gravity = 128.0f;
    lifetime = GameVariable<float>(0x0067DDF8);
    floor = GameVariable<float>(0x0067DDFC);
    bounce = GameVariable<float>(0x0067DE00);
    return this;
}

// 0x00490E80
void CShatter::DestroyBody()
{
    vtable = reinterpret_cast<void**>(kShatterVtable);
    Mem::Manager::Get()->Free(centres);
    Mem::Manager::Get()->Free(velocities);
    Mem::Manager::Get()->Free(rotations);
}

// 0x00491380
CShatter* CShatter::Destroy(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x004913D0
void CShatter::Step(int index, float step)
{
    Mth::Vector& velocity = velocities[index];
    Mth::Vector& centre = centres[index];
    double moveX = static_cast<double>(step) * velocity.x;
    double moveY = static_cast<double>(step) * velocity.y;
    float moveZ = static_cast<float>(static_cast<double>(step) * velocity.z);
    float moveW = static_cast<float>(static_cast<double>(step) * velocity.w);
    centre.x = static_cast<float>(moveX + centre.x);
    centre.y = static_cast<float>(moveY + centre.y);
    centre.z = static_cast<float>(static_cast<double>(moveZ) + centre.z);
    centre.w = static_cast<float>(static_cast<double>(moveW) + centre.w);
    if (centre.y < floor && velocity.y < 0.0f) {
        // A bounce: the height mirrored about the floor, the speed turned up, a new turn.
        centre.y = static_cast<float>((static_cast<double>(floor) - centre.y) + floor);
        velocity.y = static_cast<float>(-(static_cast<double>(bounce) * velocity.y));
        float a1 = static_cast<float>(Rand() * kPerRandTwice - 1.0f);
        float a2 = static_cast<float>(Rand() * kPerRandTwice - 1.0f);
        float a3 = static_cast<float>(Rand() * kPerRandTwice - 1.0f);
        Mth::Vector axis = {a3, a2, a1, 1.0f};
        axis.Normalize();
        Mth::Matrix& rotation = rotations[index];
        std::memset(&rotation, 0, sizeof(rotation));
        rotation.row[0].x = rotation.row[1].y = rotation.row[2].z = rotation.row[3].w = 1.0f;
        float turnAngle = static_cast<float>(Rand() * kPerRand * 0.1f); // (up to 0.1, 0x00645D74)
        Mth::Matrix turn;
        Mth::CreateRotateMatrix(turn, axis, turnAngle);
        // D3DX (linked library code): rotation = rotation * turn
        GameFunction<void*(__stdcall*)(Mth::Matrix*, const Mth::Matrix*, const Mth::Matrix*)>(0x005BFA8C)(
            &rotation, &rotation, &turn);
    }
    velocity.y = static_cast<float>(velocity.y - static_cast<double>(step) * gravity);
}

namespace Engine {

// --- settings ---

// 0x00490DE0
void __cdecl EnableFog(bool on)
{
    GameVariable<bool>(0x007004C8) = on;
    Nx::SetFogEnabled(on);
}

// 0x00490E00
void __cdecl SetFogDistance(float distance)
{
    GameVariable<float>(0x0067DDE8) = distance;
    Nx::SetFogDistance(distance);
}

// 0x00490E10
void __cdecl SetFogExponent(float exponent)
{
    GameVariable<float>(0x0067DDEC) = exponent;
    PlatSetFogExponent(exponent);
}

// 0x00490E20
void __cdecl SetFogColor(uint32_t rgba)
{
    GameVariable<uint32_t>(0x0067DE04) = rgba;
    Nx::SetFogColor(rgba);
}

// 0x00491290
void __cdecl SetLevelColor(const float* color) { std::memcpy(&GameVariable<float>(0x007004F0), color, 16); }

// 0x00491110
void __cdecl SetShatterParams(const Mth::Vector* velocity, const Mth::Vector* vector210E, float valueC483, float area,
                              float speed, float velocityScale, float lifetime, float floor, float bounce,
                              float value2E1C, uint32_t texture, uint32_t valueAF1A, int value33DF)
{
    GameVariable<Mth::Vector>(0x007004D0) = *velocity;
    GameVariable<Mth::Vector>(0x007004E0) = *vector210E;
    GameVariable<float>(0x0067DDF0) = area == 0.0f ? 82944.0f : static_cast<float>(static_cast<double>(area) * area);
    GameVariable<float>(0x007004CC) = speed == 0.0f ? 0.0f : speed;
    GameVariable<float>(0x0067DDF4) = velocityScale == 0.0f ? 1.0f : velocityScale;
    GameVariable<float>(0x0067DDF8) = lifetime == 0.0f ? 4.0f : lifetime;
    GameVariable<float>(0x0067DDFC) = floor == 0.0f ? -10000.0f : floor;
    GameVariable<float>(0x0067DE00) = bounce == 0.0f ? 0.8f : bounce;
    GameVariable<float>(0x0067DDD8) = value2E1C;
    GameVariable<float>(0x0067DDDC) = static_cast<float>(static_cast<double>(valueC483) * valueC483);
    GameVariable<uint32_t>(0x007004BC) = texture;
    GameVariable<uint32_t>(0x007004C0) = valueAF1A;
    GameVariable<int>(0x007004C4) = value33DF;
}

// 0x00490DC0
void __cdecl ShatterGeom(void* geom)
{
    void* textures = GameVariable<void*>(0x00700F20); // (the particles' dictionary)
    void* texture = GameFunction<void*(__thiscall*)(void*, uint32_t)>(0x0049F540)(textures,
                                                                                  GameVariable<uint32_t>(0x007004BC));
    // (the PC's ShatterGeom - shatter.cpp - only reads the geom)
    GameFunction<void(__cdecl*)(void*, void*)>(0x004BA7A0)(geom, texture);
}

// --- texture splats ---

// 0x00490EC0
void __cdecl AgeTextureSplats(sTextureSplats* splats, int elapsed)
{
    int living = 0;
    splats->last = -1;
    for (int i = 0; i < 0x100; ++i) {
        int left = static_cast<int>(splats->alive[i]);
        if (left < 0)
            continue;
        left -= elapsed;
        splats->alive[i] = static_cast<uint32_t>(left);
        if (left <= 0) {
            splats->alive[i] = 0;
            CollapseSplatTriangle(splats, i);
        } else {
            splats->last = i;
            ++living;
        }
    }
    if (living < 0x100 - GameVariable<int>(0x0067DDE0))
        return;
    // Nearly full: the triangle furthest through its life (of those with a time and a lifetime; the first one's
    // difference the bar to reach, whatever it is - the last of equals wins).
    int furthest = -1;
    int best = splats->correctionSteps[0] - static_cast<int>(splats->alive[0]);
    for (int i = 0; i < 0x100; ++i) {
        if (splats->correctionSteps[i] != 0 && splats->alive[i] != 0) {
            int done = splats->correctionSteps[i] - static_cast<int>(splats->alive[i]);
            if (done >= best) {
                best = done;
                furthest = i;
            }
        }
    }
    if (furthest < 0)
        return;
    // Every triangle with the same time and lifetime (as that one has now) fades: its base colours' and its
    // vertices' alphas; once all six are 0 it's dead.
    for (int i = 0; i < 0x100; ++i) {
        if (splats->alive[i] != splats->alive[furthest] ||
            splats->correctionSteps[i] != splats->correctionSteps[furthest])
            continue;
        int fade = GameVariable<int>(0x0067DDE4);
        uint8_t* alphas[6] = {reinterpret_cast<uint8_t*>(&splats->baseColors[i][0]) + 3,
                              reinterpret_cast<uint8_t*>(&splats->baseColors[i][1]) + 3,
                              reinterpret_cast<uint8_t*>(&splats->baseColors[i][2]) + 3,
                              splats->vertices[i][0] + 0xF, splats->vertices[i][1] + 0xF, splats->vertices[i][2] + 0xF};
        int gone = 0;
        for (uint8_t* alpha : alphas) {
            if (*alpha == 0) {
                ++gone;
            } else {
                int faded = *alpha - fade;
                *alpha = static_cast<uint8_t>(faded <= 0 ? 0 : faded);
            }
        }
        if (gone == 6)
            splats->alive[i] = 0;
    }
}

// The register adapter: the elapsed time in EAX, the set on the stack (the caller pops it).
__declspec(naked) void AgeTextureSplatsStub()
{
    __asm {
        push ecx
        push edx
        push eax
        push dword ptr [esp + 16]
        call AgeTextureSplats
        add esp, 8
        pop edx
        pop ecx
        ret
    }
}

// 0x00491F70
void __cdecl AgeAllTextureSplats()
{
    if (Paused() || !SplatSets())
        return;
    int elapsed = FrameMilliseconds();
    for (void* splats = FirstValue(SplatSets()); splats; splats = NextValue(SplatSets()))
        AgeTextureSplats(static_cast<sTextureSplats*>(splats), elapsed);
}

// 0x00491F10
void __cdecl LightAllTextureSplats()
{
    for (void* splats = FirstValue(SplatSets()); splats; splats = NextValue(SplatSets()))
        LightTextureSplats(static_cast<sTextureSplats*>(splats));
}

// 0x004915A0
void __cdecl KillAllTextureSplats()
{
    for (void* recent = FirstValue(RecentSplats()); recent;) {
        void* next = NextValue(RecentSplats());
        Remove(RecentSplats(), recent);
        Mem::Manager::Get()->Free(recent);
        recent = next;
    }
    Nx::KillAllTextureSplats();
}

// 0x00491630
void __cdecl KillTextureSplatsInBox(const float* box)
{
    for (void* value = FirstValue(RecentSplats()); value; value = NextValue(RecentSplats())) {
        auto* recent = static_cast<float*>(value); // (a position, then +0x14 its time)
        if (recent[0] >= box[0] && recent[1] >= box[1] && recent[2] >= box[2] && recent[0] <= box[4] &&
            recent[1] <= box[5] && recent[2] <= box[6])
            recent[5] = 0.0f;
    }
    Nx::KillTextureSplatsInBox(box);
}

// 0x004916F0
void __cdecl KillTextureSplatsOfTexture(uint32_t texture)
{
    for (void* value = FirstValue(SplatSets()); value;) {
        void* next = NextValue(SplatSets());
        if (static_cast<sTextureSplats*>(value)->texture == texture) {
            Remove(SplatSets(), value);
            Mem::Manager::Get()->Free(value);
        }
        value = next;
    }
}

// A recent splat position (0x18 bytes, in 0x007004B4): where, the id it follows, when (Tmr::GetTime).
struct RecentSplat {
    Mth::Vector pos;
    uint32_t follow; // +0x10
    uint32_t time;   // +0x14
};

// 0x004928A0
bool __cdecl CreateTextureSplatAt(const Mth::Vector* from, const Mth::Vector* to, float size, float depth, float angle,
                                  float coneAngle, float lifetime, const char* textureName, uint32_t unknown38,
                                  uint32_t follow, uint8_t unknown40, uint8_t* texture)
{
    GameVariable<int>(0x0067DDE0) = Script::GetInteger(0x26FCB7BE); // (asserted)
    RecentSplat* recent = nullptr;
    if (follow) {
        // A recent position of the same id near enough: the splat is made from it. Old ones go.
        uint32_t now = Tmr::GetTime();
        for (void* value = FirstValue(RecentSplats()); value && !recent;) {
            auto* candidate = static_cast<RecentSplat*>(value);
            RecentSplat* dead = nullptr;
            if (now < candidate->time || now - candidate->time > 500) {
                dead = candidate;
            } else if (candidate->follow == follow) {
                double dx = static_cast<double>(from->x) - candidate->pos.x;
                double dy = static_cast<double>(from->y) - candidate->pos.y;
                double dz = static_cast<double>(from->z) - candidate->pos.z;
                if (dz * dz + dy * dy + dx * dx < 4356.0f) {
                    recent = candidate;
                    break;
                }
            }
            value = NextValue(RecentSplats());
            if (dead) {
                Remove(RecentSplats(), dead);
                Mem::Manager::Get()->Free(dead);
            }
        }
        if (!recent) {
            // None: only remembered.
            auto* added = static_cast<RecentSplat*>(Mem::New(sizeof(RecentSplat)));
            added->follow = follow;
            added->pos = *from;
            added->time = now;
            uint32_t key = reinterpret_cast<uint32_t>(added);
            RecentSplats()->PutItem(key, added);
            return true;
        }
    }
    if (!texture) {
        void* textures = GameVariable<void*>(0x00700F20); // (the particles' dictionary)
        texture = GameFunction<uint8_t*(__thiscall*)(void*, uint32_t)>(0x0049F540)(
            textures, Crc::GenerateCRCFromString(textureName));
    }
    // The line's box grown by the size; the collision objects of the grids along the line that meet it.
    Mth::Line line(*from, *to);
    Mth::BBox lineBox(line.start);
    lineBox.AddPoint(line.end);
    Mth::BBox box;
    box.min = {static_cast<float>(static_cast<double>(lineBox.min.x) - size),
                 static_cast<float>(static_cast<double>(lineBox.min.y) - size),
                 static_cast<float>(static_cast<double>(lineBox.min.z) - size), 1.0f};
    box.max = {static_cast<float>(static_cast<double>(lineBox.max.x) + size),
                 static_cast<float>(static_cast<double>(lineBox.max.y) + size),
                 static_cast<float>(static_cast<double>(lineBox.max.z) + size), 1.0f};
    auto** grids = GameFunction<CCollGrid**(__cdecl*)(Mth::Line*)>(0x0048C980)(&line);
    if (!grids || !*grids)
        return false;
    auto* sectors = &GameVariable<void*>(0x00700510);
    auto* objects = &GameVariable<CCollObj*>(0x00700710);
    int count = 0;
    for (CCollGrid** grid = grids; *grid; ++grid) {
        CCollObj** inBox = (*grid)->ObjectsInBox(&box);
        if (!inBox)
            continue;
        for (; *inBox; ++inBox) {
            CCollObj* object = *inBox;
            // (slot 21: its geometry, a box first; NaN doesn't rule it out)
            auto* bounds = reinterpret_cast<const Mth::BBox*(__thiscall*)(CCollObj*)>(
                (*reinterpret_cast<void***>(object))[21])(object);
            if (!(bounds->min.x > box.max.x) && !(box.min.x > bounds->max.x) &&
                !(bounds->min.z > box.max.z) && !(box.min.z > bounds->max.z) &&
                !(bounds->min.y > box.max.y) && !(box.min.y > bounds->max.y)) {
                void* sector = GameFunction<void*(__cdecl*)(uint32_t)>(0x0048C510)(object->m_checksum);
                objects[count] = object;
                sectors[count] = sector;
                ++count;
            }
        }
    }
    if (count <= 0)
        return false;
    sectors[count] = nullptr;
    objects[count] = nullptr;
    bool made;
    if (follow && recent->pos.x == from->x && recent->pos.z == from->z) {
        made = false; // (it hasn't moved)
    } else {
        sSplatParams params;
        params.Construct();
        std::memcpy(params.start, from, 16);
        std::memcpy(params.end, to, 16);
        params.angle = angle;
        params.texture = texture;
        params.size = size;
        params.depth = depth;
        params.coneAngle = coneAngle;
        params.lifetime = lifetime;
        params.unknown38 = unknown38;
        params.up = recent ? &recent->pos.x : nullptr;
        params.unknown40 = unknown40;
        sTextureSplats* splats = Nx::CreateTextureSplat(sectors, reinterpret_cast<int>(objects), &params);
        made = splats != nullptr;
        if (!follow && splats) {
            AgeTextureSplats(splats, FrameMilliseconds());
            LightTextureSplats(splats);
            splats->texture = GameFunction<uint32_t(__thiscall*)(uint8_t*)>(0x005480C0)(texture);
        }
    }
    if (recent) {
        recent->pos = *from;
        recent->time = Tmr::GetTime();
    }
    return made;
}

// --- shatters ---

// 0x00492000
void __cdecl UpdateShatters()
{
    if (Paused() || !Shatters())
        return;
    float elapsed = static_cast<float>(Tmr::GetSlomoFrameSeconds());
    for (void* value = FirstValue(Shatters()); value;) {
        auto* shatter = static_cast<CShatter*>(value);
        double left = static_cast<double>(shatter->lifetime) - elapsed;
        shatter->lifetime = static_cast<float>(left);
        CShatter* dead = nullptr;
        if (left <= 0.0)
            dead = shatter;
        else
            UpdateShatter(reinterpret_cast<CPcShatter*>(shatter), elapsed);
        value = NextValue(Shatters());
        if (dead) {
            Remove(Shatters(), dead);
            reinterpret_cast<void(__thiscall*)(CShatter*, unsigned int)>(dead->vtable[0])(dead, 1);
        }
    }
}

// 0x004920E0
void __cdecl DrawShatters()
{
    if (!Shatters())
        return;
    for (void* shatter = FirstValue(Shatters()); shatter; shatter = NextValue(Shatters()))
        DrawShatter(static_cast<CPcShatter*>(shatter));
}

// --- flashes ---

// 0x004923E0
void __cdecl AddFlash(int viewport, uint32_t start, uint32_t end, float duration, float z, uint32_t flags,
                      const char* /*texture*/)
{
    auto* flash = static_cast<Flash*>(Mem::New(sizeof(Flash)));
    std::memcpy(flash->start, &start, 4);
    std::memcpy(flash->end, &end, 4);
    flash->duration = duration;
    flash->timeLeft = duration;
    flash->z = z;
    flash->flags = flags;
    flash->texture = nullptr;
    flash->viewport = viewport;
    uint32_t key = reinterpret_cast<uint32_t>(flash);
    Flashes()->PutItem(key, flash);
}

// 0x00491790
void __cdecl UpdateFlashes()
{
    float elapsed = static_cast<float>(Tmr::GetSlomoFrameSeconds());
    if (!Flashes())
        return;
    for (void* value = FirstValue(Flashes()); value;) {
        auto* flash = static_cast<Flash*>(value);
        Flash* dead = nullptr;
        if (!Paused() || (flash->flags & 8)) {
            double left = static_cast<double>(flash->timeLeft) - elapsed;
            flash->timeLeft = static_cast<float>(left);
            if (!(left > 0.0)) { // (NaN too)
                dead = flash;
            } else {
                double t = left / flash->duration;
                uint8_t* now = &flash->red;
                for (int i = 0; i < 4; ++i) {
                    int change = static_cast<int>(
                        static_cast<int64_t>(static_cast<double>(flash->start[i] - flash->end[i]) * t));
                    now[i] = static_cast<uint8_t>(flash->end[i] + change);
                }
            }
        }
        value = NextValue(Flashes());
        if (dead) {
            Remove(Flashes(), dead);
            Mem::Manager::Get()->Free(dead);
        }
    }
}

// 0x00491910
void __cdecl DrawFlashes(int viewport, uint32_t pass)
{
    for (void* value = FirstValue(Flashes()); value; value = NextValue(Flashes())) {
        auto* flash = static_cast<Flash*>(value);
        if (flash->viewport == viewport && !((flash->flags ^ pass) & 1))
            DrawFlash(flash);
    }
}

// --- the tables ---

// 0x00492150
void __cdecl InitEffects()
{
    if (!Flashes())
        Flashes() = NewTable();
    if (!SpawnedPolys())
        SpawnedPolys() = NewTable();
    if (!SplatSets())
        SplatSets() = NewTable();
    if (!RecentSplats())
        RecentSplats() = NewTable();
    if (!Shatters())
        Shatters() = NewTable();
    CompiledOut();
    CompiledOut();
    auto& stack = GameVariable<sVertexStack>(0x006FC4A0);
    stack.used = 0;
    stack.vertexSize = 0;
    std::memset(stack.data, 3, sizeof(stack.data));
}

// 0x00492340
void __cdecl KillAllEffects()
{
    for (void* flash = FirstValue(Flashes()); flash;) {
        void* next = NextValue(Flashes());
        Remove(Flashes(), flash);
        Mem::Manager::Get()->Free(flash);
        flash = next;
    }
    KillAllSpawnedPolys();
    KillAllTextureSplats();
    KillAllShatters();
}

} // namespace Engine

} // namespace Nx

REPLACE(0x00490D80, &Nx::sVertexStack::Pop);
REPLACE_SOURCE_ONLY(0x00490DB0, Nx::DrawTextureSplats); // a jump to 0x004B9380
REPLACE(0x00490DC0, Nx::Engine::ShatterGeom);
REPLACE(0x00490DE0, Nx::Engine::EnableFog);
REPLACE(0x00490E00, Nx::Engine::SetFogDistance);
REPLACE(0x00490E10, Nx::Engine::SetFogExponent);
REPLACE(0x00490E20, Nx::Engine::SetFogColor);
REPLACE(0x00490E40, &Nx::sSplatParams::Construct);
REPLACE(0x00490E80, &Nx::CShatter::DestroyBody);
REPLACE_UNCHECKED(0x00490EC0, Nx::Engine::AgeTextureSplatsStub);
REPLACE(0x00491110, Nx::Engine::SetShatterParams);
REPLACE(0x00491290, Nx::Engine::SetLevelColor);
REPLACE(0x004912C0, &Nx::CShatter::Construct);
REPLACE(0x00491380, &Nx::CShatter::Destroy);
REPLACE(0x004913D0, &Nx::CShatter::Step);
REPLACE(0x004915A0, Nx::Engine::KillAllTextureSplats);
REPLACE(0x00491630, Nx::Engine::KillTextureSplatsInBox);
REPLACE(0x004916F0, Nx::Engine::KillTextureSplatsOfTexture);
REPLACE(0x00491790, Nx::Engine::UpdateFlashes);
REPLACE(0x00491910, Nx::Engine::DrawFlashes);
REPLACE(0x00491F10, Nx::Engine::LightAllTextureSplats);
REPLACE(0x00491F70, Nx::Engine::AgeAllTextureSplats);
REPLACE(0x00492000, Nx::Engine::UpdateShatters);
REPLACE(0x004920E0, Nx::Engine::DrawShatters);
REPLACE(0x00492150, Nx::Engine::InitEffects);
REPLACE(0x00492340, Nx::Engine::KillAllEffects);
REPLACE(0x004923E0, Nx::Engine::AddFlash);
REPLACE(0x004928A0, Nx::Engine::CreateTextureSplatAt);
