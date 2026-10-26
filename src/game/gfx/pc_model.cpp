// The PC model: see pc_model.h.

#include "game/gfx/pc_model.h"

#include <cmath>

#include "game/core/manager.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Nx {

namespace {

constexpr uint32_t kModelVtable = 0x0064A4B4;

void* GeomOf(const sModelGeomNode* node) { return *node->entry; }

// The engine's CGeom calls (jumps through the geom's vtable).
Mth::Vector* GetGeomBoundingSphere(void* geom, Mth::Vector* out)
{
    return GameFunction<Mth::Vector*(__thiscall*)(void*, Mth::Vector*)>(0x0048DE90)(geom, out);
}
void SetGeomBoundingSphere(void* geom, const Mth::Vector* sphere)
{
    GameFunction<void(__thiscall*)(void*, const Mth::Vector*)>(0x0048DEB0)(geom, sphere);
}

} // namespace

// 0x004BAD90
CPcModel* CPcModel::Construct()
{
    GameFunction<void*(__thiscall*)(void*)>(0x00494C40)(this);
    vtable = reinterpret_cast<void**>(kModelVtable);
    unknown4C = 0;
    return this;
}

// 0x004BADB0
void CPcModel::DestroyBody()
{
    vtable = reinterpret_cast<void**>(kModelVtable);
    GameFunction<void(__thiscall*)(void*)>(0x00494D00)(this);
}

// 0x004BADC0
CPcModel* CPcModel::Destroy(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x004BADF0
void CPcModel::SetBoundingSphere(const Mth::Vector* sphere)
{
    sModelGeomNode* head = geoms;
    for (sModelGeomNode* node = head ? head->next : nullptr; node != head; node = node->next)
        SetGeomBoundingSphere(GeomOf(node), sphere);
}

// 0x004BAE30
Mth::Vector* CPcModel::GetBoundingSphere(Mth::Vector* out)
{
    if (geomCount == 0) {
        *out = {0.0f, 0.0f, 0.0f, 0.0f};
        return out;
    }
    sModelGeomNode* head = geoms;
    sModelGeomNode* node = head ? head->next : nullptr;
    Mth::Vector scratch;
    Mth::Vector sphere = *GetGeomBoundingSphere(GeomOf(node), &scratch);
    for (node = node->next; node != head; node = node->next) {
        Mth::Vector other = *GetGeomBoundingSphere(GeomOf(node), &scratch);
        float dx = static_cast<float>(static_cast<double>(other.x) - sphere.x);
        float dy = static_cast<float>(static_cast<double>(other.y) - sphere.y);
        float dz = static_cast<float>(static_cast<double>(other.z) - sphere.z);
        float dw = static_cast<float>(static_cast<double>(other.w) - sphere.w);
        float distance = static_cast<float>(
            std::sqrt(static_cast<double>(dz) * dz + static_cast<double>(dy) * dy + static_cast<double>(dx) * dx));
        if (static_cast<double>(other.w) + distance <= sphere.w)
            continue; // (inside the one so far)
        if (static_cast<double>(sphere.w) + distance <= other.w) {
            sphere = other; // (the one so far inside it)
            continue;
        }
        // Both: the sphere around the two - its centre half way between their far sides.
        double t = static_cast<double>(dw) / distance;
        double sumX = static_cast<double>(other.x) + sphere.x;
        double sumY = static_cast<double>(other.y) + sphere.y;
        float sumZ = static_cast<float>(static_cast<double>(other.z) + sphere.z);
        float sumW = static_cast<float>(static_cast<double>(other.w) + sphere.w);
        float offsetY = static_cast<float>(dy * t);
        float offsetZ = static_cast<float>(dz * t);
        float x = static_cast<float>(dx * t + sumX);
        float y = static_cast<float>(offsetY + sumY);
        sphere.z = static_cast<float>((static_cast<double>(offsetZ) + sumZ) * 0.5f);
        sphere.x = static_cast<float>(static_cast<double>(x) * 0.5f);
        sphere.y = static_cast<float>(static_cast<double>(y) * 0.5f);
        sphere.w = static_cast<float>((static_cast<double>(sumW) + distance) * 0.5f);
    }
    out->x = sphere.x;
    out->y = sphere.y;
    out->z = sphere.z;
    out->w = sphere.w + sphere.w;
    return out;
}

} // namespace Nx

REPLACE(0x004BAD90, &Nx::CPcModel::Construct);
REPLACE(0x004BADB0, &Nx::CPcModel::DestroyBody);
REPLACE(0x004BADC0, &Nx::CPcModel::Destroy);
REPLACE(0x004BADF0, &Nx::CPcModel::SetBoundingSphere);
REPLACE(0x004BAE30, &Nx::CPcModel::GetBoundingSphere);
