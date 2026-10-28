// The engine's flexible particle manager: see flexible_particle_manager.h. From the disassembly of 0x0049A8E0-0x0049B05E
// (0x0049A8E0 isn't a Ghidra function).

#include "game/gfx/flexible_particle_manager.h"

#include <new>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/gfx/camera.h"
#include "game/gfx/particle_system.h"
#include "game/gfx/pc_flexible_particles.h"
#include "game/script/struct.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Nx {

namespace {

constexpr uint32_t kManagerVtable = 0x006494B0;
void** const kHeadVtable = reinterpret_cast<void**>(0x006494C4);
void** const kNodeVtable = reinterpret_cast<void**>(0x00649420); // (Lst::Node<CParticleSystem>'s own)
const char* const kHeapName = reinterpret_cast<const char*>(0x006494C8);     // "FlxParticles"
const char* const kTempHeapName = reinterpret_cast<const char*>(0x006494B4); // "TEMPFlxPrtcl"
constexpr uint32_t kHeapSizeSymbol = 0x91CB7552;
constexpr uint32_t kFrameRateSymbol = 0x3214C818;
constexpr int kDefaultHeapSize = 0x5000;
constexpr float kFramesPerSecond = 60.0f; // 0x00645EA0

float& FrameTimeScale() { return GameVariable<float>(0x00700EF4); }
bool GamePaused() { return GameVariable<bool>(0x006FA7D4); }
int DrawnViewport() { return GameVariable<int>(0x006FA7B8); }

// A virtual scalar deleting destructor (slot 0) with flags 1.
void DeleteVirtual(void* object)
{
    void** vtable = *static_cast<void***>(object);
    reinterpret_cast<void*(__thiscall*)(void*, unsigned int)>(vtable[0])(object, 1);
}

// The list's systems: the first after the head, the one after `system` - null at the head (its data -1) or a null link.
CParticleSystem* SystemOf(Mem::ListNode* node)
{
    if (node->data == reinterpret_cast<void*>(-1) || !node)
        return nullptr;
    return reinterpret_cast<CParticleSystem*>(reinterpret_cast<uint8_t*>(node) - offsetof(CParticleSystem, m_node));
}
CParticleSystem* FirstSystem(Mem::ListNode* head) { return head ? SystemOf(head->next) : nullptr; }
CParticleSystem* NextSystem(CParticleSystem* system) { return SystemOf(system->m_node.next); }

// _ftol: truncated to a 64-bit integer, its low half.
int Truncate(double value) { return static_cast<int>(static_cast<int64_t>(value)); }

} // namespace

// 0x0049AF20
CFlexibleParticleManager* CFlexibleParticleManager::Construct()
{
    m_vtable = reinterpret_cast<void**>(kManagerVtable);
    int size = Script::GetInteger(kHeapSizeSymbol);
    if (!size)
        size = kDefaultHeapSize;
    void* memory = Mem::Calloc(sizeof(Mem::AllocRegion));
    mp_region = memory ? new (memory) Mem::AllocRegion(static_cast<uint32_t>(size)) : nullptr;
    mp_heap = Mem::Manager::Get()->AddHeap(mp_region, 1, kHeapName);
    mp_tempRegion = nullptr;
    mp_tempHeap = nullptr;
    auto* head = static_cast<Mem::ListNode*>(Mem::Calloc(sizeof(Mem::ListNode)));
    if (head) {
        head->data = reinterpret_cast<void*>(-1);
        head->priority = 0;
        head->next = head;
        head->prev = head;
        head->vtable = kHeadVtable;
    }
    mp_systems = head;
    m_colourScale[0] = m_colourScale[1] = m_colourScale[2] = m_colourScale[3] = 1.0f;
    float rate = Script::GetFloat(kFrameRateSymbol);
    // (0 gives 60; anything else, NaN too, 60 / it)
    FrameTimeScale() = rate == 0.0f ? kFramesPerSecond : static_cast<float>(kFramesPerSecond / static_cast<double>(rate));
    return this;
}

// 0x0049AA10
void CFlexibleParticleManager::DestroyBody()
{
    m_vtable = reinterpret_cast<void**>(kManagerVtable);
    if (mp_systems)
        DeleteVirtual(mp_systems);
    DeleteTempHeap();
    Mem::Manager::Get()->DeleteHeap(reinterpret_cast<Mem::Allocator*>(mp_heap));
    if (mp_region)
        DeleteVirtual(mp_region);
}

// 0x0049B040
CFlexibleParticleManager* CFlexibleParticleManager::Destroy(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        Mem::Free(this);
    return this;
}

// 0x0049A9D0
void CFlexibleParticleManager::DeleteTempHeap()
{
    if (!mp_tempHeap)
        return;
    Mem::Manager::Get()->DeleteHeap(reinterpret_cast<Mem::Allocator*>(mp_tempHeap));
    if (mp_tempRegion)
        DeleteVirtual(mp_tempRegion);
    mp_tempHeap = nullptr;
    mp_tempRegion = nullptr;
}

// 0x0049AA80
bool CFlexibleParticleManager::CreateTempHeap(uint32_t size)
{
    DeleteTempHeap();
    void* memory = Mem::Calloc(sizeof(Mem::AllocRegion));
    mp_tempRegion = memory ? new (memory) Mem::AllocRegion(size) : nullptr;
    if (!mp_tempRegion)
        return false;
    mp_tempHeap = Mem::Manager::Get()->AddHeap(mp_tempRegion, 1, kTempHeapName);
    return mp_tempHeap != nullptr;
}

// 0x0049A8E0
bool CFlexibleParticleManager::HasTempHeap() { return mp_tempHeap != nullptr; }

// 0x0049A8F0
int __cdecl CFlexibleParticleManager::CompareDistance(const void* a, const void* b)
{
    auto* camera = static_cast<Gfx::Camera*>(GameFunction<void*(__cdecl*)(int)>(0x004A09C0)(DrawnViewport()));
    const Mth::Vector& at = *camera->GetPos();
    const CParticleSystem* first = *static_cast<CParticleSystem* const*>(a);
    const CParticleSystem* second = *static_cast<CParticleSystem* const*>(b);
    // (the original keeps some differences as floats and others at 53 bits)
    float x1 = static_cast<float>(static_cast<double>(first->m_pos.x) - at.x);
    float y1 = static_cast<float>(static_cast<double>(first->m_pos.y) - at.y);
    float z1 = static_cast<float>(static_cast<double>(first->m_pos.z) - at.z);
    float x2 = static_cast<float>(static_cast<double>(second->m_pos.x) - at.x);
    double y2 = static_cast<double>(second->m_pos.y) - at.y;
    double z2 = static_cast<double>(second->m_pos.z) - at.z;
    double distance2 = (z2 * z2 + y2 * y2) + static_cast<double>(x2) * x2;
    double distance1 = (static_cast<double>(z1) * z1 + static_cast<double>(y1) * y1) + static_cast<double>(x1) * x1;
    int order = Truncate(distance2 - distance1);
    if (order)
        return order;
    return static_cast<int>(second->m_priority) - static_cast<int>(first->m_priority);
}

// 0x0049AB50
void CFlexibleParticleManager::RenderSystems(bool force)
{
    CParticleSystem* systems[256] = {};
    int count = 0;
    for (CParticleSystem* system = FirstSystem(mp_systems); system; system = NextSystem(system)) {
        if (system->m_wantsTime)
            systems[count++] = system; // (no bound, as the original)
    }
    if (!count)
        return;
    GameFunction<void(__cdecl*)(void*, size_t, size_t, int(__cdecl*)(const void*, const void*))>(0x006242A0)(
        systems, static_cast<size_t>(count), sizeof(systems[0]), &CompareDistance); // (the game's qsort)
    bool always = force || GamePaused();
    for (int i = 0; i < count; ++i)
        systems[i]->RenderIfVisible(always);
}

// 0x0049AC20
CParticleSystem* CFlexibleParticleManager::FindSystem(uint32_t name)
{
    for (CParticleSystem* system = FirstSystem(mp_systems); system; system = NextSystem(system)) {
        if (system->m_name == name)
            return system;
    }
    return nullptr;
}

// 0x0049AC60
CParticleSystem* CFlexibleParticleManager::CreateSystem(uint32_t name, uint32_t type, int maxParticles, int maxStreams,
                                                        uint32_t texture, uint32_t blendMode, int fixedAlpha,
                                                        int segments, float unknown, int history, int perm,
                                                        bool useLevelColour, const char* model)
{
    Mem::Manager::Get()->PushContext(reinterpret_cast<Mem::Allocator*>(mp_tempHeap ? mp_tempHeap : mp_heap));
    auto* system = static_cast<CParticleSystem*>(CreateFlexibleParticles(
        name, type, maxParticles, maxStreams, texture, blendMode, fixedAlpha, segments, unknown, history, model));
    Mem::Manager::Get()->PopContext();
    if (!system)
        return nullptr;
    if (!(system->m_flags & 4u)) { // (not everything allocated)
        DeleteVirtual(system);
        return nullptr;
    }
    system->unknown12E = perm != 0;
    system->m_flags |= 1u;
    system->SetWantsTime(1);
    system->m_flags |= 0x10000u;
    system->m_emitting = false;
    if (useLevelColour) {
        system->m_flags |= 0x10000u;
        system->SetColourScale(m_colourScale);
    }
    Mem::ListNode* node = &system->m_node;
    node->prev = mp_systems->prev;
    node->next = mp_systems;
    mp_systems->prev->next = node;
    mp_systems->prev = node;
    return system;
}

// 0x0049AD60
void CFlexibleParticleManager::RemoveSystem(CParticleSystem* system)
{
    if (!system)
        return;
    Mem::ListNode* node = &system->m_node;
    node->prev->next = node->next;
    node->next->prev = node->prev;
    node->next = node;
    node->prev = node;
}

// 0x0049AD90
void CFlexibleParticleManager::DestroySystemNow(uint32_t name)
{
    if (CParticleSystem* system = FindSystem(name))
        DeleteVirtual(system);
}

// 0x0049ADB0
void CFlexibleParticleManager::DestroySystemWhenEmpty(uint32_t name)
{
    if (CParticleSystem* system = FindSystem(name)) {
        system->m_dying = true;
        system->m_flags |= 1u;
    }
}

// 0x0049ADE0
void CFlexibleParticleManager::DestroyGroup(uint32_t group)
{
    CParticleSystem* system = FirstSystem(mp_systems);
    while (system) {
        CParticleSystem* next = NextSystem(system);
        if (system->m_groupId == group)
            DeleteVirtual(system);
        system = next;
    }
}

// 0x0049AE30
void CFlexibleParticleManager::DestroyNonPersistent()
{
    CParticleSystem* system = FirstSystem(mp_systems);
    while (system) {
        CParticleSystem* next = NextSystem(system);
        if (!system->unknown12E)
            DeleteVirtual(system);
        system = next;
    }
}

// 0x0049AE80
void CFlexibleParticleManager::SetColourScale(const float* rgba)
{
    if (!mp_systems)
        return;
    for (CParticleSystem* system = FirstSystem(mp_systems); system; system = NextSystem(system)) {
        if (system->m_flags & 0x10000u)
            system->SetColourScale(rgba);
    }
    for (int i = 0; i < 4; ++i)
        m_colourScale[i] = rgba[i];
}

// 0x0049AEF0
Mem::ListNode* CParticleSystemListHead::Destroy(unsigned int flags)
{
    auto* head = reinterpret_cast<Mem::ListNode*>(this);
    head->UnlinkAs(kNodeVtable); // (the node's body, 0x00496D10)
    if (flags & 1)
        Mem::Free(head);
    return head;
}

} // namespace Nx

REPLACE(0x0049AF20, &Nx::CFlexibleParticleManager::Construct);
REPLACE(0x0049AA10, &Nx::CFlexibleParticleManager::DestroyBody);
REPLACE(0x0049B040, &Nx::CFlexibleParticleManager::Destroy);
REPLACE(0x0049A9D0, &Nx::CFlexibleParticleManager::DeleteTempHeap);
REPLACE(0x0049AA80, &Nx::CFlexibleParticleManager::CreateTempHeap);
REPLACE(0x0049A8E0, &Nx::CFlexibleParticleManager::HasTempHeap);
REPLACE(0x0049A8F0, Nx::CFlexibleParticleManager::CompareDistance);
REPLACE(0x0049AB50, &Nx::CFlexibleParticleManager::RenderSystems);
REPLACE(0x0049AC20, &Nx::CFlexibleParticleManager::FindSystem);
REPLACE(0x0049AC60, &Nx::CFlexibleParticleManager::CreateSystem);
REPLACE(0x0049AD60, &Nx::CFlexibleParticleManager::RemoveSystem);
REPLACE(0x0049AD90, &Nx::CFlexibleParticleManager::DestroySystemNow);
REPLACE(0x0049ADB0, &Nx::CFlexibleParticleManager::DestroySystemWhenEmpty);
REPLACE(0x0049ADE0, &Nx::CFlexibleParticleManager::DestroyGroup);
REPLACE(0x0049AE30, &Nx::CFlexibleParticleManager::DestroyNonPersistent);
REPLACE(0x0049AE80, &Nx::CFlexibleParticleManager::SetColourScale);
REPLACE(0x0049AEF0, &Nx::CParticleSystemListHead::Destroy);
